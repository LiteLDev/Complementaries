// U12 display: the hat stand entity (HatStandEntity / HatStandItem of Supplementaries).

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/display/Display.h"
#include "feature/display/Internal.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace cm::feature::display {

using Cause = SharedTypes::Legacy::ActorDamageCause;
using SharedTypes::Legacy::ArmorSlot;

namespace {

constexpr int   kHitWindow       = 5;     ///< ticks: a second punch within this breaks the stand
constexpr int   kSkibidiTicks    = 160;   ///< 8 s dance
constexpr int   kScanInterval    = 5;     ///< ticks between hat stand scans
constexpr int   kSwingCooldown   = 10;    ///< ticks between collision swings
constexpr float kMaxHealth       = 20.0f;

struct StandState {
    int   lastHit    = -1000;
    int   lastSwing  = -1000;
    int   skibidiEnd = 0;
    int   hit        = 0;
    int   swing      = 0;
    float health     = kMaxHealth;
};

struct PendingPlace {
    ActorUniqueID player;
    BlockPos      clicked;
    int           face;
    float         yaw;
};

struct HatState {
    std::map<int64, StandState> stands;
    std::vector<PendingPlace>   places;
    std::mt19937                rng{20260521u};
};

HatState& hs() {
    static HatState s;
    return s;
}

/// The tracked state of a stand. A stand seen for the first time (placed, loaded) stops any dance
/// it was saved with: the dance timer lives only in memory.
StandState& stateOf(Actor& stand) {
    auto [it, fresh] = hs().stands.try_emplace(stand.getOrCreateUniqueID().rawID);
    if (fresh) entityEvent(stand, "skibidi_0");
    return it->second;
}

void toggle(Actor& stand, char const* prop, int& value) {
    value ^= 1;
    entityEvent(stand, std::string(prop) + "_" + std::to_string(value));
}

ItemStack hatOf(Actor& stand) { return stand.getArmor(ArmorSlot::Head); }

void setHat(Actor& stand, ItemStack const& item) {
    stand.setArmor(ArmorSlot::Head, item);
    // Items that aren't worn (config `unrestricted`) are drawn by the head's item bone instead.
    bool worn = item.isNull() || canWearOnHatStand(item);
    stand.setCarriedItem(worn ? ItemStack::EMPTY_ITEM() : item);
}

std::pair<int, int> faceOffsetXZ(int face, int& dy) {
    dy = 0;
    switch (face) {
    case 0:
        dy = -1;
        return {0, 0};
    case 1:
        dy = 1;
        return {0, 0};
    case 2:
        return {0, -1};
    case 3:
        return {0, 1};
    case 4:
        return {-1, 0};
    default:
        return {1, 0};
    }
}

float wrapDegrees(float d) {
    d = std::fmod(d, 360.0f);
    if (d >= 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

AABB standBox(Vec3 const& p) {
    return AABB{
        Vec3{p.x - 0.3125f, p.y, p.z - 0.3125f},
        Vec3{p.x + 0.3125f, p.y + 0.75f, p.z + 0.3125f}
    };
}

bool overlaps(AABB const& a, AABB const& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z
        && a.max.z > b.min.z;
}

/// Java setSkibidiIfInCauldron: 2 = tall (cauldron, composter), 1 = hopper / decorated pot.
int skibidiKind(Actor& stand) {
    auto& region = stand.getDimensionBlockSource();
    auto  p      = stand.getPosition();
    for (float dy : {0.01f, -0.2f}) {
        auto const& id = region.getBlock(game::blockPosOf(Vec3{p.x, p.y + dy, p.z})).getTypeName();
        if (id == "minecraft:cauldron" || id == "minecraft:composter") return 2;
        if (id == "minecraft:hopper" || id == "minecraft:decorated_pot") return 1;
    }
    return 0;
}

void startSkibidi(Actor& stand, int kind, int tick) {
    auto& s      = stateOf(stand);
    s.skibidiEnd = tick + kSkibidiTicks;
    entityEvent(stand, "skibidi_" + std::to_string(kind));
}

void processPlace(PendingPlace const& pp) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto* player = level->getPlayer(pp.player);
    if (!player) return;
    ItemStack hand = player->getSelectedItem();
    if (itemId(hand) != kHatStand) return;
    if (!placeHatStand(player, player->getDimensionBlockSource(), pp.clicked, pp.face, pp.yaw)) return;
    if (!isCreative(*player)) {
        hand.remove(1);
        player->setSelectedItem(hand.isNull() || hand.mCount == 0 ? ItemStack::EMPTY_ITEM() : hand);
        player->sendInventory(false);
    }
}

} // namespace

// ---- public API -------------------------------------------------------------------------------------

bool isHatStand(Actor const& actor) { return actor.getTypeName() == kHatStand; }

bool canWearOnHatStand(ItemStack const& item) {
    if (item.isNull()) return false;
    if (config::get<DisplayConfig>().hatStandUnrestricted) return true;
    return item.isHumanoidWearableItem() && item.getArmorSlot() == ArmorSlot::Head;
}

Actor* placeHatStand(Player* player, BlockSource& region, BlockPos const& clicked, int face, float playerYaw) {
    if (face == 0) return nullptr; // Java: never on the bottom of a block
    BlockPos target = clicked;
    if (!game::replaceable(region, clicked)) {
        int  dy       = 0;
        auto [dx, dz] = faceOffsetXZ(face, dy);
        target        = BlockPos{clicked.x + dx, clicked.y + dy, clicked.z + dz};
    }
    if (!game::replaceable(region, target)) return nullptr;
    Vec3 at{static_cast<float>(target.x) + 0.5f, static_cast<float>(target.y), static_cast<float>(target.z) + 0.5f};
    auto box = standBox(at);
    for (auto* a : region.getEntities(box, 0.0f)) {
        if (a && a->isAlive() && a->getTypeName() != kDisplayEntity) return nullptr;
    }
    if (!region.getBlock(target).isAir()) game::clearBlock(region, target);
    ActorUniqueID id{};
    Actor* stand = CommandUtils::spawnEntityAt(region, at, ActorDefinitionIdentifier(std::string(kHatStand)), id, player);
    if (!stand) return nullptr;
    // Java: floor((wrapDegrees(yaw - 180) + 11.25) / 22.5) * 22.5
    int   step = static_cast<int>(std::floor((wrapDegrees(playerYaw - 180.0f) + 11.25f) / 22.5f));
    entityEvent(*stand, "yaw_" + std::to_string(((step % 16) + 16) % 16));
    playSound(region, at.x, at.y, at.z, "mob.armor_stand.place", 0.75f, 0.8f);
    log().info("Display: hat stand placed at {} {} {}", target.x, target.y, target.z);
    return stand;
}

bool useHatStand(Player& player, Actor& stand) {
    if (player.isSneaking()) {
        toggle(stand, "swing", stateOf(stand).swing);
        return true;
    }
    if (player.getPlayerGameType() == GameType::Spectator) return true;
    ItemStack hand = player.getSelectedItem();
    ItemStack head = hatOf(stand);
    auto     where = stand.getPosition();
    if (hand.isNull()) {
        if (head.isNull()) return false;
        setHat(stand, ItemStack::EMPTY_ITEM());
        player.setSelectedItem(head);
        player.sendInventory(false);
        log().info("Display: took {} from a hat stand", itemId(head));
        return true;
    }
    if (!canWearOnHatStand(hand)) return false;
    // Java swapItem
    if (isCreative(player) && head.isNull()) {
        ItemStack one = hand;
        one.set(1);
        setHat(stand, one);
    } else if (hand.mCount > 1) {
        if (!head.isNull()) return false;
        ItemStack one = hand;
        one.set(1);
        setHat(stand, one);
        hand.remove(1);
        player.setSelectedItem(hand);
        player.sendInventory(false);
    } else {
        setHat(stand, hand);
        player.setSelectedItem(head.isNull() ? ItemStack::EMPTY_ITEM() : head);
        player.sendInventory(false);
    }
    playSound(stand.getDimensionBlockSource(), where.x, where.y, where.z, "armor.equip_generic");
    log().info("Display: put {} on a hat stand", itemId(hand));
    return true;
}

void dismantleHatStand(Actor& stand, bool drops) {
    auto& region = stand.getDimensionBlockSource();
    auto  p      = stand.getPosition();
    if (drops) {
        dropItem(region, p.x, p.y + 0.4f, p.z, makeItem(kHatStand));
        auto hat = hatOf(stand);
        if (!hat.isNull()) dropItem(region, p.x, p.y + 0.6f, p.z, hat);
    }
    setHat(stand, ItemStack::EMPTY_ITEM());
    playSound(region, p.x, p.y, p.z, "mob.armor_stand.break");
    hs().stands.erase(stand.getOrCreateUniqueID().rawID);
    log().info("Display: hat stand broken (drops={})", drops);
    stand.despawn();
}

bool hurtHatStand(Actor& stand, Actor* attacker, bool creativeAttacker, int causeId) {
    auto  cause = static_cast<Cause>(causeId);
    auto& s     = stateOf(stand);
    int   tick  = currentTick();
    auto  damage = [&](float amount) {
        s.health -= amount;
        if (s.health <= 0.5f) dismantleHatStand(stand, true);
    };
    switch (cause) {
    case Cause::Override:
    case Cause::Void:
    case Cause::SelfDestruct:
    case Cause::BlockExplosion:
    case Cause::EntityExplosion:
        dismantleHatStand(stand, true);
        return true;
    case Cause::Fire:
    case Cause::Campfire:
    case Cause::SoulCampfire:
        if (stand.isOnFire()) damage(0.15f);
        else stand.setOnFire(5);
        return true;
    case Cause::FireTick:
        if (s.health > 0.5f) damage(4.0f);
        return true;
    case Cause::Lava:
        stand.setOnFire(15);
        return true;
    case Cause::EntityAttack:
    case Cause::Projectile: {
        bool arrow = cause == Cause::Projectile;
        bool byPlayer = attacker && attacker->isPlayer();
        if (!byPlayer && !arrow) return true;
        if (byPlayer && creativeAttacker) {
            dismantleHatStand(stand, false);
            return true;
        }
        if (tick - s.lastHit > kHitWindow && !arrow) {
            toggle(stand, "hit", s.hit);
            auto p = stand.getPosition();
            playSound(stand.getDimensionBlockSource(), p.x, p.y, p.z, "mob.armor_stand.hit", 0.3f, 1.0f);
            s.lastHit = tick;
        } else {
            dismantleHatStand(stand, true);
        }
        return true;
    }
    default:
        return true; // falls, drowning, magic, ... do nothing (Java: not attackable)
    }
}

// ---- hooks and ticking -----------------------------------------------------------------------------------

namespace {

LL_TYPE_INSTANCE_HOOK(
    HatStandInteractHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    InteractionResult,
    Actor&      actor,
    Vec3 const& location
) {
    auto held = itemId(getSelectedItem());
    // Name tags and leads keep their vanilla behaviour (Java: interactAt passes for name tags).
    if (!getLevel().isClientSide() && isHatStand(actor) && held != "minecraft:name_tag" && held != "minecraft:lead") {
        InteractionResult r{};
        bool ok   = useHatStand(*this, actor);
        r.mSuccess = ok;
        r.mSwing   = ok;
        return r;
    }
    return origin(actor, location);
}

ll::event::ListenerPtr gHurt;

} // namespace

namespace detail {

void installHatStand() {
    HatStandInteractHook::hook();
    gHurt = ll::event::EventBus::getInstance().emplaceListener<ll::event::ActorHurtEvent>([](ll::event::ActorHurtEvent& ev) {
        auto& actor = ev.self();
        if (!isHatStand(actor) || actor.getLevel().isClientSide()) return;
        ev.cancel();
        auto const& src      = ev.source();
        Actor*      attacker = nullptr;
        if (src.isEntitySource()) attacker = actor.getLevel().fetchEntity(src.getDamagingEntityUniqueID(), false);
        bool creative = false;
        if (attacker && attacker->isPlayer()) creative = isCreative(*static_cast<Player*>(attacker));
        hurtHatStand(actor, attacker, creative, static_cast<int>(src.mCause));
    });
}

void uninstallHatStand() {
    HatStandInteractHook::unhook();
    if (gHurt) ll::event::EventBus::getInstance().removeListener(gHurt);
    gHurt = nullptr;
}

void resetHatStands() {
    hs().stands.clear();
    hs().places.clear();
}

void queueHatStandPlacement(Player& player, BlockPos const& clicked, int face) {
    hs().places.push_back({player.getOrCreateUniqueID(), clicked, face, player.getRotation().y});
}

void tickHatStands(int tick) {
    auto& h = hs();
    if (!h.places.empty()) {
        auto places = std::move(h.places);
        h.places.clear();
        for (auto const& pp : places) processPlace(pp);
    }
    if (tick % kScanInterval != 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    bool                skibidi = config::get<DisplayConfig>().hatStandSkibidi;
    std::vector<int64>  seen;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || !isHatStand(*actor) || !actor->isAlive()) continue;
        seen.push_back(actor->getOrCreateUniqueID().rawID);
        auto& s   = stateOf(*actor);
        auto  box = standBox(actor->getPosition());
        // Entities walking through the stand make its head swing (Java PendulumAnimation#hitByEntity).
        if (tick - s.lastSwing >= kSwingCooldown) {
            for (auto* other : actor->getDimensionBlockSource().getEntities(box, 0.0f)) {
                if (!other || other == actor || isHatStand(*other) || other->getTypeName() == kDisplayEntity) continue;
                if (!overlaps(box, other->getAABB())) continue;
                toggle(*actor, "swing", s.swing);
                s.lastSwing = tick;
                break;
            }
        }
        if (s.skibidiEnd != 0 && tick >= s.skibidiEnd) {
            s.skibidiEnd = 0;
            entityEvent(*actor, "skibidi_0");
        } else if (skibidi && s.skibidiEnd == 0 && tick % 100 == static_cast<int>(actor->getOrCreateUniqueID().rawID & 0x7f) % 100 / kScanInterval * kScanInterval) {
            if (std::uniform_real_distribution<float>(0.0f, 1.0f)(h.rng) < 0.2f) {
                if (int kind = skibidiKind(*actor)) startSkibidi(*actor, kind, tick);
            }
        }
    }
    // Forget stands that are no longer loaded.
    std::sort(seen.begin(), seen.end());
    std::erase_if(h.stands, [&seen](auto const& e) { return !std::binary_search(seen.begin(), seen.end(), e.first); });
}

} // namespace detail

} // namespace cm::feature::display
