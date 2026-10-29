// Flax crop: two blocks tall, random-tick growth simulated by the mod, bone meal, drops.

#include <random>
#include <set>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/block/FarmBlock.h"
#include "mc/world/level/block/block_events/BlockRandomTickEvent.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/rope/Common.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::rope::flax {

namespace {
constexpr char const* kKey       = "cm:rope:flax";
constexpr int         kMaxAge    = 7;
constexpr int         kDoubleAge = 4; ///< from this age the crop is two blocks tall

std::set<core::GlobalPos> gCrops;   ///< lower halves
std::set<core::GlobalPos> gNoDrops; ///< removals that must not drop (creative)
bool                      gDirty = false;
std::mt19937              gRng{std::random_device{}()};

bool upper(Block const& b) { return stateOf(b, "complementaries:upper") != 0; }
int  age(Block const& b) { return stateOf(b, "complementaries:age"); }

Block const* crop(int a, bool up) {
    return game::blockWith(
        kFlaxCrop,
        {
            {"complementaries:age",   a         },
            {"complementaries:upper", up ? 1 : 0}
    }
    );
}

int lightAt(BlockSource& r, BlockPos const& pos) {
    // Java crops read the raw brightness without the night darkening: flax grows at night too
    auto pair = r.getBrightnessPair(pos);
    int  sky  = static_cast<int>(pair.sky.get().mValue);
    int  blk  = static_cast<int>(pair.block.get().mValue);
    return std::max(blk, sky);
}

/// Java crop growth speed: farmland under and around the crop, halved by crowded rows.
float growthSpeed(BlockSource& r, BlockPos const& pos) {
    float f    = 1.0f;
    auto  base = neighbor(pos, 0);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            BlockPos    p{base.x + dx, base.y, base.z + dz};
            auto const& b = r.getBlock(p);
            float       g = 0.0f;
            if (b.getTypeName() == "minecraft:farmland") {
                g = 1.0f;
                if (stateOf(b, "moisturized_amount") > 0) g = 3.0f;
            }
            if (dx != 0 || dz != 0) g /= 4.0f;
            f += g;
        }
    }
    auto same = [&](int dx, int dz) { return isFlaxCrop(r.getBlock({pos.x + dx, pos.y, pos.z + dz})); };
    bool ns   = same(0, -1) || same(0, 1);
    bool we   = same(-1, 0) || same(1, 0);
    bool diag = same(-1, -1) || same(1, -1) || same(1, 1) || same(-1, 1);
    if (diag || (ns && we)) f /= 2.0f;
    return f * static_cast<float>(std::max(0.0, config().flaxGrowthMultiplier));
}

/// Sets the crop at `pos` (lower half) to `newAge`, growing / updating the upper half.
bool growTo(BlockSource& r, BlockPos const& pos, int newAge) {
    auto const& b   = r.getBlock(pos);
    int         old = age(b);
    newAge          = std::min(newAge, kMaxAge);
    auto above      = neighbor(pos, 1);
    if (newAge >= kDoubleAge) {
        auto const& ab   = r.getBlock(above);
        bool        free = ab.isAir() || (isFlaxCrop(ab) && upper(ab));
        if (!free || !loaded(r, above)) newAge = std::min(newAge, kDoubleAge - 1);
    }
    if (newAge <= old) return false;
    auto const* lower = crop(newAge, false);
    if (!lower || !set(r, pos, *lower)) return false;
    if (newAge >= kDoubleAge) {
        if (auto const* up = crop(newAge, true)) set(r, above, *up);
    }
    return true;
}

void spawnDrops(BlockSource& r, core::GlobalPos const& g, int a) {
    Vec3 at{static_cast<float>(g.x) + 0.5f, static_cast<float>(g.y) + 0.3f, static_cast<float>(g.z) + 0.5f};
    if (a >= kMaxAge) {
        std::binomial_distribution<int> extra(3, 0.5714);
        dropAt(r, at, makeStack(kFlax, 1));
        dropAt(r, at, makeStack(kFlaxSeeds, 1 + extra(gRng)));
    } else {
        dropAt(r, at, makeStack(kFlaxSeeds, 1));
    }
}
} // namespace

namespace {
/// Farmland next to water (4 blocks around, same level or one above) stays wet.
bool waterNear(BlockSource& r, BlockPos const& pos) {
    for (int dx = -4; dx <= 4; ++dx) {
        for (int dz = -4; dz <= 4; ++dz) {
            for (int dy = 0; dy <= 1; ++dy) {
                BlockPos p{pos.x + dx, pos.y + dy, pos.z + dz};
                if (!loaded(r, p)) continue;
                for (auto const* b : {&r.getBlock(p), &r.getLiquidBlock(p)}) {
                    auto const& id = b->getTypeName();
                    if (id == "minecraft:water" || id == "minecraft:flowing_water") return true;
                }
            }
        }
    }
    return false;
}
} // namespace

