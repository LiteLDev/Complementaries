// U10 `building` module: hooks and event handlers (docs/spec/building.md).

#include "feature/building/Building.h"

#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/util/Random.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/animal/Horse.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectIds.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemTag.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/FireBlock.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("building", cm::feature::building::BuildingConfig);

namespace cm::feature::building {

namespace {

using Sound = SharedTypes::Legacy::LevelSoundEvent;

BuildingConfig const& cfg() { return config::get<BuildingConfig>(); }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
double roll() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

bool serverSide(BlockSource& region) { return !region.getLevel().isClientSide(); }

ItemStack makeItem(std::string_view id, int count) {
    ItemStack s;
    s.reinit(id, count, 0);
    return s;
}

bool itemHasTag(ItemStack const& item, char const* tag) {
    if (item.isNull()) return false;
    return item.hasTag(ItemTag{HashedString(tag)});
}

Vec3 center(BlockPos const& p) { return p.center(); }

void dropItem(BlockSource& region, BlockPos const& pos, ItemStack const& item) {
    region.getLevel().getSpawner().spawnItem(region, item, nullptr, center(pos), 0);
}

/// The block currently known to the server at `pos`, sent to `player` (undoes a client-side
/// predicted placement we refused).
void resyncBlock(Player& player, BlockSource& region, BlockPos const& pos) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::UpdateBlock);
    if (!pkt) return;
    auto& p        = static_cast<UpdateBlockPacket&>(*pkt);
    p.mPos         = pos;
    p.mLayer       = 0;
    p.mUpdateFlags = 3;
    p.mRuntimeId   = region.getBlock(pos).mNetworkId;
    p.sendTo(player);
}

/// Uses one of the item in the player's hand (not in creative).
void consumeHeld(Player& player) {
    if (player.isCreative()) return;
    ItemStack held = player.getSelectedItem();
    if (held.isNull()) return;
    held.remove(1);
    if (held.mCount == 0) held = ItemStack();
    player.setSelectedItem(held);
    player.sendInventory(false);
}

/// Damages the held tool by 1 (not in creative).
void damageHeld(Player& player) {
    if (player.isCreative()) return;
    ItemStack held = player.getSelectedItem();
    if (held.isNull()) return;
    held.hurtAndBreak(1, &player);
    player.setSelectedItem(held);
    player.sendInventory(false);
}

void playBlockSound(Player& player, BlockSource& region, Sound sound, BlockPos const& pos, Block const& block) {
    player.playSound(region, sound, center(pos), static_cast<int>(block.mNetworkId));
}

std::string replacePrefix(std::string const& id, std::string_view from, std::string_view to) {
    std::string const ns = "complementaries:";
    if (id.rfind(ns + std::string(from), 0) != 0) return {};
    return ns + std::string(to) + id.substr(ns.size() + from.size());
}

// ---- deferred world edits -----------------------------------------------------------------------
// A cancelled PlayerInteractBlockEvent rolls back the block changes made during it: edit next tick.
std::vector<std::function<void()>> gNextTick;
void defer(std::function<void()> fn) { gNextTick.push_back(std::move(fn)); }

Player* playerByUuid(mce::UUID const& uuid) {
    auto level = ll::service::getLevel();
    return level ? level->getPlayer(uuid) : nullptr;
}

struct LastClick {
    BlockPos      pos;
    unsigned char face  = 0;
    Vec3          click = Vec3{0.5f, 0.5f, 0.5f};
    std::uint64_t tick  = 0;
    bool          handled = false;
};
std::unordered_map<std::string, LastClick> gClicks; // player uuid -> last block click
std::uint64_t                              gTicks = 0;

/// The flipped state of a brace placed by `player` (`face` / `click` of the click): the diagonal
/// follows the half of the face the player pointed at.
bool braceFlipped(Player const& player, unsigned char face, Vec3 const& click) {
    auto frac = [](float v) { return v - std::floor(v); };
    float fx = frac(click.x), fz = frac(click.z);
    switch (face) {
    case 2: // north face, seen from the north: right is west
        return fx < 0.5f;
    case 3: // south face: right is east
        return fx > 0.5f;
    case 4: // west face, seen from the west: right is south
        return fz > 0.5f;
    case 5: // east face: right is north
        return fz < 0.5f;
    default: {
        // top / bottom: along the player's right
        auto [dx, dz] = game::facingOffset((game::playerFacing(player) + 1) & 3);
        return (fx - 0.5f) * static_cast<float>(dx) + (fz - 0.5f) * static_cast<float>(dz) > 0.0f;
    }
    }
}

