// Spring launcher (piston_launcher): extends on redstone, launches entities, absorbs long falls.

#include <cmath>
#include <map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/mechanism/Mechanism.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::mechanism::launcher {

namespace {
enum class Phase { Idle, Extending, Retracting };
struct State {
    Phase phase = Phase::Idle;
    int   timer = 0;
};
constexpr int kStepTicks = 2; ///< ticks the arm stays before the head / air replaces it

std::map<core::GlobalPos, State>& states() {
    static std::map<core::GlobalPos, State> m;
    return m;
}

int launchEntities(BlockSource& region, BlockPos const& pos, int facing) {
    BlockPos front = offset(pos, facing);
    auto     v     = rotation::dirVec(facing);
    // the front block, grown a little so that things resting on the base face are included
    constexpr float g = 0.1f;
    AABB            box{
        Vec3{static_cast<float>(front.x) - g, static_cast<float>(front.y) - g, static_cast<float>(front.z) - g},
        Vec3{static_cast<float>(front.x) + 1 + g, static_cast<float>(front.y) + 1 + g, static_cast<float>(front.z) + 1 + g}
    };
    float vel = config().springLauncher.launcherVelocity;
    int   n   = 0;
    for (Actor* a : entitiesIn(region, box)) {
        if (!a || a->isSpectator()) continue;
        Vec3 m = a->getPosDelta();
        if (v.x) m.x = vel * static_cast<float>(v.x);
        if (v.y) m.y = vel * static_cast<float>(v.y);
        if (v.z) m.z = vel * static_cast<float>(v.z);
        setMotion(*a, m);
        a->setFallDistance(0.f);
        ++n;
    }
    return n;
}
} // namespace

bool fire(BlockSource& region, BlockPos const& pos) {
    Block const& base = region.getBlock(pos);
    if (kindOf(base) != Kind::Launcher || boolStateOf(base, "complementaries:extended")) return false;
    int f = facingOf(base);
    if (f < 0) return false;
    BlockPos front = offset(pos, f);
    if (!game::replaceable(region, front)) return false;
    auto const* extended = withStates(base, {{"complementaries:extended", rotation::Value{true}}});
    auto const* arm      = blockFrom(ids::kArm, facingStates(Kind::Arm, f));
    if (!extended || !arm) return false;
    place(region, pos, *extended);
    place(region, front, *arm);
    states()[game::globalPos(region, pos)] = {Phase::Extending, kStepTicks};
    int n = launchEntities(region, pos, f);
    playSound(region, centerOf(pos), "complementaries.block.piston_launcher.extend", 0.5f, 0.7f);
    if (n > 0) playSound(region, centerOf(front), "complementaries.block.piston_launcher.launch", 0.8f, 1.f);
    log().info("mechanism: launcher at {} {} {} launched {} entities", pos.x, pos.y, pos.z, n);
    return true;
}

void tick(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto&    st  = states()[game::globalPos(region, pos)];
    bool     ext = boolStateOf(block, "complementaries:extended");
    int      f   = facingOf(block);
    if (f < 0) return;
    BlockPos front = offset(pos, f);

    if (st.phase != Phase::Idle) {
        if (--st.timer > 0) return;
        Kind fk = kindAt(region, front);
        if (st.phase == Phase::Extending) {
            if (fk == Kind::Arm) {
                if (auto const* head = blockFrom(ids::kHead, facingStates(Kind::Head, f))) place(region, front, *head);
            }
        } else {
            if (fk == Kind::Arm || fk == Kind::Head) placeAir(region, front);
            Block const& now = region.getBlock(pos);
            if (kindOf(now) == Kind::Launcher && boolStateOf(now, "complementaries:extended")) {
                if (auto const* b = withStates(now, {{"complementaries:extended", rotation::Value{false}}}))
                    place(region, pos, *b);
            }
        }
        st.phase = Phase::Idle;
        return;
    }

    bool powered = config().springLauncher.enabled && consumerPower(region, pos) > 0;
    if (!ext) {
        if (powered) fire(region, pos);
        return;
    }
    Kind fk = kindAt(region, front);
    if (fk != Kind::Head && fk != Kind::Arm) {
        // the head is gone (explosion, command...): retract
        if (auto const* b = withStates(block, {{"complementaries:extended", rotation::Value{false}}}))
            place(region, pos, *b);
        return;
    }
    if (!powered) {
        if (auto const* arm = blockFrom(ids::kArm, facingStates(Kind::Arm, f))) place(region, front, *arm);
        st = {Phase::Retracting, kStepTicks};
        playSound(region, centerOf(pos), "complementaries.block.piston_launcher.retract", 0.5f, 0.65f);
    }
}

void check(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    Kind         k = kindOf(b);
    if (k != Kind::Head && k != Kind::Arm) return;
    BlockPos     behind = offset(pos, rotation::opposite(facingOf(b)));
    Block const& base   = region.getBlock(behind);
    if (kindOf(base) == Kind::Launcher && boolStateOf(base, "complementaries:extended")
        && facingOf(base) == facingOf(b))
        return;
    placeAir(region, pos);
}

bool absorbFall(Actor& actor, float distance) {
    auto const& cfg = config().springLauncher;
    if (!cfg.enabled || distance < cfg.fallHeightRequired || actor.isSneaking()) return false;
    auto&       region = actor.getDimensionBlockSource();
    AABB const& bb     = actor.getAABB();
    BlockPos    below{
        static_cast<int>(std::floor((bb.min.x + bb.max.x) * 0.5f)),
        static_cast<int>(std::floor(bb.min.y - 0.2f)),
        static_cast<int>(std::floor((bb.min.z + bb.max.z) * 0.5f))
    };
    if (kindAt(region, below) != Kind::Launcher) return false;
    Block const& b = region.getBlock(below);
    if (boolStateOf(b, "complementaries:extended") || facingOf(b) != rotation::kUp) return false;
    actor.setFallDistance(0.f);
    int dim = static_cast<int>(region.getDimensionId());
    defer([dim, below] {
        if (auto* r = regionOf(dim)) fire(*r, below);
    });
    log().info("mechanism: launcher at {} {} {} caught a fall of {:.1f} blocks", below.x, below.y, below.z, distance);
    return true;
}

void playerBroke(BlockSource& region, BlockPos const& base, bool creative) {
    if (kindAt(region, base) != Kind::Launcher) return;
    if (!creative) dropItem(region, base, ids::kLauncher);
    placeAir(region, base);
}

void reset() { states().clear(); }

} // namespace cm::feature::mechanism::launcher
