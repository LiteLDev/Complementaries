// U15 `cannon`: what a cannon shoots, cannonballs breaking blocks, bombs (bomb, blue bomb with its
// baby bombs, spiky bomb), bouncy blocks and TNT exploding on impact.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/cannon/Cannon.h"
#include "feature/cannon/CannonConfig.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace cm::feature::cannon {

namespace {

CannonConfig const& cfg() { return config::get<CannonConfig>(); }

math::V3 toV3(Vec3 const& v) { return {v.x, v.y, v.z}; }
Vec3     toVec(math::V3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }
BlockPos blockAt(math::V3 p) {
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

/// A projectile of this unit hit something (handled on the next tick, outside the projectile's
/// own update).
struct PendingHit {
    std::string type;
    int         dim = 0;
    Vec3        pos;
    Vec3        vel;
    int64_t     self  = 0;
    int64_t     owner = 0;
    bool        block = false;
    BlockPos    blockPos;
    uchar       face   = 1;
    int64_t     entity = 0;
    int         delay  = 0; ///< ticks left (bomb fuse)
};
std::vector<PendingHit> gHits;

/// Cannonballs that keep flying after a hit are re-spawned; this remembers which entities a
/// ball (and its successors) already damaged.
std::map<int64_t, std::set<int64_t>> gBallHits;

/// TNT shot from a cannon: explodes when it lands (`explodeTnt`).
struct ShotTnt {
    int64_t id  = 0;
    int     dim = 0;
    int     age = 0;
    float   lastHorizontal = 0.0f;
};
std::vector<ShotTnt> gTnt;

Actor* byId(int64_t id) {
    auto level = ll::service::getLevel();
    return (level && id) ? level->fetchEntity(ActorUniqueID{id}, false) : nullptr;
}

bool isBomb(std::string_view t) { return t == kBomb || t == kBombBlue || t == kBombBlueBaby || t == kBombSpiky; }
bool isOurs(std::string_view t) { return t == kCannonball || isBomb(t); }

/// Blocks bombs bounce off (Supplementaries' `bouncy_blocks` tag).
bool bouncy(std::string const& id) { return id == "minecraft:slime" || id == "minecraft:honey_block"; }

void recordHit(Actor& projectile, HitResult const& res) {
    auto const& type = projectile.getTypeName();
    if (type != kCannonball && !isBomb(type)) return;
    PendingHit h;
    h.type = type;
    h.dim  = static_cast<int>(projectile.getDimensionId());
    h.pos  = (res.mType == HitResultType::Tile || res.mType == HitResultType::Entity) ? res.mPos : projectile.getPosition();
    h.vel  = projectile.getPosDelta();
    h.self = projectile.getOrCreateUniqueID().rawID;
    if (auto* o = projectile.getOwner()) h.owner = o->getOrCreateUniqueID().rawID;
    if (res.mType == HitResultType::Tile) {
        h.block    = true;
        h.blockPos = res.mBlock;
        h.face     = res.mFacing;
    } else if (res.mType == HitResultType::Entity) {
        if (auto* e = res.getEntity()) h.entity = e->getOrCreateUniqueID().rawID;
    }
    if (isBomb(type)) h.delay = std::max(0, cfg().bombFuse);
    log().debug("{} hit: kind {} block {} {} {} at {:.2f} {:.2f} {:.2f} vel {:.2f} {:.2f} {:.2f} (actor at {:.2f} {:.2f} {:.2f})", type, static_cast<int>(res.mType), h.blockPos.x, h.blockPos.y, h.blockPos.z, h.pos.x, h.pos.y, h.pos.z, h.vel.x, h.vel.y, h.vel.z, projectile.getPosition().x, projectile.getPosition().y, projectile.getPosition().z);
    gHits.push_back(std::move(h));
}

/// Shoots one of this unit's projectiles. A projectile does not register a block hit during its
/// first tick, so a solid block inside that first step (a wall in front of the muzzle, the block
/// behind the one a cannonball just broke, a wall next to a bounce) is hit here right away.
/// `hits`: entities a cannonball already damaged.
Actor* fireOurs(BlockSource& region, std::string_view id, Vec3 const& pos, Vec3 const& velocity, int64_t owner, std::set<int64_t> hits = {}) {
    static int64_t syntheticIds = 0;
    math::V3 const a = toV3(pos), b = toV3(pos + velocity);
    double const   len = (b - a).length();
    for (int i = 1, n = std::max(1, static_cast<int>(std::ceil(len / 0.1))); i <= n; ++i) {
        math::V3 p  = a + (b - a) * (static_cast<double>(i) / n);
        BlockPos bp = blockAt(p);
        if (bp.y < region.getMinHeight() || bp.y >= region.getMaxHeight()) break;
        auto const& blk = region.getBlock(bp);
        if (blk.isAir() || !blk.mCachedComponentData->mIsSolid) continue;
        PendingHit h;
        h.type     = id;
        h.dim      = static_cast<int>(region.getDimensionId());
        h.pos      = toVec(a + (b - a) * (static_cast<double>(i - 1) / n));
        h.vel      = velocity;
        h.self     = --syntheticIds; // never a real actor id
        h.owner    = owner;
        h.block    = true;
        h.blockPos = bp;
        math::V3 d  = (b - a).normalized();
        double   ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
        h.face      = ay >= ax && ay >= az ? (d.y > 0 ? 0 : 1) : (az >= ax ? (d.z > 0 ? 2 : 3) : (d.x > 0 ? 4 : 5));
        if (isBomb(id)) h.delay = std::max(0, cfg().bombFuse);
        if (!hits.empty()) gBallHits[h.self] = std::move(hits);
        gHits.push_back(std::move(h));
        return nullptr;
    }
    Actor* actor = shootProjectile(region, id, pos, velocity, owner);
    if (actor && !hits.empty()) gBallHits[actor->getOrCreateUniqueID().rawID] = std::move(hits);
    return actor;
}

void hurt(Actor& target, float damage, int64_t ownerId) {
    Actor* owner = byId(ownerId);
    if (owner && owner != &target) target.hurtByCause(damage, SharedTypes::Legacy::ActorDamageCause::Projectile, *owner);
    else target.hurtByCause(damage, SharedTypes::Legacy::ActorDamageCause::Projectile);
}

void ballBreaks(int dim, Vec3 const& pos) {
    playSound(dim, pos, "random.break", 0.8f, 0.6f);
    particle(dim, pos, "minecraft:basic_smoke_particle");
    log().info("Cannonball broke apart at {:.1f} {:.1f} {:.1f}", pos.x, pos.y, pos.z);
}

void continueBall(BlockSource& region, PendingHit const& h, Vec3 const& from, math::V3 vel, std::set<int64_t> hits) {
    if (vel.length() < 0.25) {
        ballBreaks(h.dim, from);
        return;
    }
    fireOurs(region, kCannonball, from, toVec(vel), h.owner, std::move(hits));
}

void handleCannonball(PendingHit const& h) {
    auto* region = regionOf(h.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return;
    std::set<int64_t> hits;
    if (auto it = gBallHits.find(h.self); it != gBallHits.end()) {
        hits = std::move(it->second);
        gBallHits.erase(it);
    }
    double const k      = std::max(0.001, static_cast<double>(cfg().cannonballPowerScaling));
    math::V3     vel    = toV3(h.vel);
    math::V3     dir    = vel.normalized();
    double       energy = math::energy(k, vel);
    if (h.entity) {
        Actor* target = byId(h.entity);
        if (target && !hits.contains(h.entity)) {
            hits.insert(h.entity);
            hurt(*target, static_cast<float>(energy), h.owner);
            log().info("Cannonball hit {} for {:.1f}", target->getTypeName(), energy);
            energy *= 0.5;
        }
        continueBall(*region, h, h.pos + toVec(dir * 0.5), dir * math::speedFor(k, energy), std::move(hits));
        return;
    }
    if (!h.block) return;
    // break the blocks around the hit point that the ball's energy can pay for, nearest first
    double const radius = cfg().cannonballBreakRadius;
    if (radius > 0.0) {
        math::V3 const        c = toV3(h.pos) + dir * 0.5;
        int const             r = static_cast<int>(std::ceil(radius));
        BlockPos const        cb = blockAt(c);
        std::vector<std::pair<double, BlockPos>> cand;
        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dz = -r; dz <= r; ++dz) {
                    BlockPos bp{cb.x + dx, cb.y + dy, cb.z + dz};
                    math::V3 bc{bp.x + 0.5, bp.y + 0.5, bp.z + 0.5};
                    double   d = (bc - c).length();
                    if (d <= radius + 0.5) cand.emplace_back(d, bp);
                }
            }
        }
        // the block that was hit always comes first
        cand.emplace_back(-1.0, h.blockPos);
        std::sort(cand.begin(), cand.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::set<std::tuple<int, int, int>> done;
        int                                 broken = 0;
        for (auto const& [d, bp] : cand) {
            if (!done.insert({bp.x, bp.y, bp.z}).second) continue;
            if (bp.y < region->getMinHeight() || bp.y >= region->getMaxHeight()) continue;
            auto const& block = region->getBlock(bp);
            if (block.isAir()) continue;
            float hardness = block.getBlockType().getDestroySpeed();
            if (hardness < 0.0f) continue; // unbreakable
            double cost = std::max(0.1, static_cast<double>(hardness));
            if (cost > energy) continue;
            energy -= cost;
            level->destroyBlock(*region, bp, true, BlockChangeContext{});
            ++broken;
        }
        if (broken > 0) {
            playSound(h.dim, h.pos, "complementaries.cannonball.break", 1.0f, 1.0f);
            log().info("Cannonball broke {} block(s)", broken);
            continueBall(*region, h, h.pos, dir * math::speedFor(k, energy), std::move(hits));
            return;
        }
    }
    ballBreaks(h.dim, h.pos);
}

/// Reflects a bomb off a bouncy block: re-spawns it with the mirrored velocity.
bool bounce(PendingHit const& h) {
    if (!h.block) return false;
    auto* region = regionOf(h.dim);
    if (!region || !bouncy(region->getBlock(h.blockPos).getTypeName())) return false;
    math::V3 v = toV3(h.vel);
    switch (h.face) {
    case 0:
    case 1:
        v.y = -v.y;
        break;
    case 2:
    case 3:
        v.z = -v.z;
        break;
    default:
        v.x = -v.x;
        break;
    }
    v = v * 0.8;
    if (v.length() < 0.1) return false;
    fireOurs(*region, h.type, h.pos + toVec(v.normalized() * 0.2), toVec(v), h.owner);
    playSound(h.dim, h.pos, "mob.slime.small", 0.8f, 1.0f);
    return true;
}

void shrapnel(BlockSource& region, Vec3 const& pos, int64_t ownerId) {
    double const range = 7.0;
    auto         dirs  = math::sphereDirections(std::max(0, cfg().spikyShrapnel));
    Vec3 const   r{static_cast<float>(range), static_cast<float>(range), static_cast<float>(range)};
    auto         entities = region.getEntities(AABB{pos - r, pos + r}, 0.0f);
    std::map<Actor*, int> spikes;
    math::V3 const        o = toV3(pos);
    for (auto const& d : dirs) {
        double wall = range; // distance to the first solid block along the spike
        for (double t = 0.25; t < range; t += 0.25) {
            auto const& b = region.getBlock(blockAt(o + d * t));
            if (!b.isAir() && b.mCachedComponentData->mIsSolid) {
                wall = t;
                break;
            }
        }
        Actor* best  = nullptr;
        double bestT = wall;
        for (auto* a : entities) {
            if (!a || !a->hasCategory(ActorCategory::Mob)) continue;
            auto const& bb = a->getAABB();
            double      t  = math::rayBox(o, d, toV3(bb.min), toV3(bb.max));
            if (t >= 0.0 && t < bestT) {
                bestT = t;
                best  = a;
            }
        }
        if (best) ++spikes[best];
    }
    for (auto const& [a, n] : spikes) hurt(*a, cfg().spikyDamage * static_cast<float>(n), ownerId);
    if (!spikes.empty()) log().info("Spiky bomb shrapnel hit {} entities", spikes.size());
}

void setFireAround(BlockSource& region, Vec3 const& pos, float radius, int seconds) {
    if (seconds <= 0) return;
    Vec3 const r{radius, radius, radius};
    for (auto* a : region.getEntities(AABB{pos - r, pos + r}, 0.0f)) {
        if (!a || !a->hasCategory(ActorCategory::Mob) || a->getPosition().distanceTo(pos) > radius) continue;
        a->setOnFire(seconds, true);
    }
}

/// No block stronger than `maxResistance` lies between `from` and the centre of `target`.
bool clearPath(BlockSource& region, math::V3 from, BlockPos const& target, float maxResistance) {
    math::V3 const to{target.x + 0.5, target.y + 0.5, target.z + 0.5};
    double const   len = (to - from).length();
    for (int i = 1, n = std::max(1, static_cast<int>(std::ceil(len / 0.25))); i < n; ++i) {
        BlockPos bp = blockAt(from + (to - from) * (static_cast<double>(i) / n));
        if (bp == target) break;
        auto const& b = region.getBlock(bp);
        if (!b.isAir() && b.getBlockType().getExplosionResistance() > maxResistance) return false;
    }
    return true;
}

/// Explosion following a `break_blocks` mode: "all" breaks like TNT, "none" breaks nothing and
/// "weak" breaks only blocks with a blast resistance up to `maxResistance` inside the radius.
void explodeWith(BlockSource& region, Vec3 const& pos, float radius, std::string const& mode, float maxResistance) {
    auto level = ll::service::getLevel();
    if (!level) return;
    if (mode == "all") {
        level->explode(region, nullptr, pos, radius, false, true, FLT_MAX, false);
        return;
    }
    level->explode(region, nullptr, pos, radius, false, false, FLT_MAX, false);
    if (mode != "weak") return;
    math::V3 const c = toV3(pos);
    int const      r = static_cast<int>(std::ceil(radius));
    BlockPos const cb = blockAt(c);
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dz = -r; dz <= r; ++dz) {
                BlockPos bp{cb.x + dx, cb.y + dy, cb.z + dz};
                if (bp.y < region.getMinHeight() || bp.y >= region.getMaxHeight()) continue;
                if ((math::V3{bp.x + 0.5, bp.y + 0.5, bp.z + 0.5} - c).length() > radius) continue;
                auto const& block = region.getBlock(bp);
                if (block.isAir() || block.getBlockType().getDestroySpeed() < 0.0f) continue;
                if (block.getBlockType().getExplosionResistance() > maxResistance) continue;
                if (!clearPath(region, c, bp, maxResistance)) continue;
                level->destroyBlock(region, bp, true, BlockChangeContext{});
            }
        }
    }
}

} // namespace

