// Urn: treasure urns (cave worldgen) drop loot through their block loot table and may release a
// mob; any projectile breaks an urn.

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/HitResult.h"

#include "feature/tableware/Tableware.h"
#include "mod/Log.h"

namespace cm::feature::tableware::urn {

namespace {
int gReleases = 0;
}

int releases() { return gReleases; }

void onRemoved(int dim, BlockPos const& pos, Block const& prev) {
    if (state(prev, "cm:treasure") != 1) return;
    auto const& mobs = config().urnSpawnMobs;
    if (mobs.empty() || randomFloat() >= config().urnEntitySpawnChance) return;
    ++gReleases;
    spawnMobLater(dim, pos, mobs[randomInt(static_cast<int>(mobs.size()))]);
}

namespace {
void breakLater(int dim, BlockPos pos) {
    defer([dim, pos] {
        auto level = ll::service::getLevel();
        if (!level) return;
        auto d = level->getDimension(DimensionType(dim)).lock();
        if (!d) return;
        auto& region = d->getBlockSourceFromMainChunkSource();
        if (!region.hasChunksAt(pos, 0, false) || !is(region.getBlock(pos), kUrn)) return;
        level->destroyBlock(region, pos, true, BlockChangeContext{});
        log().info("tableware: a projectile broke an urn");
    });
}

LL_TYPE_INSTANCE_HOOK(
    UrnProjectileHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    Actor&           owner,
    HitResult const& result
) {
    if (result.mType == HitResultType::Tile && !owner.isClientSide()) {
        auto& region = owner.getDimensionBlockSource();
        if (region.hasChunksAt(result.mBlock, 0, false) && is(region.getBlock(result.mBlock), kUrn)) {
            breakLater(static_cast<int>(region.getDimensionId()), result.mBlock);
        }
    }
    origin(owner, result);
}
} // namespace

void installHooks() { UrnProjectileHook::hook(); }
void uninstallHooks() { UrnProjectileHook::unhook(); }

} // namespace cm::feature::tableware::urn
