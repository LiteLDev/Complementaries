// Slingshot: charge like a bow, release to fire an item from the inventory. The projectile is a
// real item entity (every client renders the actual item, like Supplementaries' projectile
// renderer) that can't be picked up while flying; this file moves it and resolves its impact.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/tool/Tool.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::tool::slingshot {

namespace {

constexpr float kGravity = 0.04f; ///< item entity gravity per tick
constexpr int   kMaxAge  = 20 * 30;
/// Pickup delay of a flying projectile, renewed every tick: an untracked one (world closed in
/// flight) becomes an ordinary item soon after.
constexpr int kFlightPickupDelay = 40;

struct Projectile {
    std::int64_t actor = 0;
    std::int64_t owner = 0;
    ItemStack    ammo; ///< the item without the in-flight marker
    Vec3         lastPos;
    Vec3         stasisVel;
    int          age       = 0;
    int          loyalty   = 0;
    int          stasis    = 0;
    bool         falling   = false; ///< stasis projectile slowed down: normal gravity again
    bool         returning = false;
    int          returnAge = 0;
    bool         done      = false;
};

std::vector<Projectile>& projectiles() {
    static std::vector<Projectile> v;
    return v;
}

int gCounter = 0;

bool blacklisted(ItemStack const& item) {
    auto        id   = itemId(item);
    auto const& list = config().slingshot.ammoBlacklist;
    return std::find(list.begin(), list.end(), id) != list.end();
}

bool validAmmo(ItemStack const& item) {
    return !item.isNull() && item.mCount > 0 && !isItem(item, ids::Slingshot) && !blacklisted(item);
}

/// Takes one ammo item from the player (off hand first, then the inventory). Empty when none.
ItemStack takeAmmo(Player& player) {
    bool creative = isCreative(player);
    if (auto const& off = player.getOffhandSlot(); validAmmo(off)) {
        ItemStack one = off;
        one.set(1);
        if (!creative) {
            ItemStack rest = off;
            if (rest.mCount <= 1) rest = ItemStack::EMPTY_ITEM();
            else rest.remove(1);
            player.setOffhandSlot(rest);
        }
        return one;
    }
    auto& inv      = player.getInventory();
    int   selected = player.getSelectedItemSlot();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (i == selected) continue;
        auto const& it = inv.getItem(i);
        if (!validAmmo(it)) continue;
        ItemStack one = it;
        one.set(1);
        if (!creative) inv.removeItem(i, 1);
        return one;
    }
    return ItemStack::EMPTY_ITEM();
}

