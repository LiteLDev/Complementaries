// Bellows: pumps while powered, blowing entities away and sucking them back, heating furnaces.

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaTickBlockActorComponent.h"
#include "mc/world/phys/AABB.h"

#include "feature/mechanism/Mechanism.h"
#include "game/Blocks.h"

namespace cm::feature::mechanism::bellows {

namespace {
struct State {
    int  t      = 0;     ///< tick within the current cycle
    bool active = false; ///< a cycle is running
    bool manual = false; ///< the running cycle is a manual press
    int  onTop  = 0;     ///< entities standing on it last tick
};

std::map<core::GlobalPos, State>& states() {
    static std::map<core::GlobalPos, State> m;
    return m;
}

int cycleLength(int power) {
    int p = config().bellows.period;
    return std::max(10, ((p - 10) * (15 - std::clamp(power, 1, 15))) / 14 + 10);
}

AABB blockBox(BlockPos const& a, BlockPos const& b) {
    return AABB{
        Vec3{static_cast<float>(std::min(a.x, b.x)), static_cast<float>(std::min(a.y, b.y)), static_cast<float>(std::min(a.z, b.z))},
        Vec3{static_cast<float>(std::max(a.x, b.x) + 1), static_cast<float>(std::max(a.y, b.y) + 1), static_cast<float>(std::max(a.z, b.z) + 1)}
    };
}

int entitiesOnTop(BlockSource& region, BlockPos const& pos) {
    AABB box{
        Vec3{static_cast<float>(pos.x) + 0.05f, static_cast<float>(pos.y) + 0.95f, static_cast<float>(pos.z) + 0.05f},
        Vec3{static_cast<float>(pos.x) + 0.95f, static_cast<float>(pos.y) + 1.25f, static_cast<float>(pos.z) + 0.95f}
    };
    int n = 0;
    for (Actor* a : entitiesIn(region, box)) {
        if (a && !a->isSpectator() && a->getAABB().min.y >= static_cast<float>(pos.y) + 0.9f) ++n;
    }
    return n;
}

bool heatable(std::string_view id, Block const& block) {
    if (id == "minecraft:lit_furnace" || id == "minecraft:lit_blast_furnace" || id == "minecraft:lit_smoker") return true;
    if (id == "minecraft:campfire" || id == "minecraft:soul_campfire") return !boolStateOf(block, "extinguished");
    return false;
}

void blow(BlockSource& region, BlockPos const& pos, int f, bool blowing, int t) {
    auto const& cfg = config().bellows;
    auto        v   = rotation::dirVec(f);
    Vec3        dir{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    Vec3        mouth = centerOf(pos) + dir * 0.5f;
    float       range = static_cast<float>(cfg.range);

    AABB box = blockBox(offset(pos, f), offset(pos, f, cfg.range));
    for (Actor* a : entitiesIn(region, box)) {
        if (!a || a->isSpectator()) continue;
        if (a->isPlayer() && a->isSneaking()) continue;
        AABB const& bb = a->getAABB();
        Vec3        c  = (bb.min + bb.max) * 0.5f;
        float       d  = (c - mouth).dot(dir);
        if (d < 0.f || d > range) continue;
        float k = cfg.power * 0.05f * (1.f - d / range);
        if (!blowing) k *= -0.5f;
        Vec3 m = a->getPosDelta() + dir * k;
        // cap the speed along the axis
        float along = m.dot(dir);
        if (along > 1.f) m = m - dir * (along - 1.f);
        if (along < -1.f) m = m - dir * (along + 1.f);
        setMotion(*a, m);
        a->setFallDistance(0.f);
    }

    if (blowing) {
        if (t % 2 == 0) {
            std::string effect = "complementaries:bellows_air_" + std::string(rotation::detail::kDirNames[static_cast<std::size_t>(f)]);
            spawnParticle(region, mouth + dir * 0.1f, effect);
        }
        if (cfg.heatBlocks) {
            BlockPos front = offset(pos, f);
            if (region.hasChunksAt(front, 0, false)) {
                Block const& fb = region.getBlock(front);
                if (heatable(fb.getTypeName(), fb)) {
                    if (auto* be = region.getBlockEntity(front)) {
                        if (auto* tc = be->_getTickComponent()) tc->tick(region);
                    }
                }
            }
        }
    }
}
} // namespace

void tick(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto const& cfg = config().bellows;
    auto&       st  = states()[game::globalPos(region, pos)];
    int         f   = facingOf(block);
    if (f < 0) return;
    int power = cfg.enabled ? consumerPower(region, pos) : 0;

    // something stepping on an idle bellows presses it once
    int onTop = entitiesOnTop(region, pos);
    if (cfg.enabled && onTop > st.onTop && power == 0 && !st.active) st.manual = true;
    st.onTop = onTop;

    if (!st.active && (power > 0 || st.manual)) {
        st.active = true;
        st.t      = 0;
    }
    int stage = 0;
    if (st.active) {
        int T = cycleLength(power > 0 ? power : 15);
        if (st.t >= T) st.t = 0;
        double phase = static_cast<double>(st.t) / T;
        stage        = static_cast<int>(std::lround(7.0 * (1.0 - std::cos(2.0 * std::numbers::pi * phase)) / 2.0));
        bool blowing = st.t < T / 2;
        if (st.t == 0) playSound(region, centerOf(pos), "complementaries.block.bellows.blow", 0.4f, 1.f);
        if (st.t == T / 2) playSound(region, centerOf(pos), "complementaries.block.bellows.retract", 0.3f, 1.f);
        blow(region, pos, f, blowing, st.t);
        if (++st.t >= T) {
            st.t      = 0;
            st.manual = false;
            if (power == 0) st.active = false;
        }
    }
    if (stage != intStateOf(block, "complementaries:stage", 0)) {
        if (auto const* b = withStates(block, {{"complementaries:stage", rotation::Value{stage}}})) place(region, pos, *b);
    }
}

void press(BlockSource& region, BlockPos const& pos) {
    if (!config().bellows.enabled || kindAt(region, pos) != Kind::Bellows) return;
    auto& st = states()[game::globalPos(region, pos)];
    if (!st.active) st.manual = true;
}

void reset() { states().clear(); }

} // namespace cm::feature::mechanism::bellows
