// Turn table: rotates the block in front of its top face (and spins entities standing on it).

#include <algorithm>
#include <array>
#include <map>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/mechanism/Mechanism.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::mechanism::turntable {

namespace {
struct State {
    int counter = 0;
};

std::map<core::GlobalPos, State>& states() {
    static std::map<core::GlobalPos, State> m;
    return m;
}

int periodFor(int power) {
    int p = config().turnTable.period;
    return std::max(1, ((p - 1) * (15 - std::clamp(power, 1, 15))) / 14 + 1);
}

constexpr std::array<std::string_view, 6> kNeverRotate{
    "minecraft:piston_arm_collision",
    "minecraft:sticky_piston_arm_collision",
    "minecraft:moving_block",
    "minecraft:bed",
    "minecraft:end_portal_frame",
    "minecraft:portal",
};

bool isChest(std::string_view id) { return id == "minecraft:chest" || id == "minecraft:trapped_chest"; }

/// A block that must stay as it is (two-block structures, pistons out, our launcher parts).
bool locked(BlockSource& region, BlockPos const& pos, Block const& b) {
    std::string_view id = b.getTypeName();
    for (auto n : kNeverRotate)
        if (id == n) return true;
    switch (kindOf(b)) {
    case Kind::Head:
    case Kind::Arm:
        return true;
    case Kind::Launcher:
        return boolStateOf(b, "complementaries:extended");
    default:
        break;
    }
    if (id == "minecraft:piston" || id == "minecraft:sticky_piston") {
        int      d   = intStateOf(b, "facing_direction", -1);
        BlockPos arm = offset(pos, d < 0 ? 0 : d);
        if (d >= 0 && region.hasChunksAt(arm, 0, false)) {
            std::string_view a = region.getBlock(arm).getTypeName();
            if (a == "minecraft:piston_arm_collision" || a == "minecraft:sticky_piston_arm_collision") return true;
        }
    }
    if (isChest(id)) {
        for (int d = 2; d < 6; ++d) {
            BlockPos n = offset(pos, d);
            if (region.hasChunksAt(n, 0, false) && region.getBlock(n).getTypeName() == id) return true;
        }
    }
    return false;
}
} // namespace

std::string rotateFront(BlockSource& region, BlockPos const& pos) {
    Block const& table = region.getBlock(pos);
    if (kindOf(table) != Kind::TurnTable) return {};
    int      axis  = facingOf(table);
    bool     ccw   = boolStateOf(table, "complementaries:inverted");
    BlockPos front = offset(pos, axis);
    if (front.y < region.getMinHeight() || front.y >= region.getMaxHeight()) return {};
    if (!region.hasChunksAt(front, 0, false)) return {};
    Block const& b = region.getBlock(front);
    if (b.isAir() || locked(region, front, b)) return {};

    auto states  = statesOf(b);
    bool changed = false;
    for (auto& [name, value] : states) {
        if (rotation::blocksRotation(name)) return {};
        auto r = rotation::rotateState(name, value, axis, ccw);
        if (r == rotation::Result::Unsupported) return {};
        if (r == rotation::Result::Changed) changed = true;
    }
    if (!changed) return {};
    std::string id = b.getTypeName();
    auto const* nb = blockFrom(id, states);
    if (!nb || nb == &b) return {};
    place(region, front, *nb);
    playSound(region, centerOf(front), "complementaries.block.turn_table.rotate", 0.7f, 1.f);
    return id;
}

bool toggle(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    if (kindOf(b) != Kind::TurnTable) return false;
    bool inv = !boolStateOf(b, "complementaries:inverted");
    if (auto const* nb = withStates(b, {{"complementaries:inverted", rotation::Value{inv}}})) place(region, pos, *nb);
    playSound(region, centerOf(pos), "random.click", 0.3f, inv ? 0.55f : 0.5f);
    states()[game::globalPos(region, pos)].counter = 0;
    log().info("mechanism: turn table at {} {} {} inverted={}", pos.x, pos.y, pos.z, inv);
    return inv;
}

void tick(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto const& cfg      = config().turnTable;
    auto&       st       = states()[game::globalPos(region, pos)];
    int         power    = cfg.enabled ? consumerPower(region, pos) : 0;
    bool        rotating = power > 0;
    Block const* cur     = &block;
    if (boolStateOf(block, "complementaries:rotating") != rotating) {
        if (auto const* nb = withStates(block, {{"complementaries:rotating", rotation::Value{rotating}}})) {
            place(region, pos, *nb);
            cur = nb;
        }
    }
    if (!rotating) {
        st.counter = 0;
        return;
    }
    int  period = periodFor(power);
    int  f      = facingOf(*cur);
    bool ccw    = boolStateOf(*cur, "complementaries:inverted");
    if (++st.counter >= period) {
        st.counter = 0;
        auto id    = rotateFront(region, pos);
        if (!id.empty()) log().debug("mechanism: turn table at {} {} {} rotated {}", pos.x, pos.y, pos.z, id);
    }
    if (f == rotation::kUp && cfg.rotateEntities) {
        AABB box{
            Vec3{static_cast<float>(pos.x), static_cast<float>(pos.y) + 0.95f, static_cast<float>(pos.z)},
            Vec3{static_cast<float>(pos.x) + 1.f, static_cast<float>(pos.y) + 1.5f, static_cast<float>(pos.z) + 1.f}
        };
        float step = 90.f / static_cast<float>(period) * (ccw ? -1.f : 1.f);
        for (Actor* a : entitiesIn(region, box)) {
            if (!a || a->isSpectator()) continue;
            if (a->getAABB().min.y < static_cast<float>(pos.y) + 0.9f) continue;
            turnEntity(*a, step);
        }
    }
}

void reset() { states().clear(); }

} // namespace cm::feature::mechanism::turntable