Vec3 rotateY(Vec3 const& v, float degrees) {
    float r = degrees * std::numbers::pi_v<float> / 180.0f;
    float c = std::cos(r), s = std::sin(r);
    return Vec3{v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}

/// Segment a->b against a box: the entry fraction in [0, 1], or nullopt.
std::optional<float> segmentHits(Vec3 const& a, Vec3 const& b, AABB const& box) {
    float       tmin = 0.0f, tmax = 1.0f;
    float const from[3] = {a.x, a.y, a.z};
    float const to[3]   = {b.x, b.y, b.z};
    float const lo[3]   = {box.min.x, box.min.y, box.min.z};
    float const hi[3]   = {box.max.x, box.max.y, box.max.z};
    for (int i = 0; i < 3; ++i) {
        float d = to[i] - from[i];
        if (std::abs(d) < 1e-7f) {
            if (from[i] < lo[i] || from[i] > hi[i]) return std::nullopt;
            continue;
        }
        float t1 = (lo[i] - from[i]) / d, t2 = (hi[i] - from[i]) / d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return std::nullopt;
    }
    return tmin;
}

void dropAt(BlockSource& region, ItemStack const& item, Vec3 const& pos) {
    auto* l = level();
    if (!l) return;
    l->getSpawner().spawnItem(region, item, nullptr, pos, 0);
}

void giveOrDrop(Player& player, ItemStack const& item) {
    ItemStack copy = item;
    if (!player.add(copy) || copy.mCount > 0) {
        if (copy.mCount > 0) dropAt(player.getDimensionBlockSource(), copy, player.getPosition());
    }
    player.sendInventory(false);
}

void finish(Projectile& p, Actor& a) {
    p.done = true;
    a.remove();
}

void startReturn(Projectile& p, Actor& a) {
    p.returning = true;
    p.returnAge = 0;
    playSound(static_cast<int>(a.getDimensionId()), a.getPosition(), "item.trident.return", 0.8f, 1.2f);
}

/// Tries to place the projectile's block on `face` of `hit`.
bool tryPlace(Projectile& p, Actor& a, BlockSource& region, BlockPos const& hit, int face) {
    if (!config().slingshot.placeBlocks) return false;
    // only plain block items: no multi-block structures, nothing carrying block-entity data
    Block const* block = p.ammo.mBlock;
    if (!block || block->isAir() || p.ammo.mUserData) return false;
    auto id = itemId(p.ammo);
    for (std::string_view part :
         {"bed",
          "door",
          "tall_grass",
          "large_fern",
          "sunflower",
          "lilac",
          "rose_bush",
          "peony",
          "pitcher_plant",
          "dripleaf"}) {
        if (id.find(part) != std::string::npos && id.find("trapdoor") == std::string::npos) return false;
    }
    auto     off = faceOffset(face);
    BlockPos target{hit.x + off.x, hit.y + off.y, hit.z + off.z};
    if (!game::replaceable(region, target)) return false;
    if (!block->getBlockType().mayPlace(region, target)) return false;
    if (!game::setBlock(region, target, *block)) return false;
    playSound(static_cast<int>(region.getDimensionId()), center(target), "dig.stone", 1.0f, 0.8f);
    log().info("slingshot placed {} at {} {} {}", itemId(p.ammo), target.x, target.y, target.z);
    (void)a;
    return true;
}

void hitBlock(Projectile& p, Actor& a, BlockSource& region, BlockPos const& hit, int face, Vec3 const& hitPos) {
    auto id  = itemId(p.ammo);
    int  dim = static_cast<int>(region.getDimensionId());
    if (id == "minecraft:ender_pearl") {
        if (auto* owner = playerById(p.owner); owner && static_cast<int>(owner->getDimensionId()) == dim) {
            auto off = faceOffset(face);
            Vec3 to{hitPos.x + off.x * 0.5f, hitPos.y + std::max(0, off.y) * 0.1f, hitPos.z + off.z * 0.5f};
            owner->teleport(to, owner->getDimensionId());
            owner->hurtByCause(5.0f, SharedTypes::Legacy::ActorDamageCause::Fall);
            particle(dim, to, "minecraft:huge_explosion_lab_misc_emitter");
            playSound(dim, to, "mob.endermen.portal");
        }
        finish(p, a);
        return;
    }
    if (id == "minecraft:egg") {
        auto*                   l = level();
        static std::minstd_rand rng{std::random_device{}()};
        if (l && rng() % 8 == 0) {
            l->getSpawner().spawnMob(
                region,
                ActorDefinitionIdentifier("minecraft:chicken<minecraft:entity_born>"),
                nullptr,
                hitPos,
                false,
                true,
                false
            );
        }
        particle(dim, hitPos, "minecraft:egg_destroy_emitter");
        finish(p, a);
        return;
    }
    if (p.loyalty > 0) {
        startReturn(p, a);
        return;
    }
    if (tryPlace(p, a, region, hit, face)) {
        finish(p, a);
        return;
    }
    auto off = faceOffset(face);
    dropAt(
        region,
        p.ammo,
        Vec3{hitPos.x + off.x * 0.2f, hitPos.y + std::max(0, off.y) * 0.1f, hitPos.z + off.z * 0.2f}
    );
    finish(p, a);
}

void hitEntity(Projectile& p, Actor& a, Actor& target, Vec3 const& dir) {
    auto* owner = playerById(p.owner);
    target.hurtByCause(
        1.0f,
        SharedTypes::Legacy::ActorDamageCause::Projectile,
        owner ? optional_ref<Actor>(*owner) : std::nullopt
    );
    if (!target.isType(ActorType::ItemEntity)) {
        Vec3 push = normalized(Vec3{dir.x, 0, dir.z});
        auto v    = target.getPosDelta();
        setVelocity(target, Vec3{v.x + push.x * 0.4f, v.y + 0.2f, v.z + push.z * 0.4f});
    }
    log().info("slingshot hit {}", target.getTypeName());
    if (p.loyalty > 0) {
        startReturn(p, a);
        return;
    }
    dropAt(a.getDimensionBlockSource(), p.ammo, a.getPosition());
    finish(p, a);
}

void tickReturn(Projectile& p, Actor& a) {
    ++p.returnAge;
    auto* owner = playerById(p.owner);
    if (!owner || owner->getDimensionId() != a.getDimensionId()) {
        dropAt(a.getDimensionBlockSource(), p.ammo, a.getPosition());
        finish(p, a);
        return;
    }
    Vec3  to   = eyePos(*owner) - Vec3{0, 0.4f, 0} - a.getPosition();
    float dist = length(to);
    if (dist < 1.5f || p.returnAge > 20 * 8) {
        giveOrDrop(*owner, p.ammo);
        playSound(static_cast<int>(owner->getDimensionId()), owner->getPosition(), "random.pop", 0.4f, 1.5f);
        log().info("slingshot projectile returned to {}", owner->getRealName());
        finish(p, a);
        return;
    }
    float speed = std::min(dist, 0.35f + 0.15f * static_cast<float>(p.loyalty));
    Vec3  v     = normalized(to) * speed;
    setVelocity(a, Vec3{v.x, v.y + kGravity, v.z});
}

} // namespace