// Dry farmland turns into dirt unless a crop grows on it; vanilla only knows its own crops.
LL_TYPE_INSTANCE_HOOK(
    FlaxFarmlandHook,
    ll::memory::HookPriority::Normal,
    FarmBlock,
    &FarmBlock::randomTick,
    void,
    BlockEvents::BlockRandomTickEvent& event
) {
    BlockSource& r     = event.mRegion;
    BlockPos     pos   = event.mPos.get();
    auto         above = neighbor(pos, 1);
    if (isServerRegion(r) && isFlaxCrop(r.getBlock(above)) && stateOf(r.getBlock(pos), "moisturized_amount") == 0
        && !waterNear(r, pos) && !r.getDimension().mWeather->isRainingAt(r, above)) {
        return; // stays dry farmland under the crop
    }
    origin(event);
}

void install() { FlaxFarmlandHook::hook(); }
void uninstall() { FlaxFarmlandHook::unhook(); }

void noDrops(core::GlobalPos const& pos) { gNoDrops.insert(pos); }

std::size_t tracked() { return gCrops.size(); }

bool growthTick(BlockSource& r, BlockPos const& pos, bool forced) {
    if (!loaded(r, pos)) return false;
    auto const& b = r.getBlock(pos);
    if (!isFlaxCrop(b) || upper(b)) return false;
    int a = age(b);
    if (a >= kMaxAge) return false;
    if (!forced) {
        if (lightAt(r, neighbor(pos, 1)) < 9) return false;
        float f = growthSpeed(r, pos);
        if (f <= 0.0f) return false;
        int bound = static_cast<int>(25.0f / f) + 1;
        if (std::uniform_int_distribution<int>(0, bound - 1)(gRng) != 0) return false;
    }
    return growTo(r, pos, a + 1);
}

bool boneMeal(BlockSource& r, BlockPos const& clicked) {
    BlockPos pos = clicked;
    if (upper(r.getBlock(pos))) pos = neighbor(pos, 0);
    auto const& b = r.getBlock(pos);
    if (!isFlaxCrop(b) || upper(b) || age(b) >= kMaxAge) return false;
    int inc = std::uniform_int_distribution<int>(2, 5)(gRng);
    if (!growTo(r, pos, age(b) + inc)) return false;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    r.getLevel().broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::ParticleCropGrowth, at, 0, nullptr);
    return true;
}

void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& prev, Block const& next) {
    auto g = gpos(region, pos);
    if (isFlaxCrop(next) && !upper(next) && gCrops.insert(g).second) gDirty = true;
    if (!isFlaxCrop(prev)) return;
    bool wasUpper = upper(prev);
    if (isFlaxCrop(next) && upper(next) == wasUpper) return; // grew
    bool drops = !gNoDrops.erase(g);
    if (!wasUpper) {
        if (!(isFlaxCrop(next) && !upper(next)) && gCrops.erase(g)) gDirty = true;
        int a = age(prev);
        later(1, [g, a, drops] {
            auto* r = serverRegion(g.dim);
            if (!r) return;
            if (drops) spawnDrops(*r, g, a);
            auto        above = neighbor(game::blockPos(g), 1);
            auto const& ab    = r->getBlock(above);
            if (isFlaxCrop(ab) && upper(ab)) setAir(*r, above);
        });
    } else {
        later(1, [g, drops] {
            auto* r = serverRegion(g.dim);
            if (!r) return;
            auto        below = neighbor(game::blockPos(g), 0);
            auto const& bb    = r->getBlock(below);
            if (isFlaxCrop(bb) && !upper(bb)) {
                if (!drops) noDrops(gpos(*r, below));
                setAir(*r, below);
            }
        });
    }
}

void tick() {
    gNoDrops.clear();
    if (gCrops.empty()) return;
    // 3 random ticks per 16x16x16 section per game tick (Java's default randomTickSpeed).
    std::uniform_int_distribution<int> roll(0, 4095);
    std::vector<core::GlobalPos>       gone;
    for (auto const& g : gCrops) {
        if (roll(gRng) >= 3) continue;
        auto* r = serverRegion(g.dim);
        if (!r) continue;
        auto pos = game::blockPos(g);
        if (!loaded(*r, pos)) continue;
        auto const& b = r->getBlock(pos);
        if (!isFlaxCrop(b) || upper(b)) {
            gone.push_back(g);
            continue;
        }
        growthTick(*r, pos, false);
    }
    for (auto const& g : gone) gCrops.erase(g);
    if (!gone.empty()) gDirty = true;
}

void load() {
    gCrops.clear();
    if (auto j = game::db::load(kKey); j && j->is_array()) {
        for (auto const& e : *j) {
            if (e.is_array() && e.size() == 4)
                gCrops.insert({e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
        }
    }
    gDirty = false;
}

void save() {
    if (!gDirty) return;
    nlohmann::json arr = nlohmann::json::array();
    for (auto const& g : gCrops) arr.push_back({g.dim, g.x, g.y, g.z});
    game::db::save(kKey, arr);
    gDirty = false;
}

void reset() {
    gCrops.clear();
    gNoDrops.clear();
    gDirty = false;
}

} // namespace cm::feature::rope::flax
