// Rope arrow: shot from bows, places ropes where it lands, picked up again with its charges.

#include <cmath>
#include <map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/RangedWeaponItem.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "feature/rope/Common.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::rope::arrow {

namespace {
constexpr char const* kKey      = "cm:rope:arrows";
constexpr int         kLifetime = 1200; ///< ticks a stuck arrow stays (like vanilla arrows)
constexpr int         kUseTicks = 72000;

struct Tracked {
    int                     charges = 0;
    bool                    pickup  = true; ///< shot by a survival player
    bool                    hit     = false;
    std::optional<BlockPos> anchor; ///< rope column still being extended
    int                     stuck   = 0;
    int                     missing = 0;
};
std::map<std::int64_t, Tracked> gArrows;

bool isRopeArrowItem(ItemStackBase const& s) { return !s.isNull() && s.getTypeName() == kRopeArrow; }
bool isArrowItem(ItemStackBase const& s) { return !s.isNull() && s.getTypeName() == "minecraft:arrow"; }

int chargesOfItem(ItemStackBase const& s) {
    return std::clamp(kArrowCharges - static_cast<int>(s.getDamageValue()), 0, kArrowCharges);
}

void setCharges(ItemStack& s, int charges) {
    s.setDamageValue(static_cast<short>(kArrowCharges - std::clamp(charges, 0, kArrowCharges)));
}

ItemStack ropeArrowItem(int charges) {
    auto s = makeStack(kRopeArrow, 1);
    setCharges(s, charges);
    return s;
}

/// Where the bow takes its ammunition from: -2 offhand, slot index, -1 none. Rope arrows win when
/// they come first (offhand, then the inventory in slot order), like Java's projectile search.
int ropeArrowAmmoSlot(Player& player) {
    auto const& off = player.getOffhandSlot();
    if (isRopeArrowItem(off)) return -2;
    if (isArrowItem(off)) return -1;
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& s = inv.getItem(i);
        if (isRopeArrowItem(s)) return i;
        if (isArrowItem(s)) return -1;
    }
    return -1;
}

bool hasRopeArrow(Player& player) {
    if (isRopeArrowItem(player.getOffhandSlot())) return true;
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (isRopeArrowItem(inv.getItem(i))) return true;
    }
    return false;
}

bool isBow(ItemStack const& s) { return !s.isNull() && s.getTypeName() == "minecraft:bow"; }

bool serverSide(Actor const& a) { return !a.getLevel().isClientSide(); }

Actor* fetch(std::int64_t uid) {
    auto level = ll::service::getLevel();
    return level ? level->fetchEntity(ActorUniqueID{uid}, false) : nullptr;
}
} // namespace

// ---- hooks ---------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    RopeArrowBowUseHook,
    ll::memory::HookPriority::Normal,
    RangedWeaponItem,
    &RangedWeaponItem::$use,
    ItemStack&,
    ItemStack& instance,
    Player&    player,
    ::HandSlot handSlot
) {
    auto& result = origin(instance, player, handSlot);
    // A bow only draws with vanilla arrows: let it draw with rope arrows too.
    if (config().ropeArrowEnabled && isBow(instance) && player.getItemUseDuration() <= 0 && hasRopeArrow(player)) {
        player.startUsingItem(instance, kUseTicks);
    }
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    RopeArrowBowReleaseHook,
    ll::memory::HookPriority::Normal,
    RangedWeaponItem,
    &RangedWeaponItem::$releaseUsing,
    void,
    ItemStack& item,
    Player*    player,
    int        durationLeft
) {
    if (!player || !config().ropeArrowEnabled || !isBow(item) || !serverSide(*player)) {
        origin(item, player, durationLeft);
        return;
    }
    int slot = ropeArrowAmmoSlot(*player);
    if (slot == -1) {
        origin(item, player, durationLeft);
        return;
    }
    float f = static_cast<float>(kUseTicks - durationLeft) / 20.0f;
    f       = (f * f + f * 2.0f) / 3.0f;
    if (f < 0.1f) return;
    f = std::min(f, 1.0f);

    ItemStack ammo = slot == -2 ? player->getOffhandSlot() : player->getInventory().getItem(slot);
    normalize(*player);
    ammo          = slot == -2 ? player->getOffhandSlot() : player->getInventory().getItem(slot);
    bool creative = player->isCreative();
    shoot(*player, chargesOfItem(ammo), f, creative);
    if (!creative) {
        if (slot == -2) player->setOffhandSlot(ItemStack::EMPTY_ITEM());
        else player->getInventory().setItem(slot, ItemStack::EMPTY_ITEM());
        item.hurtAndBreak(1, player);
        player->refreshInventory();
    }
}

