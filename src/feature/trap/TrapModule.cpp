// U17 trap module: config, world lifecycle, events and hooks. The behaviour lives in Spikes.cpp,
// Cage.cpp and Fodder.cpp.

#include <functional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/player/PlayerAttackEvent.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/trap/Trap.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("trap", cm::feature::trap::TrapConfig);

namespace cm::feature::trap {

extern thread_local bool gSpikeHurt;

TrapConfig const& config() { return cm::config::get<TrapConfig>(); }
TrapConfig&       mutableConfig() { return cm::config::mut<TrapConfig>(); }

namespace {

enum class PlaceKind { None, Cage, Spikes };

/// What the current item use is placing (set by the interact event, read by the block change
/// that the placement makes in the same call).
struct Placing {
    PlaceKind   kind = PlaceKind::None;
    int         dim  = 0;
    BlockPos    target{0, 0, 0}; ///< where the item places its block
    CompoundTag cageData;
    spikes::Tip tip;
};

Placing                              gPlacing;
std::vector<std::function<void()>>   gNextTick;
std::set<std::tuple<int, int, int, int>> gCreativeBreaks;

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    return d ? &d->getBlockSourceFromMainChunkSource() : nullptr;
}

Player* playerByUid(int64_t id) {
    auto* actor = actorByUid(id);
    return actor && actor->isPlayer() ? static_cast<Player*>(actor) : nullptr;
}

void later(std::function<void()> fn) { gNextTick.push_back(std::move(fn)); }

/// Java's right-click delay (4 ticks): the client repeats item-use packets while the button is
/// held, and several can arrive in one tick. One use per player per 4 ticks.
std::unordered_map<int64_t, uint64_t> gLastUse;

bool useAllowed(int64_t player) {
    uint64_t now  = currentTick();
    auto     it   = gLastUse.find(player);
    if (it != gLastUse.end() && now < it->second + 4) return false;
    gLastUse[player] = now;
    return true;
}

bool isHoe(std::string const& id) { return id.ends_with("_hoe"); }

/// Unit offset of a clicked face (Bedrock FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
BlockPos faceOffset(int face) {
    switch (face) {
    case 0: return {0, -1, 0};
    case 1: return {0, 1, 0};
    case 2: return {0, 0, -1};
    case 3: return {0, 0, 1};
    case 4: return {-1, 0, 0};
    case 5: return {1, 0, 0};
    default: return {0, 0, 0};
    }
}

/// The block a placement on `face` of `at` fills: `at` itself when replaceable, else its neighbour.
BlockPos placementTarget(BlockSource& region, BlockPos const& at, int face) {
    if (game::replaceable(region, at)) return at;
    auto d = faceOffset(face);
    return BlockPos{at.x + d.x, at.y + d.y, at.z + d.z};
}

/// LeviLamina passes the hit position of the click; accept both absolute and block-relative.
Vec3 absoluteClick(Vec3 const& click, BlockPos const& pos) {
    bool relative = click.x >= 0.f && click.x <= 1.f && click.y >= 0.f && click.y <= 1.f && click.z >= 0.f && click.z <= 1.f;
    if (relative && !(pos.x == 0 && pos.y == 0 && pos.z == 0)) {
        return Vec3{static_cast<float>(pos.x) + click.x, static_cast<float>(pos.y) + click.y, static_cast<float>(pos.z) + click.z};
    }
    return click;
}

void onInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
    Player& player = ev.self();
    if (player.isClientSide()) return;
    ItemStack& item  = ev.item();
    std::string id   = itemId(item);
    BlockPos   at    = ev.blockPos();
    int        dim   = static_cast<int>(player.getDimensionId());
    int64_t    puid  = uid(player);
    auto       block = ev.block();
    int        face  = static_cast<int>(ev.face());
    log().debug("trap: interact {} with {} at {} {} {} face {} sneaking {}", player.getRealName(), id, at.x, at.y, at.z, face, player.isSneaking());
    auto&      region = player.getDimensionBlockSource();

