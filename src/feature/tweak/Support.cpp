// Hanging pots need a block above them and gunpowder trails a solid block below. Custom blocks only
// check their placement filter when placed, so the mod pops them when the support goes away.

#include <set>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/Util.h"
#include "game/Blocks.h"

namespace cm::feature::tweak {

namespace {
std::set<core::GlobalPos>& pending() {
    static std::set<core::GlobalPos> s;
    return s;
}

bool supported(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto const& id = blockName(block);
    if (id == kHangingPotBlock) {
        Block const& above = region.getBlock(offset(pos, 1));
        return !above.isAir() && !game::replaceable(region, offset(pos, 1));
    }
    if (id == kGunpowderBlock) return isSolid(region.getBlock(offset(pos, 0)));
    return true;
}
} // namespace

void supportBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now) {
    if (isSolid(previous) == isSolid(now) && previous.isAir() == now.isAir()) return;
    int dim = static_cast<int>(region.getDimensionId());
    pending().insert(game::globalPos(dim, offset(pos, 0))); // a pot hanging below
    pending().insert(game::globalPos(dim, offset(pos, 1))); // a trail lying on top
}

void supportTick() {
    if (pending().empty()) return;
    auto todo = std::move(pending());
    pending().clear();
    for (auto const& gp : todo) {
        auto* region = regionOf(gp.dim);
        if (!region) continue;
        auto pos = game::blockPos(gp);
        if (!region->hasChunksAt(pos, 1, false)) continue;
        Block const& b = region->getBlock(pos);
        if (supported(*region, pos, b)) continue;
        BlockChangeContext ctx{};
        region->getLevel().destroyBlock(*region, pos, false, ctx); // drops come from onBlockChanged
    }
}

void supportClear() { pending().clear(); }

} // namespace cm::feature::tweak
