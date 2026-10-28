// Wind vane: wind strength from the weather (redstone + comparator), swaying vane, wind-charge spin.

#include <cmath>
#include <numbers>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/Explosion.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/instrument/Instrument.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::instrument::windvane {

namespace {
constexpr int    kChargedDuration = 40;
constexpr double kTau             = 2.0 * std::numbers::pi;

float trapezoidal(float x, float rise) {
    float fallStart = 1.f - rise;
    if (x < 0.f) return 0.f;
    if (x < rise) {
        float a = (x - rise) / rise;
        return 1.f - a * a;
    }
    if (x < fallStart) return 1.f;
    if (x <= 1.f) {
        float n = 1.f - (x - fallStart) / rise;
        return n * n * (3.f - 2.f * n);
    }
    return 0.f;
}

double initialSway(core::GlobalPos const& p) {
    auto s = [](int v) { return std::sin(std::fmod(0.005 * v, kTau)); };
    return 400.0 * (s(p.x) + s(p.z) + s(p.y));
}

void updateWind(Record& rec, BlockSource& region, BlockPos const& pos, Block const& block) {
    int wind = rec.windCharged > 0 ? 3 : weatherStrength(region);
    if (wind != state(block, "complementaries:wind")) {
        apply(region, pos, block, withStates(block, {{"complementaries:wind", wind}}));
        log().info("instrument: wind vane at {} wind {}", rec.pos.key(), wind);
    }
    rec.signal = wind;
}
} // namespace

int weatherStrength(BlockSource& region) {
    auto& dim = region.getDimension();
    if (!dim.mHasWeather || !dim.mWeather) return 0;
    auto const& w     = *dim.mWeather;
    float       rain  = w.mRainLevel;
    float       storm = w.mLightningLevel;
    if (storm * rain > 0.9f) return 2;
    if (rain > 0.2f) return 1;
    return 0;
}

double yawAt(Record const& rec) {
    auto const& c      = config();
    double      p1     = c.windVanePeriod1 > 0 ? c.windVanePeriod1 : 1;
    double      p2     = c.windVanePeriod2 > 0 ? c.windVanePeriod2 : 1;
    double      yaw    = c.windVaneMaxAngle1 * std::sin(kTau * std::fmod(rec.sway / p1, 1.0))
                + c.windVaneMaxAngle2 * std::sin(kTau * std::fmod(rec.sway / p2, 1.0));
    float charged      = static_cast<float>(rec.windCharged) / kChargedDuration;
    yaw                = (1.f - trapezoidal(1.f - charged, 0.3f)) * yaw;
    yaw               += (1.f - charged * charged) * 360.0 * 5.0 * (rec.windCharged > 0 ? 1.0 : 0.0);
    yaw                = std::fmod(yaw, 360.0);
    return yaw < 0 ? yaw + 360.0 : yaw;
}

void charge(Record& rec, BlockSource& region, BlockPos const& pos) {
    rec.windCharged = kChargedDuration;
    rec.dirty       = true;
    playSound(region, pos, "complementaries.block.wind_vane.spin", 1.f, 1.f);
    updateWind(rec, region, pos, region.getBlock(pos));
    log().info("instrument: wind vane at {} wind charged", rec.pos.key());
}

void tick(Record& rec, BlockSource& region, BlockPos const& pos, Block const& block) {
    if (!rec.initDone) {
        rec.sway     = initialSway(rec.pos);
        rec.initDone = true;
        updateWind(rec, region, pos, block);
        return;
    }
    if (gameTick() % 20 == 0) updateWind(rec, region, pos, block);
    if (rec.windCharged > 0) {
        if (--rec.windCharged == 0) {
            rec.dirty = true;
            updateWind(rec, region, pos, region.getBlock(pos));
        }
    }
    // the animation only matters while someone can see it
    if (!playerNear(region, pos, 64.f)) return;
    int power = state(region.getBlock(pos), "complementaries:wind");
    rec.sway += std::max(1.0, power * config().windVanePowerScaling);
    int bucket = static_cast<int>(std::floor(yawAt(rec) / 5.625 + 0.5)) % 64;
    auto const& current = region.getBlock(pos);
    apply(region, pos, current, withStates(current, {{"complementaries:yaw", bucket / 4}, {"complementaries:yaw_fine", bucket % 4}}));
    if (rec.windCharged == 0) {
        int oneIn = power == 0 ? 1200 : power == 1 ? 380 : 220;
        if (static_cast<int>(random01() * oneIn) == 0) {
            playSound(
                region,
                pos,
                "complementaries.block.wind_vane.spin",
                0.5f + power * 0.1f,
                0.85f + power * 0.12f + static_cast<float>(random01() * 0.1)
            );
        }
    }
}

} // namespace cm::feature::instrument::windvane

namespace cm::feature::instrument::windvane {
namespace {
/// Wind charges (explosions that toggle blocks) charge the wind vanes they reach, as Java's
/// `onExplosionHit` with `canTriggerBlocks`.
LL_TYPE_INSTANCE_HOOK(ExplodeHook, ll::memory::HookPriority::Normal, Explosion, &Explosion::explode, bool, IRandom& random) {
    bool result = origin(random);
    try {
        BlockSource& region = this->mRegion;
        if (!this->mCanToggleBlocks || region.getLevel().isClientSide() || !config().windVane) return result;
        Vec3 const c   = this->mPos;
        float      rad = this->mRadius + 1.f;
        int        dim = static_cast<int>(region.getDimensionId());
        forEach([&](Record& rec) {
            if (rec.kind != Kind::WindVane || rec.pos.dim != dim) return;
            float dx = rec.pos.x + 0.5f - c.x, dy = rec.pos.y + 0.5f - c.y, dz = rec.pos.z + 0.5f - c.z;
            if (dx * dx + dy * dy + dz * dz > rad * rad) return;
            auto pos = game::blockPos(rec.pos);
            if (!region.hasChunksAt(pos, 0, false) || kindOf(region.getBlock(pos)) != Kind::WindVane) return;
            charge(rec, region, pos);
        });
    } catch (...) {}
    return result;
}
} // namespace

void installHooks() { ExplodeHook::hook(); }
void uninstallHooks() { ExplodeHook::unhook(); }

} // namespace cm::feature::instrument::windvane