    if (id == kCageId && config().cage.enabled) {
        if (!player.isSneaking()) {
            // cages are placed only while sneaking; otherwise a full cage releases its mob
            ev.cancel();
            if (cage::isFull(item) && useAllowed(puid)) {
                Vec3 click = absoluteClick(ev.clickPos(), at);
                if (face != 1) { // keep the mob out of the clicked block
                    auto d = faceOffset(face);
                    click  = Vec3{click.x + 0.5f * d.x, click.y + (face == 0 ? -1.f : 0.f), click.z + 0.5f * d.z};
                }
                later([puid, click] {
                    if (auto* p = playerByUid(puid)) cage::release(*p, click);
                });
            }
            return;
        }
        if (cage::isFull(item)) {
            gPlacing          = Placing{};
            gPlacing.kind     = PlaceKind::Cage;
            gPlacing.dim      = dim;
            gPlacing.target   = placementTarget(region, at, face);
            gPlacing.cageData = *userData(item);
        }
        return;
    }
    if (id == kTippedId) {
        if (auto tip = spikes::itemTip(item)) {
            gPlacing      = Placing{};
            gPlacing.kind   = PlaceKind::Spikes;
            gPlacing.dim    = dim;
            gPlacing.target = placementTarget(region, at, face);
            gPlacing.tip  = *tip;
        }
        return;
    }
    if (!block) return;
    if (id == "minecraft:lingering_potion" && isBlock(*block, kSpikesId) && stateOf(*block, kTipState) == 0
        && config().bambooSpikes.tippedSpikes && spikes::isPotionValid(item.getAuxValue())) {
        ev.cancel();
        if (!useAllowed(puid)) return;
        later([puid, at] {
            if (auto* p = playerByUid(puid)) spikes::useLingeringPotion(*p, at);
        });
        return;
    }
    if (isHoe(id) && isBlock(*block, kFodderId)) {
        ev.cancel();
        if (!useAllowed(puid)) return;
        later([puid, at] {
            if (auto* p = playerByUid(puid)) fodder::useHoe(*p, at);
        });
    }
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    Block const& prev = ev.previousBlock();
    Block const& next = ev.newBlock();
    std::string const& prevId = prev.getTypeName();
    std::string const& nextId = next.getTypeName();
    if (prevId == nextId && prevId != kFodderId) return;
    bool prevOurs = prevId == kSpikesId || prevId == kCageId || prevId == kFodderId;
    bool nextOurs = nextId == kSpikesId || nextId == kCageId || nextId == kFodderId;
    if (!prevOurs && !nextOurs) return;

    BlockSource& region = ev.blockSource();
    BlockPos     pos    = ev.pos();
    int          dim    = static_cast<int>(region.getDimensionId());
    bool drop = !gCreativeBreaks.contains({dim, pos.x, pos.y, pos.z});

    if (prevId == kSpikesId && nextId != kSpikesId && stateOf(prev, kTipState) > 0) {
        later([dim, pos, drop] {
            if (auto* r = regionOf(dim)) spikes::onRemoved(*r, pos, drop);
        });
    }
    if (prevId == kCageId && nextId != kCageId && stateOf(prev, kFullState) > 0) {
        later([dim, pos, drop] {
            if (auto* r = regionOf(dim)) cage::onRemoved(*r, pos, drop);
        });
    }
    if (gPlacing.kind != PlaceKind::None && gPlacing.dim == dim && gPlacing.target == pos && prevId != nextId) {
        if (gPlacing.kind == PlaceKind::Cage && nextId == kCageId) {
            later([dim, pos, data = gPlacing.cageData] {
                if (auto* r = regionOf(dim)) cage::onPlaced(*r, pos, data);
            });
            gPlacing.kind = PlaceKind::None;
        } else if (gPlacing.kind == PlaceKind::Spikes && nextId == kSpikesId) {
            later([dim, pos, tip = gPlacing.tip] {
                if (auto* r = regionOf(dim)) spikes::onPlaced(*r, pos, tip);
            });
            gPlacing.kind = PlaceKind::None;
        }
    }
    if (prevId == kFodderId || nextId == kFodderId) {
        later([dim, pos] {
            if (auto* r = regionOf(dim)) fodder::settle(*r, pos);
        });
    }
}

