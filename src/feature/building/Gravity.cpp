// Falling ash, raked gravel and sugar cubes; ash layers; dissolving sugar.
//
// Bedrock's falling-block entity only lands vanilla falling blocks, so our blocks fall as blocks:
// 2 ticks after a change a block with nothing under it moves down one block per tick until it lands
// (see docs/spec/building.md, "Not ported / differences").

#include "feature/building/Building.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/TickingQueueType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/material/Material.h"

#include "game/Blocks.h"
#include "mod/Config.h"

namespace cm::feature::building {

namespace {

struct Pending {
    int           dim;
    BlockPos      pos;
    std::uint64_t due;
    std::uint64_t since; ///< when it was first scheduled (unloaded chunks retry for a while)
};
using PosKey = std::tuple<int, int, int, int>;

// Falls are scheduled from neighbour updates; keep the queue safe should one come from another thread.
std::mutex           gMutex;
std::vector<Pending> gPending;
std::set<PosKey>     gPendingKeys;
std::uint64_t        gTick = 0;

constexpr std::uint64_t kRetryTicks = 20 * 60 * 5; ///< keep retrying a fall in an unloaded chunk for 5 minutes

PosKey keyOf(int dim, BlockPos const& pos) { return {dim, pos.x, pos.y, pos.z}; }

void fallStepLoaded(BlockSource& region, BlockPos const& pos);

bool isWater(Block const& b) {
    auto const& name = b.getTypeName();
    return name == "minecraft:water" || name == "minecraft:flowing_water";
}

/// One step of a falling block at `pos`. False when its chunks aren't loaded (try again later).
bool fallStep(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 1, false)) return false;
    fallStepLoaded(region, pos);
    return true;
}

void fallStepLoaded(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    Kind        kind  = kindOf(block);
    if (!fallsLikeSand(kind)) return;
    if (kind == Kind::SugarCube && config::get<BuildingConfig>().sugarCubeDissolves && touchesWater(region, pos)) {
        dissolve(region, pos);
        return;
    }
    BlockPos below{pos.x, pos.y - 1, pos.z};
    if (kind == Kind::Ash) {
        // falling ash piles onto ash
        auto const& under = region.getBlock(below);
        if (kindOf(under) == Kind::Ash) {
            int have = ashLayers(under);
            if (have < 8) {
                int mine  = ashLayers(block);
                int moved = std::min(mine, 8 - have);
                if (auto const* nb = ashBlock(have + moved)) game::setBlock(region, below, *nb);
                if (mine - moved > 0) {
                    if (auto const* nb = ashBlock(mine - moved)) game::setBlock(region, pos, *nb);
                } else {
                    game::clearBlock(region, pos);
                }
                return;
            }
        }
    }
    if (!isFree(region, below)) {
        ensureWeatherTick(region, pos, block); // landed
        return;
    }
    bool intoWater = isWater(region.getBlock(below)) || isWater(region.getLiquidBlock(below));
    Block const& moving = block;
    game::setBlock(region, below, moving);
    game::clearBlock(region, pos);
    if (kind == Kind::SugarCube && config::get<BuildingConfig>().sugarCubeDissolves && (intoWater || touchesWater(region, below))) {
        dissolve(region, below);
        return;
    }
    scheduleFall(region, below, 1);
}

} // namespace

bool fallsLikeSand(Kind kind) { return kind == Kind::Ash || kind == Kind::RakedGravel || kind == Kind::SugarCube; }

bool isFree(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight()) return false;
    if (!region.hasChunksAt(pos, 0, false)) return false;
    auto const& b = region.getBlock(pos);
    if (b.isAir()) return true;
    if (b.getMaterial().mLiquid) return true;
    auto const& name = b.getTypeName();
    if (name == "minecraft:fire" || name == "minecraft:soul_fire") return true;
    if (kindOf(b) != Kind::None) return false;
    return b.getBlockType().canBeBuiltOver(b, region, pos);
}

bool touchesWater(BlockSource& region, BlockPos const& pos) {
    static constexpr int d[6][3]{
        {0,  -1, 0 },
        {0,  1,  0 },
        {0,  0,  -1},
        {0,  0,  1 },
        {-1, 0,  0 },
        {1,  0,  0 }
    };
    for (auto const& o : d) {
        BlockPos p{pos.x + o[0], pos.y + o[1], pos.z + o[2]};
        if (!region.hasChunksAt(p, 0, false)) continue;
        if (isWater(region.getBlock(p)) || isWater(region.getLiquidBlock(p))) return true;
    }
    return false;
}