int chargeTicks(ItemStackBase const& sling) {
    int base = std::max(1, config().slingshot.chargeTime);
    int qc   = enchantLevel(sling, static_cast<int>(Enchant::Type::QuickCharge));
    return std::max(5, base - base * qc / 4);
}

float power(int usedTicks, ItemStackBase const& sling) {
    float f = std::clamp(static_cast<float>(usedTicks) / static_cast<float>(chargeTicks(sling)), 0.0f, 1.0f);
    // the bow curve: fast at first, slower towards the full draw
    return std::min(1.0f, (f * f + f * 2.0f) / 3.0f);
}

bool launch(Player& owner, ItemStack const& ammo, Vec3 const& from, Vec3 const& velocity, int loyalty, int stasis) {
    auto* l = level();
    if (!l || ammo.isNull()) return false;
    ItemStack flying = ammo;
    flying.set(1);
    // a unique marker keeps flying projectiles from merging with each other or with drops
    setIntData(flying, "cm_sling", ++gCounter);
    auto* ia = l->getSpawner().spawnItem(owner.getDimensionBlockSource(), flying, &owner, from, 0);
    if (!ia) return false;
    ia->mPickupDelay = kFlightPickupDelay;
    setVelocity(*ia, velocity);
    Projectile p;
    p.actor = ia->getOrCreateUniqueID().rawID;
    p.owner = owner.getOrCreateUniqueID().rawID;
    p.ammo  = ammo;
    p.ammo.set(1);
    p.lastPos   = from;
    p.stasisVel = velocity;
    p.loyalty   = loyalty;
    p.stasis    = stasis;
    projectiles().push_back(std::move(p));
    return true;
}

int release(Player& player, ItemStack const& sling, int usedTicks) {
    if (!config().slingshot.enabled) return 0;
    float pw = power(usedTicks, sling);
    if (pw < 0.1f) return 0;
    int                multishot = enchantLevel(sling, static_cast<int>(Enchant::Type::Multishot));
    int                loyalty   = enchantLevel(sling, static_cast<int>(Enchant::Type::Loyalty));
    int                stasis    = stasisLevel(sling);
    Vec3               dir       = lookVector(player);
    Vec3               from      = eyePos(player) - Vec3{0, 0.15f, 0} + dir * 0.3f;
    float              speed     = 1.5f * pw * config().slingshot.rangeMultiplier;
    std::vector<float> angles    = multishot > 0 ? std::vector<float>{0.0f, -10.0f, 10.0f} : std::vector<float>{0.0f};
    int                fired     = 0;
    for (float angle : angles) {
        ItemStack ammo = takeAmmo(player);
        if (ammo.isNull()) break;
        if (launch(player, ammo, from, rotateY(dir, angle) * speed, loyalty, stasis)) ++fired;
    }
    if (fired == 0) return 0;
    player.sendInventory(false);
    playSound(static_cast<int>(player.getDimensionId()), eyePos(player), "random.bow", 0.8f, 1.6f - 0.4f * pw);
    log().info("slingshot fired {} projectile(s) power {:.2f} by {}", fired, pw, player.getRealName());
    // durability: after the vanilla release has finished with the in-use copy of the item
    std::int64_t id = player.getOrCreateUniqueID().rawID;
    nextTick([id] {
        if (auto* p = playerById(id); p && isItem(p->getSelectedItem(), ids::Slingshot)) damageMainhand(*p, 1);
    });
    return fired;
}