void onTick(ll::event::ServerLevelTickEvent& ev) {
    auto pending = std::move(gNextTick);
    gNextTick.clear();
    for (auto& fn : pending) fn();
    gPlacing = Placing{};
    gCreativeBreaks.clear();

    Level& level = ev.level();
    spikes::tick(level);
    fodder::tick(level);
    if (currentTick() % 20 == 0) cage::maintain(level);
}

LL_TYPE_INSTANCE_HOOK(
    TrapInteractHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$interact,
    bool,
    Actor&        entity,
    Vec3 const&   location,
    ::HandSlot    handSlot
) {
    Player& player = this->mPlayer;
    if (!player.isClientSide()) {
        if (cage::isDisplay(entity)) return false;
        auto const& held = player.getSelectedItem();
        if (itemId(held) == kCageId && !cage::isFull(held) && config().cage.enabled) {
            if (cage::tryCatch(player, entity)) {
                gLastUse[uid(player)] = currentTick();
                return true;
            }
        }
    }
    return origin(entity, location, handSlot);
}

using DeathMessage = std::pair<std::string, std::vector<std::string>>;

LL_TYPE_INSTANCE_HOOK(
    TrapDeathMessageHook,
    ll::memory::HookPriority::Normal,
    ActorDamageSource,
    &ActorDamageSource::getDeathMessage,
    DeathMessage,
    std::string const& deadName,
    Actor*             dead
) {
    if (!gSpikeHurt) return origin(deadName, dead);
    // Java SpikePlayerDamageSource: the ".player" variant names the entity with the kill credit
    if (dead && dead->mLastHurtByMobTime > 0) {
        if (auto* killer = actorByUid(dead->mLastHurtByMobId->rawID); killer && killer != dead) {
            std::string name = killer->isPlayer() ? static_cast<Player*>(killer)->getRealName() : killer->getNameTag();
            if (name.empty()) {
                std::string type = killer->getTypeName();
                if (auto colon = type.find(':'); colon != std::string::npos) type = type.substr(colon + 1);
                name = "%entity." + type + ".name";
            }
            return {"death.attack.supplementaries.bamboo_spikes.player", {deadName, name}};
        }
    }
    return {"death.attack.supplementaries.bamboo_spikes", {deadName}};
}

} // namespace

class TrapModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteractBlock));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerAttackEvent>([](ll::event::PlayerAttackEvent& ev) {
            Player& player = ev.self();
            if (player.isClientSide()) return;
            if (cage::isDisplay(ev.target())) {
                ev.cancel();
                return;
            }
            auto const& held = player.getSelectedItem();
            if (itemId(held) == kCageId && !cage::isFull(held) && config().cage.enabled) {
                if (cage::tryCatch(player, ev.target())) {
                    gLastUse[uid(player)] = currentTick();
                    ev.cancel();
                }
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& ev) {
            Player& player = ev.self();
            if (player.isClientSide() || !isCreative(player)) return;
            auto const& p = ev.pos();
            gCreativeBreaks.insert({static_cast<int>(player.getDimensionId()), p.x, p.y, p.z});
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ActorHurtEvent>([](ll::event::ActorHurtEvent& ev) {
            if (cage::isDisplay(ev.self())) ev.cancel();
        }));
        TrapInteractHook::hook();
        TrapDeathMessageHook::hook();

        static bool lifecycle = false; // db listeners cannot be removed: register once per process
        if (!lifecycle) {
            lifecycle = true;
            game::db::onWorldReady([] {
                spikes::onWorldReady();
                cage::onWorldReady();
            });
            game::db::onWorldClosed([] {
                gNextTick.clear();
                gLastUse.clear();
                gPlacing = Placing{};
                spikes::onWorldClosed();
                cage::onWorldClosed();
                fodder::onWorldClosed();
            });
        }
        if (game::db::available()) { // BDS: the world already exists
            spikes::onWorldReady();
            cage::onWorldReady();
        }
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        TrapInteractHook::unhook();
        TrapDeathMessageHook::unhook();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace cm::feature::trap

CM_REGISTER_MODULE(cm::feature::trap::TrapModule);