void dissolve(BlockSource& region, BlockPos const& pos) {
    game::clearBlock(region, pos);
    auto& level = region.getLevel();
    Vec3  c = pos.center();
    // broadcast the fizz to the players around (the sound needs an actor identity: any player there)
    auto  dim = region.getDimensionId();
    Player* listener = nullptr;
    level.forEachPlayer([&](Player& p) {
        if (p.getDimensionId() != dim) return true;
        listener = &p;
        return false;
    });
    if (listener) {
        level.broadcastSoundEvent(region, SharedTypes::Legacy::LevelSoundEvent::Fizz, c, -1, ActorSoundIdentifier(*listener), false, std::nullopt);
    }
    level.spawnParticleEffect("minecraft:water_evaporation_bucket_emitter", c, &region.getDimension());
}

void ensureWeatherTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    Kind kind = kindOf(block);
    if (kind != Kind::Ash && kind != Kind::SugarCube) return;
    if (region.hasTickInPendingTicks(pos, block.getBlockType(), TickingQueueType::Internal)) return;
    // vanilla random ticks reach a block every ~68 s on average (randomTickSpeed 3)
    thread_local std::mt19937 rng{std::random_device{}()};
    region.addToTickingQueue(pos, block, std::uniform_int_distribution<int>(400, 2400)(rng));
}

void rainTick(BlockSource& region, BlockPos const& pos, bool wash) {
    auto const& block = region.getBlock(pos);
    Kind        kind  = kindOf(block);
    if (kind == Kind::SugarCube) {
        if (config::get<BuildingConfig>().sugarCubeDissolves) dissolve(region, pos);
        return;
    }
    if (kind != Kind::Ash || !wash || !config::get<BuildingConfig>().rainWashAsh) return;
    int layers = ashLayers(block);
    if (layers <= 1) {
        game::clearBlock(region, pos);
    } else if (auto const* nb = ashBlock(layers - 1)) {
        game::setBlock(region, pos, *nb);
    }
}

void scheduleFall(BlockSource& region, BlockPos const& pos, int delay) {
    int             dim = static_cast<int>(region.getDimensionId());
    std::lock_guard lock(gMutex);
    if (!gPendingKeys.insert(keyOf(dim, pos)).second) return;
    gPending.push_back({dim, pos, gTick + static_cast<std::uint64_t>(std::max(delay, 1)), gTick});
}

void tickGravity(std::uint64_t tick) {
    std::vector<Pending> due;
    {
        std::lock_guard lock(gMutex);
        gTick = tick;
        if (gPending.empty()) return;
        std::erase_if(gPending, [&due](Pending const& p) {
            if (p.due > gTick) return false;
            due.push_back(p);
            gPendingKeys.erase(keyOf(p.dim, p.pos));
            return true;
        });
    }
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto const& p : due) {
        auto dim = level->getDimension(p.dim).lock();
        if (!dim) continue;
        auto& region = dim->getBlockSourceFromMainChunkSource();
        if (!fallStep(region, p.pos) && tick - p.since < kRetryTicks) {
            std::lock_guard lock(gMutex);
            if (gPendingKeys.insert(keyOf(p.dim, p.pos)).second) gPending.push_back({p.dim, p.pos, tick + 20, p.since});
        }
    }
}

void clearGravity() {
    std::lock_guard lock(gMutex);
    gPending.clear();
    gPendingKeys.clear();
}

int addAsh(BlockSource& region, BlockPos const& pos, int layers) {
    if (layers <= 0 || !region.hasChunksAt(pos, 0, false)) return 0;
    auto const& block = region.getBlock(pos);
    int         have  = 0;
    if (kindOf(block) == Kind::Ash) {
        have = ashLayers(block);
    } else if (!block.isAir()) {
        return 0;
    }
    int total = std::min(8, have + layers);
    if (total == have) return 0;
    auto const* ash = ashBlock(total);
    if (!ash || !game::setBlock(region, pos, *ash)) return 0;
    ensureWeatherTick(region, pos, *ash);
    scheduleFall(region, pos);
    return total - have;
}

int ashLayers(Block const& block) {
    if (kindOf(block) != Kind::Ash) return 0;
    return stateOf(block, "complementaries:layers").value_or(7) + 1;
}

Block const* ashBlock(int layers) { return game::blockWith(kAsh, {{"complementaries:layers", std::clamp(layers, 1, 8) - 1}}); }

int ashLayersFor(double chance, double roll) {
    if (chance <= 0) return 0;
    int    n    = static_cast<int>(std::floor(chance));
    double frac = chance - n;
    if (roll < frac) ++n;
    return std::clamp(n, 0, 8);
}

} // namespace cm::feature::building