void explodeBomb(int dim, std::string_view type, Vec3 const& pos, int64_t owner) {
    auto* region = regionOf(dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return;
    auto const& c = cfg();
    if (type == kBombBlue) {
        explodeWith(*region, pos, c.blueBombExplosionRadius, c.blueBombBreakBlocks, c.bombMaxResistance);
        particle(dim, pos, "complementaries:blue_flame");
        // scatter the baby bombs
        std::uniform_real_distribution<double> a(0.0, 2.0 * math::kPi), up(0.25, 0.55), sp(0.25, 0.45);
        for (int i = 0; i < c.blueBombSplitCount; ++i) {
            double   ang = a(rng());
            math::V3 v{std::cos(ang) * sp(rng()), up(rng()), std::sin(ang) * sp(rng())};
            fireOurs(*region, kBombBlueBaby, pos + Vec3{0, 0.4f, 0} + toVec(v.normalized() * 0.4), toVec(v), owner);
        }
    } else if (type == kBombBlueBaby) {
        level->explode(*region, nullptr, pos, c.blueBombBabyExplosionRadius, false, false, FLT_MAX, false);
        setFireAround(*region, pos, c.blueBombBabyExplosionRadius * 2.0f, c.blueBombBabyFireDuration);
        particle(dim, pos, "complementaries:blue_flame");
    } else if (type == kBombSpiky) {
        level->explode(*region, nullptr, pos, c.bombExplosionRadius, false, false, FLT_MAX, false);
        shrapnel(*region, pos, owner);
        particle(dim, pos, "complementaries:shrapnel");
    } else {
        explodeWith(*region, pos, c.bombExplosionRadius, c.bombBreakBlocks, c.bombMaxResistance);
    }
    log().info("Bomb exploded: {} at {:.1f} {:.1f} {:.1f}", type, pos.x, pos.y, pos.z);
}

Actor* launch(BlockSource& region, ItemStack const& item, Vec3 const& pos, Vec3 const& velocity, int64_t owner) {
    std::string const id = item.getTypeName();
    static std::map<std::string, std::string> const projectiles = {
        {std::string(kCannonball), std::string(kCannonball)},
        {std::string(kBomb), std::string(kBomb)},
        {std::string(kBombBlue), std::string(kBombBlue)},
        {std::string(kBombSpiky), std::string(kBombSpiky)},
        {"minecraft:arrow", "minecraft:arrow"},
        {"minecraft:snowball", "minecraft:snowball"},
        {"minecraft:egg", "minecraft:egg"},
        {"minecraft:ender_pearl", "minecraft:ender_pearl"},
        {"minecraft:fire_charge", "minecraft:small_fireball"},
        {"minecraft:wind_charge", "minecraft:wind_charge_projectile"},
        {"minecraft:splash_potion", "minecraft:splash_potion"},
        {"minecraft:lingering_potion", "minecraft:lingering_potion"},
        {"minecraft:experience_bottle", "minecraft:xp_bottle"},
        {"minecraft:trident", "minecraft:thrown_trident"},
        {"minecraft:firework_rocket", "minecraft:fireworks_rocket"},
    };
    if (id == "minecraft:tnt") {
        Actor* tnt = spawnActor(region, "minecraft:tnt", pos, velocity, 0, "Fuse:80b");
        if (tnt && cfg().explodeTnt) gTnt.push_back({tnt->getOrCreateUniqueID().rawID, static_cast<int>(tnt->getDimensionId()), 0});
        return tnt;
    }
    if (isOurs(id)) return fireOurs(region, id, pos, velocity, owner);
    if (auto it = projectiles.find(id); it != projectiles.end()) {
        std::string extra;
        if (id == "minecraft:splash_potion" || id == "minecraft:lingering_potion") extra = "PotionId:" + std::to_string(item.getAuxValue()) + "s";
        if (!extra.empty()) return spawnActor(region, it->second, pos, velocity, owner, extra);
        return shootProjectile(region, it->second, pos, velocity, owner);
    }
    return dropItem(region, item, pos, velocity);
}

LL_TYPE_INSTANCE_HOOK(
    CannonProjectileHitHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    Actor&           owner,
    HitResult const& res
) {
    if (!owner.isClientSide()) {
        // our projectiles fly through each other (baby bombs start in a cluster) and the barrels
        if (res.mType == HitResultType::Entity && isOurs(owner.getTypeName())) {
            if (auto* e = res.getEntity(); e && (isOurs(e->getTypeName()) || e->getTypeName() == kBarrel)) return;
        }
        // (qualified: an unqualified name would find ProjectileComponent::onHit, i.e. this hook)
        cm::feature::cannon::recordHit(owner, res);
    }
    origin(owner, res);
}

void installProjectiles() { CannonProjectileHitHook::hook(); }

void uninstallProjectiles() {
    CannonProjectileHitHook::unhook();
    clearProjectiles();
}

void clearProjectiles() {
    gHits.clear();
    gBallHits.clear();
    gTnt.clear();
}

void tickProjectiles() {
    if (!gHits.empty()) {
        auto hits = std::move(gHits);
        gHits.clear();
        for (auto& h : hits) {
            if (h.delay > 0) {
                --h.delay;
                gHits.push_back(std::move(h));
                continue;
            }
            if (h.type == kCannonball) handleCannonball(h);
            else if (!bounce(h)) explodeBomb(h.dim, h.type, h.pos, h.owner);
        }
    }
    // TNT shot from a cannon explodes when it lands or hits something
    for (auto it = gTnt.begin(); it != gTnt.end();) {
        Actor* tnt = byId(it->id);
        if (!tnt) {
            it = gTnt.erase(it);
            continue;
        }
        ++it->age;
        Vec3 const v          = tnt->getPosDelta();
        float      horizontal = std::sqrt(v.x * v.x + v.z * v.z);
        bool       stopped    = it->lastHorizontal > 0.1f && horizontal < it->lastHorizontal * 0.3f; // hit a wall
        bool       landed     = it->age > 2 && (tnt->isOnGround() || stopped);
        it->lastHorizontal    = horizontal;
        if (landed) {
            Vec3 pos = tnt->getPosition();
            int  dim = it->dim;
            tnt->remove();
            if (auto* region = regionOf(dim)) {
                if (auto level = ll::service::getLevel()) level->explode(*region, nullptr, pos, 4.0f, false, true, FLT_MAX, false);
                log().info("Cannon TNT exploded on impact");
            }
            it = gTnt.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace cm::feature::cannon