LL_TYPE_INSTANCE_HOOK(
    RopeArrowHitHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    Actor&           owner,
    HitResult const& hit
) {
    origin(owner, hit);
    if (hit.mType != HitResultType::Tile || !serverSide(owner) || owner.getTypeName() != kRopeArrow) return;
    onHitBlock(owner, hit.mBlock, static_cast<int>(hit.mFacing));
}

void install() {
    RopeArrowBowUseHook::hook();
    RopeArrowBowReleaseHook::hook();
    RopeArrowHitHook::hook();
}

void uninstall() {
    RopeArrowHitHook::unhook();
    RopeArrowBowReleaseHook::unhook();
    RopeArrowBowUseHook::unhook();
}

// ---- behaviour -----------------------------------------------------------------------------------

Actor* shoot(Player& shooter, int charges, float power, bool creative) {
    auto& region = shooter.getDimensionBlockSource();
    auto  dir    = shooter.getViewVector(1.0f);
    auto  pos    = shooter.getHeadPos();
    auto* actor  = shooter.getLevel()
                       .getSpawner()
                       .spawnProjectile(region, ActorDefinitionIdentifier(std::string(kRopeArrow)), &shooter, pos, dir);
    if (!actor) return nullptr;
    float speed                                                 = power * 3.0f;
    actor->mBuiltInComponents->mStateVectorComponent->mPosDelta = Vec3{dir.x * speed, dir.y * speed, dir.z * speed};
    track(*actor, charges, !creative);
    shooter.getLevel().broadcastSoundEvent(
        region,
        SharedTypes::Legacy::LevelSoundEvent::Bow,
        pos,
        -1,
        ActorSoundIdentifier(shooter),
        false,
        std::nullopt
    );
    log().info("rope: {} shot a rope arrow with {} charges", shooter.getRealName(), charges);
    return actor;
}

void track(Actor& arrowActor, int charges, bool pickup) {
    gArrows[arrowActor.getOrCreateUniqueID().rawID] = Tracked{std::clamp(charges, 0, kArrowCharges), pickup};
}

int chargesOf(Actor const& a) {
    auto it = gArrows.find(a.getOrCreateUniqueID().rawID);
    return it == gArrows.end() ? -1 : it->second.charges;
}

void onHitBlock(Actor& a, BlockPos const& hit, int face) {
    auto it = gArrows.find(a.getOrCreateUniqueID().rawID);
    if (it == gArrows.end() || it->second.hit) return;
    auto& st = it->second;
    st.hit   = true;
    auto& r  = a.getDimensionBlockSource();
    if (st.charges <= 0 || !loaded(r, hit)) return;
    if (isRope(r.getBlock(hit))) {
        st.anchor = hit; // extend that rope downwards, one piece per tick
        log().info("rope: rope arrow hit a rope at {} {} {}", hit.x, hit.y, hit.z);
        return;
    }
    auto target = neighbor(hit, face);
    if (!loaded(r, target) || !replaceable(r, target) || ropes::distanceAt(r, target) >= 7) return;
    auto const* rope = ropes::shapedRope(r, target);
    if (!rope || !set(r, target, *rope)) return;
    st.charges -= 1;
    st.anchor   = target;
    blockSound(r, target, *rope, true, a);
    log().info("rope: rope arrow placed a rope at {} {} {}", target.x, target.y, target.z);
}