void tick() {
    auto& list = projectiles();
    if (list.empty()) return;
    for (auto& p : list) {
        if (p.done) continue;
        auto* a = actorById(p.actor);
        if (!a || !a->isAlive()) {
            p.done = true;
            continue;
        }
        ++p.age;
        static_cast<ItemActor*>(a)->mPickupDelay = kFlightPickupDelay;
        auto& region                             = a->getDimensionBlockSource();
        if (p.returning) {
            tickReturn(p, *a);
            continue;
        }
        if (p.stasis > 0 && !p.falling) {
            p.stasisVel = p.stasisVel * config().slingshot.stasisDeceleration;
            if (length(p.stasisVel) < 0.05f) p.falling = true;
            else setVelocity(*a, Vec3{p.stasisVel.x, p.stasisVel.y + kGravity, p.stasisVel.z});
        }
        Vec3 pos  = a->getPosition();
        Vec3 vel  = a->getPosDelta();
        Vec3 next = pos + vel;

        // the closest of: a block on the path, a living entity on the path
        float     bestT = 2.0f;
        HitResult blockHit;
        bool      hasBlock = false;
        blockHit           = region.clip(
            p.lastPos,
            next,
            false,
            ShapeType::Outline,
            64,
            false,
            false,
            nullptr,
            [](BlockSource const&, Block const&, bool) { return true; },
            false
        );
        Vec3  seg    = next - p.lastPos;
        float segLen = length(seg);
        if (blockHit.mType == HitResultType::Tile && segLen > 1e-5f) {
            hasBlock = true;
            bestT    = length(blockHit.mPos - p.lastPos) / segLen;
        }
        Actor* target = nullptr;
        AABB   sweep{
            Vec3{
                 std::min(p.lastPos.x, next.x) - 1.0f,
                 std::min(p.lastPos.y, next.y) - 1.0f,
                 std::min(p.lastPos.z, next.z) - 1.0f
            },
            Vec3{
                 std::max(p.lastPos.x, next.x) + 1.0f,
                 std::max(p.lastPos.y, next.y) + 1.0f,
                 std::max(p.lastPos.z, next.z) + 1.0f
            }
        };
        for (Actor* other : region.fetchEntities(a, sweep, true, false)) {
            if (other->isType(ActorType::ItemEntity) || !other->hasCategory(ActorCategory::Mob) || !other->isAlive())
                continue;
            if (p.age < 6 && other->getOrCreateUniqueID().rawID == p.owner) continue;
            AABB box = other->getAABB();
            box.min  = box.min - Vec3{0.15f, 0.15f, 0.15f};
            box.max  = box.max + Vec3{0.15f, 0.15f, 0.15f};
            if (auto t = segmentHits(p.lastPos, next, box); t && *t < bestT) {
                bestT  = *t;
                target = other;
            }
        }
        if (target) {
            hitEntity(p, *a, *target, seg);
        } else if (hasBlock) {
            hitBlock(p, *a, region, blockHit.mBlock, blockHit.mFacing, blockHit.mPos);
        } else if (p.age > 2 && (a->isOnGround() || (length(vel) < 0.01f && (p.stasis == 0 || p.falling)))) {
            BlockPos below = game::blockPosOf(pos - Vec3{0, 0.1f, 0});
            hitBlock(p, *a, region, below, 1, pos);
        } else if (p.age > kMaxAge) {
            dropAt(region, p.ammo, pos);
            finish(p, *a);
        }
        if (!p.done) p.lastPos = pos;
    }
    std::erase_if(list, [](Projectile const& p) { return p.done; });
}

void clear() { projectiles().clear(); }

int activeProjectiles() { return static_cast<int>(projectiles().size()); }

} // namespace cm::feature::tool::slingshot