// ---- interactions -------------------------------------------------------------------------------

BlockPos adjacent(BlockPos const& pos, unsigned char face) {
    switch (face) {
    case 0:
        return {pos.x, pos.y - 1, pos.z};
    case 1:
        return {pos.x, pos.y + 1, pos.z};
    case 2:
        return {pos.x, pos.y, pos.z - 1};
    case 3:
        return {pos.x, pos.y, pos.z + 1};
    case 4:
        return {pos.x - 1, pos.y, pos.z};
    default:
        return {pos.x + 1, pos.y, pos.z};
    }
}

/// What a click with `item` on `clicked` at `pos` does; empty when we don't handle it.
std::function<void(Player&, BlockSource&)>
interactionFor(Player& player, BlockSource& region, ItemStack const& item, BlockPos const& pos, unsigned char face, Vec3 const& click) {
    if (item.isNull()) return {};
    std::string const itemId  = item.getTypeName();
    auto const&       clicked = region.getBlock(pos);
    Kind              cKind   = kindOf(clicked);
    BlockPos const    side    = adjacent(pos, face);

    // slabs: merge into a double slab
    if (auto const* itemBlock = game::blockWith(itemId); itemBlock && kindOf(*itemBlock) == Kind::Slab) {
        auto single = [&](Block const& b) { return b.getTypeName() == itemId && stateOf(b, "complementaries:double").value_or(1) == 0; };
        std::optional<BlockPos> target;
        if (single(clicked)) {
            // Java: the top face of a bottom slab, or a side in its empty half (and the mirror for top slabs)
            int  half  = stateOf(clicked, "minecraft:vertical_half").value_or(0);
            bool upper = click.y - std::floor(click.y) > 0.5f;
            if (half == 0 && (face == 1 || (face >= 2 && upper))) target = pos;
            if (half == 1 && (face == 0 || (face >= 2 && !upper))) target = pos;
        }
        if (!target && single(region.getBlock(side))) target = side;
        if (!target) return {};
        return [t = *target, itemId](Player& p, BlockSource& r) {
            auto const& b = r.getBlock(t);
            if (b.getTypeName() != itemId) return;
            if (auto const* d = withState(b, "complementaries:double", 1); d && game::setBlock(r, t, *d)) {
                consumeHeld(p);
                playBlockSound(p, r, Sound::Place, t, *d);
                log().info("building: double slab {} at {} {} {}", itemId, t.x, t.y, t.z);
            }
        };
    }

    // ash: add a layer
    if (itemId == kAsh) {
        auto stackable = [](Block const& b) { return kindOf(b) == Kind::Ash && ashLayers(b) < 8; };
        std::optional<BlockPos> target;
        if (stackable(clicked) && face == 1) target = pos;
        else if (stackable(region.getBlock(side))) target = side;
        if (!target) return {};
        return [t = *target](Player& p, BlockSource& r) {
            if (addAsh(r, t, 1) > 0) {
                consumeHeld(p);
                playBlockSound(p, r, Sound::Place, t, r.getBlock(t));
                log().info("building: ash layer added at {} {} {}", t.x, t.y, t.z);
            }
        };
    }

    // hoe on gravel: rake it
    if (clicked.getTypeName() == "minecraft:gravel" && face != 0 && itemHasTag(item, "minecraft:is_hoe")) {
        BlockPos above{pos.x, pos.y + 1, pos.z};
        if (!region.getBlock(above).isAir()) return {};
        int facing = game::playerFacing(player);
        return [pos, facing](Player& p, BlockSource& r) {
            if (r.getBlock(pos).getTypeName() != "minecraft:gravel") return;
            int         shape = computeRakedShape(r, pos, rakedShapeForFacing(facing));
            auto const* raked = game::blockWith(kRakedGravel, {{"complementaries:shape", shape}});
            if (!raked || !game::setBlock(r, pos, *raked)) return;
            damageHeld(p);
            playBlockSound(p, r, Sound::ItemUseOn, pos, *raked);
            log().info("building: raked gravel at {} {} {}", pos.x, pos.y, pos.z);
        };
    }

    // daub into a timber frame
    if (itemId == kDaub && cKind == Kind::Timber && cfg().replaceDaub) {
        return [pos](Player& p, BlockSource& r) {
            auto const& b    = r.getBlock(pos);
            std::string daub = replacePrefix(b.getTypeName(), "timber_", "daub_");
            if (kindOf(b) != Kind::Timber || daub.empty()) return;
            auto const* nb = game::blockWith(daub, {{"complementaries:flipped", stateOf(b, "complementaries:flipped").value_or(0)}});
            if (!nb || !game::setBlock(r, pos, *nb)) return;
            consumeHeld(p);
            playBlockSound(p, r, Sound::Place, pos, *nb);
            log().info("building: filled {} with daub at {} {} {}", b.getTypeName(), pos.x, pos.y, pos.z);
        };
    }

    // a timber frame put around daub
    if (clicked.getTypeName() == kDaub) {
        std::string daubVariant = replacePrefix(itemId, "timber_", "daub_");
        if (!daubVariant.empty() && player.isSneaking() == cfg().swapOnShift) {
            bool flipped = braceFlipped(player, face, click);
            return [pos, daubVariant, flipped](Player& p, BlockSource& r) {
                if (r.getBlock(pos).getTypeName() != kDaub) return;
                auto const* nb = game::blockWith(daubVariant, {{"complementaries:flipped", flipped ? 1 : 0}});
                if (!nb || !game::setBlock(r, pos, *nb)) return;
                consumeHeld(p);
                playBlockSound(p, r, Sound::Place, pos, *nb);
                log().info("building: framed daub as {} at {} {} {}", daubVariant, pos.x, pos.y, pos.z);
            };
        }
    }

    // axe on a daub frame: take the frame off
    if (cKind == Kind::DaubFramed && cfg().axesStrip && itemHasTag(item, "minecraft:is_axe")) {
        return [pos](Player& p, BlockSource& r) {
            auto const& b      = r.getBlock(pos);
            std::string timber = replacePrefix(b.getTypeName(), "daub_", "timber_");
            auto const* daub   = game::blockWith(kDaub);
            if (kindOf(b) != Kind::DaubFramed || timber.empty() || !daub) return;
            if (!game::setBlock(r, pos, *daub)) return;
            if (!p.isCreative()) dropItem(r, pos, makeItem(timber, 1));
            damageHeld(p);
            playBlockSound(p, r, Sound::Break, pos, *daub);
            log().info("building: stripped {} at {} {} {}", timber, pos.x, pos.y, pos.z);
        };
    }
    return {};
}

void onInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    auto& region = player.getDimensionBlockSource();
    if (!serverSide(region)) return;
    std::string const uuid  = player.getUuid().asString();
    auto&             last  = gClicks[uuid];
    bool              again = last.pos == ev.blockPos() && last.face == static_cast<unsigned char>(ev.face()) && gTicks - last.tick <= 1 && last.tick != 0;
    bool              wasHandled = again && last.handled;
    last.pos   = ev.blockPos();
    last.face  = static_cast<unsigned char>(ev.face());
    last.click = ev.clickPos();
    last.tick  = gTicks;
    if (wasHandled) {
        // the client repeats the same click: already handled
        ev.cancel();
        return;
    }
    auto action = interactionFor(player, region, ev.item(), ev.blockPos(), static_cast<unsigned char>(ev.face()), ev.clickPos());
    last.handled = static_cast<bool>(action);
    if (!action) return;
    ev.cancel();
    mce::UUID      id   = player.getUuid();
    BlockPos const side = adjacent(ev.blockPos(), static_cast<unsigned char>(ev.face()));
    defer([id, side, action = std::move(action)] {
        auto* p = playerByUuid(id);
        if (!p) return;
        auto& r = p->getDimensionBlockSource();
        action(*p, r);
        // the client may have drawn the block it predicted next to the clicked one: undo it
        resyncBlock(*p, r, side);
    });
}

