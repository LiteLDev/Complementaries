// Drops of the unit's blocks. Their loot tables are empty: the drops depend on block states (number
// of sticks, plant in a hanging pot) that Bedrock loot tables can't read.

#include <set>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/Util.h"
#include "game/Blocks.h"

namespace cm::feature::tweak {

namespace {
std::set<core::GlobalPos>& suppressed() {
    static std::set<core::GlobalPos> s;
    return s;
}
} // namespace

std::vector<ItemStack> dropsOf(Block const& block) {
    std::vector<ItemStack> out;
    auto const&            id = blockName(block);
    if (auto item = stickItemFor(id); !item.empty()) {
        int n = 0;
        for (auto const* axis : {"complementaries:axis_x", "complementaries:axis_y", "complementaries:axis_z"}) {
            n += game::intState(block, axis).value_or(0) != 0 ? 1 : 0;
        }
        if (n > 0) out.push_back(makeItem(item, n));
    } else if (id == kGunpowderBlock) {
        out.push_back(makeItem("minecraft:gunpowder"));
    } else if (id == kHangingPotBlock) {
        out.push_back(makeItem("minecraft:flower_pot"));
        if (auto plant = plantItem(potPlant(block)); !plant.empty()) {
            out.push_back(makeItem(plant));
        }
    }
    return out;
}

void suppressDrops(core::GlobalPos const& pos) { suppressed().insert(pos); }

void clearDropSuppression() { suppressed().clear(); }

void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now) {
    auto const& prevId = blockName(previous);
    if (!prevId.starts_with("complementaries:") || prevId == blockName(now)) return;
    // pushed by a piston (the block comes back elsewhere) or burnt by fire: no drops
    auto const& nowId = blockName(now);
    if (nowId == "minecraft:moving_block" || nowId == "minecraft:fire" || nowId == "minecraft:soul_fire") return;
    auto gp = game::globalPos(region, pos);
    if (suppressed().erase(gp)) return;
    auto drops = dropsOf(previous);
    if (drops.empty()) return;
    defer([gp, drops = std::move(drops)] {
        auto* r = regionOf(gp.dim);
        if (!r) return;
        for (auto const& item : drops) dropItem(*r, game::blockPos(gp), item);
    });
}

} // namespace cm::feature::tweak
