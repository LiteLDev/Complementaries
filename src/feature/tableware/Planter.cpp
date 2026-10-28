// Planter: crops, saplings and flowers can be placed on it and survive there; crops grow as on
// moist farmland.

#include "ll/api/memory/Hook.h"

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/CropBlock.h"
#include "mc/world/level/block/CropUtils.h"
#include "mc/world/level/block/FlowerBlock.h"
#include "mc/world/level/block/FoliageBlock.h"

#include "feature/tableware/Tableware.h"

namespace cm::feature::tableware::planter {

namespace {
bool planterAt(BlockSource const& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    return is(region.getBlock(pos), kPlanter);
}

/// `pos` is the support block, or the (still empty) plant position above a planter.
bool supportsPlacement(BlockSource const& region, BlockPos const& pos) {
    if (planterAt(region, pos)) return true;
    return region.getBlock(pos).isAir() && planterAt(region, BlockPos{pos.x, pos.y - 1, pos.z});
}

/// `pos` is the plant.
bool standsOnPlanter(BlockSource const& region, BlockPos const& pos) {
    return planterAt(region, BlockPos{pos.x, pos.y - 1, pos.z});
}

LL_TYPE_INSTANCE_HOOK(CropMayPlaceOnHook, ll::memory::HookPriority::Normal, CropBlock, &CropBlock::$mayPlaceOn, bool, BlockSource& region, BlockPos const& pos) {
    return supportsPlacement(region, pos) || origin(region, pos);
}
LL_TYPE_INSTANCE_HOOK(CropCanSurviveHook, ll::memory::HookPriority::Normal, CropBlock, &CropBlock::$canSurvive, bool, BlockSource& region, BlockPos const& pos) {
    return standsOnPlanter(region, pos) || origin(region, pos);
}
LL_TYPE_INSTANCE_HOOK(FoliageMayPlaceOnHook, ll::memory::HookPriority::Normal, FoliageBlock, &FoliageBlock::$mayPlaceOn, bool, BlockSource& region, BlockPos const& pos) {
    return supportsPlacement(region, pos) || origin(region, pos);
}
LL_TYPE_INSTANCE_HOOK(FoliageCanSurviveHook, ll::memory::HookPriority::Normal, FoliageBlock, &FoliageBlock::$canSurvive, bool, BlockSource& region, BlockPos const& pos) {
    return standsOnPlanter(region, pos) || origin(region, pos);
}
LL_TYPE_INSTANCE_HOOK(FlowerMayPlaceOnHook, ll::memory::HookPriority::Normal, FlowerBlock, &FlowerBlock::$mayPlaceOn, bool, BlockSource& region, BlockPos const& pos) {
    return supportsPlacement(region, pos) || origin(region, pos);
}
LL_STATIC_HOOK(
    CropGrowthHook,
    ll::memory::HookPriority::Normal,
    &CropUtils::getGrowthSpeed,
    float,
    BlockType const&   type,
    BlockSource const& region,
    BlockPos const&    pos
) {
    float speed = origin(type, region, pos);
    if (config().planterGrowthBoost && standsOnPlanter(region, pos)) speed += 3.0f; // moist farmland below
    return speed;
}
} // namespace

bool supports(BlockSource const& region, BlockPos const& pos) { return supportsPlacement(region, pos); }

void installHooks() {
    CropMayPlaceOnHook::hook();
    CropCanSurviveHook::hook();
    FoliageMayPlaceOnHook::hook();
    FoliageCanSurviveHook::hook();
    FlowerMayPlaceOnHook::hook();
    CropGrowthHook::hook();
}

void uninstallHooks() {
    CropMayPlaceOnHook::unhook();
    CropCanSurviveHook::unhook();
    FoliageMayPlaceOnHook::unhook();
    FoliageCanSurviveHook::unhook();
    FlowerMayPlaceOnHook::unhook();
    CropGrowthHook::unhook();
}

} // namespace cm::feature::tableware::planter