void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    auto& region = player.getDimensionBlockSource();
    if (!serverSide(region)) return;
    BlockPos const pos   = ev.pos();
    auto const&    block = region.getBlock(pos);
    Kind           kind  = kindOf(block);
    if (kind == Kind::None) return;
    switch (kind) {
    case Kind::Stairs: {
        // Java stairs face the player's look direction
        int facing = game::playerFacing(player);
        int placed = stateOf(block, "minecraft:cardinal_direction").value_or(-1);
        if (placed != facing) {
            if (auto const* nb = withState(block, "minecraft:cardinal_direction", facing)) game::setBlock(region, pos, *nb);
        }
        updateShape(region, pos);
        log().info("building: placed {} facing {} (predicted {})", block.getTypeName(), facing, placed);
        break;
    }
    case Kind::Wall:
        updateShape(region, pos);
        break;
    case Kind::RakedGravel: {
        int shape = computeRakedShape(region, pos, rakedShapeForFacing(game::playerFacing(player)));
        if (auto const* nb = withState(block, "complementaries:shape", shape)) game::setBlock(region, pos, *nb);
        break;
    }
    case Kind::Timber:
    case Kind::DaubFramed: {
        if (!stateOf(block, "complementaries:flipped")) break;
        auto const& last = gClicks[player.getUuid().asString()];
        bool        flip = braceFlipped(player, last.face, last.click);
        if (auto const* nb = withState(block, "complementaries:flipped", flip ? 1 : 0)) game::setBlock(region, pos, *nb);
        break;
    }
    default:
        break;
    }
    if (fallsLikeSand(kind)) scheduleFall(region, pos);
    ensureWeatherTick(region, pos, region.getBlock(pos));
}

void onMobDie(ll::event::MobDieEvent& ev) {
    if (!cfg().ashFromBurningMobs) return;
    auto& mob = ev.self();
    if (mob.isType(ActorType::Player)) return;
    auto& region = mob.getDimensionBlockSource();
    if (!serverSide(region)) return;
    using Cause = SharedTypes::Legacy::ActorDamageCause;
    auto cause  = ev.source().mCause;
    if (!mob.isOnFire() && cause != Cause::Fire && cause != Cause::FireTick && cause != Cause::Lava) return;
    int count = 1 + std::uniform_int_distribution<int>(0, 1)(rng());
    region.getLevel().getSpawner().spawnItem(region, makeItem(kAsh, count), nullptr, mob.getPosition(), 0);
    log().info("building: burning mob dropped {} ash", count);
}

void onTick(ll::event::ServerLevelTickEvent&) {
    ++gTicks;
    if (!gNextTick.empty()) {
        auto run = std::move(gNextTick);
        gNextTick.clear();
        for (auto& fn : run) fn();
    }
    tickGravity(gTicks);
}

bool onFeather(Actor const& actor) {
    auto const& region = actor.getDimensionBlockSourceConst();
    Vec3        feet   = actor.getFeetPos();
    BlockPos    p      = game::blockPosOf(feet);
    for (int dy : {0, -1}) {
        BlockPos q{p.x, p.y + dy, p.z};
        if (kindOf(region.getBlock(q)) == Kind::Feather) return true;
    }
    return false;
}

bool isHorseLike(Actor const& a) { return a.isType(ActorType::Horse) || a.isType(ActorType::Donkey) || a.isType(ActorType::Mule); }

/// Feeds a sugar cube to a horse. True when handled.
bool feedSugarCube(Player& player, Actor& target) {
    if (player.getSelectedItem().isNull() || player.getSelectedItem().getTypeName() != kSugarCube) return false;
    if (!isHorseLike(target)) return false;
    if (!serverSide(player.getDimensionBlockSource())) return false;
    target.heal(2);
    MobEffectInstance speed(static_cast<unsigned>(MobEffectIds::MovementSpeed), EffectDuration{std::max(1, cfg().horseSpeedDuration) * 20});
    speed.mAmplifier = 1;
    target.addEffect(speed);
    consumeHeld(player);
    target.playSound(player.getDimensionBlockSource(), Sound::Eat, target.getPosition(), -1);
    player.getLevel().spawnParticleEffect("minecraft:heart_particle", target.getPosition() + Vec3{0.f, 1.5f, 0.f}, &player.getDimension());
    log().info("building: fed a sugar cube to a horse");
    return true;
}

} // namespace

// ---- hooks --------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    BuildingNeighborChangedHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::neighborChanged,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    BlockPos const& neighborPos
) {
    origin(region, pos, neighborPos);
    Kind kind = kindOf(*this);
    if (kind == Kind::None || !serverSide(region)) return;
    updateShape(region, pos);
    if (fallsLikeSand(kind)) scheduleFall(region, pos);
    ensureWeatherTick(region, pos, region.getBlock(pos));
}