void tick() {
    std::vector<std::int64_t> done;
    for (auto& [uid, st] : gArrows) {
        auto* a = fetch(uid);
        if (!a) {
            if (++st.missing > kLifetime * 5) done.push_back(uid);
            continue;
        }
        st.missing = 0;
        if (!st.hit) continue;
        auto& r = a->getDimensionBlockSource();
        if (st.anchor && st.charges > 0) {
            if (auto placed = ropes::addRopeDown(r, *st.anchor)) {
                st.charges -= 1;
                blockSound(r, *placed, r.getBlock(*placed), true, *a);
            } else {
                st.anchor.reset();
            }
            continue;
        }
        st.anchor.reset();
        // picked up by a player walking over it
        auto const& p   = a->getPosition();
        Player*     who = nullptr;
        a->getLevel().forEachPlayer([&](Player& pl) {
            if (pl.getDimensionId() != a->getDimensionId() || !pl.isAlive()) return true;
            auto const q  = pl.getFeetPos();
            float      dx = q.x - p.x, dz = q.z - p.z, dy = q.y - p.y;
            if (dx * dx + dz * dz <= 1.5f * 1.5f && dy > -2.5f && dy < 1.5f) {
                who = &pl;
                return false;
            }
            return true;
        });
        if (who) {
            if (st.pickup && survival(*who)) {
                giveOrDrop(*who, st.charges > 0 ? ropeArrowItem(st.charges) : makeStack("minecraft:arrow", 1));
            }
            a->getLevel().broadcastSoundEvent(
                r,
                SharedTypes::Legacy::LevelSoundEvent::Pop,
                p,
                -1,
                ActorSoundIdentifier(*who),
                false,
                std::nullopt
            );
            log().info("rope: {} picked up a rope arrow ({} charges)", who->getRealName(), st.charges);
            a->remove();
            done.push_back(uid);
            continue;
        }
        if (++st.stuck > kLifetime) {
            a->remove();
            done.push_back(uid);
        }
    }
    for (auto uid : done) gArrows.erase(uid);
}

bool onUseOnRope(Player& player, BlockPos const& pos) {
    auto held = player.getSelectedItem();
    if (!isRopeArrowItem(held)) return false;
    auto uid = player.getOrCreateUniqueID().rawID;
    auto dim = static_cast<int>(player.getDimensionId());
    later(1, [uid, dim, pos] {
        auto* pl = playerById(uid);
        auto* r  = serverRegion(dim);
        if (!pl || !r) return;
        normalize(*pl);
        ItemStack held = pl->getSelectedItem();
        if (!isRopeArrowItem(held)) return;
        int charges = chargesOfItem(held);
        int taken   = 0;
        while (charges + taken < kArrowCharges) {
            auto removed = ropes::removeRopeDown(*r, pos, true);
            if (!removed) break;
            ++taken;
            if (*removed == pos) break;
        }
        if (taken == 0) return;
        setCharges(held, charges + taken);
        pl->setSelectedItem(held);
        pl->refreshInventory();
        log().info("rope: {} wound {} ropes into a rope arrow", pl->getRealName(), taken);
    });
    return true;
}

void normalize(Player& player) {
    auto& inv     = player.getInventory();
    bool  changed = false;
    auto  fix     = [&](ItemStack s) -> std::optional<ItemStack> {
        if (!isRopeArrowItem(s) || s.mAuxValue <= 0 || s.getDamageValue() != 0) return std::nullopt;
        int charges = std::clamp(static_cast<int>(s.mAuxValue), 0, kArrowCharges);
        s.mAuxValue = 0;
        setCharges(s, charges);
        return s;
    };
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (auto s = fix(inv.getItem(i))) {
            inv.setItem(i, *s);
            changed = true;
        }
    }
    if (auto s = fix(player.getOffhandSlot())) {
        player.setOffhandSlot(*s);
        changed = true;
    }
    if (changed) player.refreshInventory();
}

void load() {
    gArrows.clear();
    if (auto j = game::db::load(kKey); j && j->is_array()) {
        for (auto const& e : *j) {
            Tracked t;
            t.charges                                = e.value("charges", 0);
            t.pickup                                 = e.value("pickup", true);
            t.hit                                    = e.value("hit", false);
            gArrows[e.value("uid", std::int64_t{0})] = t;
        }
    }
}

void save() {
    nlohmann::json arr = nlohmann::json::array();
    for (auto const& [uid, t] : gArrows)
        arr.push_back({
            {"uid",     uid      },
            {"charges", t.charges},
            {"pickup",  t.pickup },
            {"hit",     t.hit    }
        });
    if (arr.empty()) game::db::erase(kKey);
    else game::db::save(kKey, arr);
}

void reset() { gArrows.clear(); }

} // namespace cm::feature::rope::arrow
