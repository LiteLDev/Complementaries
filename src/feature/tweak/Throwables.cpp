// Throwable bricks and slimeballs (docs/spec/tweak.md §2.1, §2.2).

#include <cmath>
#include <unordered_map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/HitResultType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "mod/Log.h"

namespace cm::feature::tweak {

namespace {
constexpr int   kMaxBounces    = 2;
constexpr int   kSlownessId    = 2; // MobEffectIds::MovementSlowdown
constexpr float kBounceDamping = 0.5f;

/// Bounces left per thrown slimeball (by unique id).
std::unordered_map<int64, int>& bounces() {
    static std::unordered_map<int64, int> m;
    return m;
}

bool breakableByBrick(std::string const& id) {
    return id == "minecraft:glass" || id == "minecraft:glass_pane" || id.ends_with("_stained_glass")
        || id.ends_with("_stained_glass_pane") || id == "minecraft:flower_pot" || id == "minecraft:decorated_pot"
        || id == kHangingPotBlock;
}

std::unordered_map<int64, long long>& lastThrow() {
    static std::unordered_map<int64, long long> m;
    return m;
}

bool throwEnabled(std::string const& itemId) {
    if (itemId == "minecraft:brick" || itemId == "minecraft:netherbrick") return config().throwableBricks;
    if (itemId == "minecraft:slime_ball") return config().throwableSlimeballs;
    return false;
}

Actor* spawnThrown(Player& player, char const* entity, std::string const& initEvent) {
    auto&                     region = player.getDimensionBlockSource();
    ActorDefinitionIdentifier id("complementaries", std::string(entity).substr(16), initEvent);
    Vec3                      eye = player.getHeadPos();
    return player.getLevel().getSpawner().spawnProjectile(region, id, &player, eye, player.getViewVector(1.0f));
}
} // namespace

void throwablesClear() {
    bounces().clear();
    lastThrow().clear();
}

bool throwUse(Player& player, std::string const& itemId) {
    if (!throwEnabled(itemId)) return false;
    bool brick = itemId != "minecraft:slime_ball";
    bool slime = !brick;
    // a block click and an air use can both arrive for one right click: one throw per 4 ticks
    auto& last = lastThrow()[player.getOrCreateUniqueID().rawID];
    if (last != 0 && currentTick() - last < 4) return true;
    last = currentTick();
    Actor* thrown = brick ? spawnThrown(player, kBrickEntity, itemId == "minecraft:netherbrick" ? "complementaries:as_nether" : "")
                          : spawnThrown(player, kSlimeballEntity, "");
    if (!thrown) return false;
    if (slime) bounces()[thrown->getOrCreateUniqueID().rawID] = kMaxBounces;
    playSound(player.getDimensionBlockSource(), Sound::Throw, player.getPosition());
    consumeSelected(player);
    log().info("tweak: {} threw {}", player.getRealName(), itemId);
    return true;
}

bool throwUseOn(UseOn const& use) {
    if (!throwEnabled(use.itemId) || (isInteractive(use.block) && !use.player.isSneaking())) return false;
    auto playerId = use.player.getOrCreateUniqueID();
    auto item     = use.itemId;
    defer([=] {
        auto* player = findPlayer(playerId);
        if (player && itemId(player->getSelectedItem()) == item) throwUse(*player, item);
    });
    return true;
}

bool projectileHit(Actor& projectile, HitResult const& hit) {
    auto const& type = projectile.getTypeName();
    bool        brick = type == kBrickEntity;
    bool        slime = type == kSlimeballEntity;
    if (!brick && !slime) return false;
    auto& region = projectile.getDimensionBlockSource();
    log().debug("tweak: {} hit type {} at {} {} {}", type, static_cast<int>(hit.mType), hit.mBlock.x, hit.mBlock.y, hit.mBlock.z);
    int   dim    = static_cast<int>(region.getDimensionId());

    if (hit.mType == HitResultType::Entity) {
        if (slime) {
            if (auto* target = hit.getEntity()) {
                MobEffectInstance effect(kSlownessId, EffectDuration{config().slimedDurationTicks});
                effect.mAmplifier = 1;
                target->addEffect(effect);
            }
            playSound(region, Sound::SquishBig, projectile.getPosition());
            bounces().erase(projectile.getOrCreateUniqueID().rawID);
        }
        return false; // vanilla: impact damage, removal
    }
    if (hit.mType != HitResultType::Tile) return false;

    BlockPos pos = hit.mBlock;
    if (brick) {
        auto const& id = blockName(region.getBlock(pos));
        if (breakableByBrick(id)) {
            defer([dim, pos] {
                auto* r = regionOf(dim);
                if (!r || !breakableByBrick(blockName(r->getBlock(pos)))) return;
                BlockChangeContext ctx{};
                r->getLevel().destroyBlock(*r, pos, true, ctx);
                log().info("tweak: a thrown brick broke {} at {} {} {}", "glass", pos.x, pos.y, pos.z);
            });
        }
        playBlockSound(region, Sound::Hit, pos, region.getBlock(pos));
        return false;
    }

    // slimeball: bounce off the block a few times, then splat
    auto key  = projectile.getOrCreateUniqueID().rawID;
    int  left = bounces().contains(key) ? bounces()[key] : 0;
    bounces().erase(key);
    playSound(region, Sound::SquishBig, hit.mPos);
    if (left <= 0) return false;
    Vec3 v = projectile.getPosDelta();
    switch (hit.mFacing) {
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
    v = v * kBounceDamping;
    if (v.length() < 0.05f) return false;
    Vec3 from = hit.mPos + v * (0.2f / v.length());
    defer([dim, from, v, left] {
        auto* r = regionOf(dim);
        if (!r) return;
        ActorDefinitionIdentifier id("complementaries", "throwable_slimeball", "");
        Actor* next = r->getLevel().getSpawner().spawnProjectile(*r, id, nullptr, from, v);
        if (!next) return;
        const_cast<Vec3&>(next->getPosDelta()) = v;
        bounces()[next->getOrCreateUniqueID().rawID] = left - 1;
    });
    return false;
}

} // namespace cm::feature::tweak