LL_TYPE_INSTANCE_HOOK(
    BuildingQueuedTickHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::queuedTick,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    Random&         random
) {
    origin(region, pos, random);
    Kind kind = kindOf(*this);
    if ((kind != Kind::Ash && kind != Kind::SugarCube) || !serverSide(region)) return;
    if (&region.getBlock(pos) != this) return;
    ensureWeatherTick(region, pos, *this); // the next periodic tick
    scheduleFall(region, pos);             // in case it was left floating (chunk unloaded mid-fall)
    auto& weather = region.getDimension().mWeather;
    if (!weather) return;
    BlockPos above{pos.x, pos.y + 1, pos.z};
    if (!weather->isRainingAt(region, above)) return;
    rainTick(region, pos, random.nextInt(2) == 0);
}

LL_TYPE_INSTANCE_HOOK(
    BuildingFireBurnHook,
    ll::memory::HookPriority::Normal,
    FireBlock,
    &FireBlock::checkBurn,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    int             chance,
    IRandom&        random,
    int             age,
    BlockPos const& firePos
) {
    Block const* before = &region.getBlock(pos);
    origin(region, pos, chance, random, age, firePos);
    if (!serverSide(region)) return;
    auto const& after = region.getBlock(pos);
    if (&after == before || before->isAir() || !after.isAir() || kindOf(*before) == Kind::Ash) return;
    int layers = ashLayersFor(cfg().ashFromFireChance, roll());
    if (layers > 0 && addAsh(region, pos, layers) > 0) {
        log().info("building: {} burned to ash at {} {} {}", before->getTypeName(), pos.x, pos.y, pos.z);
    }
}

#define CM_BUILDING_FALL_HOOK(NAME, TYPE)                                                                              \
    LL_TYPE_INSTANCE_HOOK(                                                                                             \
        NAME,                                                                                                          \
        ll::memory::HookPriority::Normal,                                                                              \
        TYPE,                                                                                                          \
        &TYPE::$causeFallDamageToActor,                                                                                \
        float,                                                                                                         \
        float             distance,                                                                                    \
        float             multiplier,                                                                                  \
        ActorDamageSource source                                                                                       \
    ) {                                                                                                                \
        if (!getLevel().isClientSide() && onFeather(*this)) return 0.0f;                                                          \
        return origin(distance, multiplier, std::move(source));                                                        \
    }
CM_BUILDING_FALL_HOOK(BuildingMobFallHook, Mob)
CM_BUILDING_FALL_HOOK(BuildingPlayerFallHook, Player)
CM_BUILDING_FALL_HOOK(BuildingHorseFallHook, Horse)
#undef CM_BUILDING_FALL_HOOK

LL_TYPE_INSTANCE_HOOK(
    BuildingPlayerInteractHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    InteractionResult,
    Actor&      actor,
    Vec3 const& location
) {
    if (!getLevel().isClientSide() && feedSugarCube(*this, actor)) {
        InteractionResult r{};
        r.mSuccess = true;
        r.mSwing   = true;
        return r;
    }
    return origin(actor, location);
}

// ---- module -------------------------------------------------------------------------------------

namespace {

class BuildingModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteractBlock));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced));
        mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>(onMobDie));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        BuildingNeighborChangedHook::hook();
        BuildingQueuedTickHook::hook();
        BuildingFireBurnHook::hook();
        BuildingMobFallHook::hook();
        BuildingPlayerFallHook::hook();
        BuildingHorseFallHook::hook();
        BuildingPlayerInteractHook::hook();
        game::db::onWorldReady([] { resetKinds(); });
        game::db::onWorldClosed([] {
            resetKinds();
            clearGravity();
            gNextTick.clear();
            gClicks.clear();
        });
    }

    void uninstall() {
        BuildingPlayerInteractHook::unhook();
        BuildingHorseFallHook::unhook();
        BuildingPlayerFallHook::unhook();
        BuildingMobFallHook::unhook();
        BuildingFireBurnHook::unhook();
        BuildingQueuedTickHook::unhook();
        BuildingNeighborChangedHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) {
            if (l) bus.removeListener(l);
        }
        mListeners.clear();
        clearGravity();
        gNextTick.clear();
        gClicks.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace cm::feature::building

CM_REGISTER_MODULE(cm::feature::building::BuildingModule);
