// Block kinds, states and the connected shapes of stairs, walls and raked gravel.

#include "feature/building/Building.h"

#include <array>
#include <atomic>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/material/Material.h"

#include "game/Blocks.h"

namespace cm::feature::building {

namespace {

std::atomic<int> gKindGeneration{0};

Kind classify(std::string const& name) {
    static std::unordered_map<std::string, Kind> const kinds = [] {
        std::unordered_map<std::string, Kind> m;
        auto add = [&m](std::string_view path, Kind k) { m.emplace("complementaries:" + std::string(path), k); };
        add("ash", Kind::Ash);
        for (auto p : {"ash_bricks", "stone_tile", "blackstone_tile", "lapis_bricks"}) {
            add(std::string(p) + "_slab", Kind::Slab);
            add(std::string(p) + "_stairs", Kind::Stairs);
            add(std::string(p) + "_wall", Kind::Wall);
        }
        add("checker_slab", Kind::Slab);
        add("fine_wood_slab", Kind::Slab);
        add("fine_wood_stairs", Kind::Stairs);
        add("raked_gravel", Kind::RakedGravel);
        add("sugar_cube", Kind::SugarCube);
        add("feather_block", Kind::Feather);
        add("timber_frame", Kind::Timber);
        add("timber_brace", Kind::Timber);
        add("timber_cross_brace", Kind::Timber);
        add("daub_frame", Kind::DaubFramed);
        add("daub_brace", Kind::DaubFramed);
        add("daub_cross_brace", Kind::DaubFramed);
        add("daub", Kind::Daub);
        return m;
    }();
    auto it = kinds.find(name);
    return it == kinds.end() ? Kind::None : it->second;
}

/// HashedString of a state name, built once per name.
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

HashedString const& stateName(std::string_view name) {
    thread_local std::unordered_map<std::string, HashedString, StringHash, std::equal_to<>> names;
    auto it = names.find(name);
    if (it == names.end()) it = names.emplace(std::string(name), HashedString(std::string(name))).first;
    return it->second;
}

BlockPos offset(BlockPos const& pos, int facing, int dy = 0) {
    auto [dx, dz] = game::facingOffset(facing);
    return BlockPos{pos.x + dx, pos.y + dy, pos.z + dz};
}

int cw(int f) { return (f + 1) & 3; }
int ccw(int f) { return (f + 3) & 3; }
bool sameAxis(int a, int b) { return (a & 1) == (b & 1); }

/// FacingID (0 down, 1 up, 2 north, 3 south, 4 west, 5 east) of a Java 2D facing.
unsigned char faceId(int facing) {
    switch (facing & 3) {
    case 0:
        return 3;
    case 1:
        return 4;
    case 2:
        return 2;
    default:
        return 5;
    }
}

bool isAnyWall(Block const& b) { return kindOf(b) == Kind::Wall || b.getBlockType().isWallBlock(); }

constexpr std::array<char const*, 4> kWallStates{
    "complementaries:wall_south",
    "complementaries:wall_west",
    "complementaries:wall_north",
    "complementaries:wall_east"
};
constexpr std::array<char const*, 4> kVanillaWallStates{
    "wall_connection_type_south",
    "wall_connection_type_west",
    "wall_connection_type_north",
    "wall_connection_type_east"
};

/// Connection (0 none, 1 low, 2 tall) of any wall towards `facing`.
int wallSide(Block const& b, int facing) {
    if (kindOf(b) == Kind::Wall) return stateOf(b, kWallStates[facing]).value_or(0);
    return stateOf(b, kVanillaWallStates[facing]).value_or(0);
}

bool wallPost(Block const& b) {
    if (kindOf(b) == Kind::Wall) return stateOf(b, "complementaries:wall_post").value_or(1) != 0;
    return stateOf(b, "wall_post_bit").value_or(1) != 0;
}

bool connectsTo(Block const& nb, int towards) {
    if (nb.isAir()) return false;
    if (isAnyWall(nb)) return true;
    auto const& type = nb.getBlockType();
    if (type.isThinFenceBlock()) return true;
    if (nb.getTypeName().find("fence_gate") != std::string::npos) return true;
    return nb.canProvideFullSupport(faceId((towards + 2) & 3));
}

} // namespace

Kind kindOf(Block const& block) {
    struct Cache {
        int                                         generation = -1;
        std::unordered_map<BlockType const*, Kind> kinds;
    };
    thread_local Cache cache;
    int gen = gKindGeneration.load(std::memory_order_relaxed);
    if (cache.generation != gen) {
        cache.kinds.clear();
        cache.generation = gen;
    }
    auto const* type = &block.getBlockType();
    auto        it   = cache.kinds.find(type);
    if (it != cache.kinds.end()) return it->second;
    Kind k = classify(block.getTypeName());
    cache.kinds.emplace(type, k);
    return k;
}

void resetKinds() { gKindGeneration.fetch_add(1, std::memory_order_relaxed); }

std::optional<int> stateOf(Block const& block, std::string_view state) {
    auto const* s = block.getBlockType().getBlockState(stateName(state));
    if (!s) return std::nullopt;
    return block.getState<int>(*s);
}

Block const* withState(Block const& block, std::string_view state, int value) {
    auto const* s = block.getBlockType().getBlockState(stateName(state));
    if (!s) return nullptr;
    auto b = block.setState<int>(*s, value);
    return b ? b.as_ptr() : nullptr;
}

std::optional<int> stairFacing(Block const& block) {
    if (kindOf(block) == Kind::Stairs) return stateOf(block, "minecraft:cardinal_direction");
    if (auto w = stateOf(block, "weirdo_direction")) {
        // vanilla stairs: 0 east, 1 west, 2 south, 3 north
        static constexpr std::array<int, 4> toJava{3, 1, 0, 2};
        return toJava[*w & 3];
    }
    return std::nullopt;
}

std::optional<int> stairHalf(Block const& block) {
    if (kindOf(block) == Kind::Stairs) return stateOf(block, "minecraft:vertical_half");
    if (stateOf(block, "weirdo_direction")) return stateOf(block, "upside_down_bit");
    return std::nullopt;
}

int computeStairShape(BlockSource& region, BlockPos const& pos, Block const& stairs) {
    auto dirO  = stairFacing(stairs);
    auto halfO = stairHalf(stairs);
    if (!dirO || !halfO) return 0;
    int  dir  = *dirO;
    int  half = *halfO;
    auto canTakeShape = [&](int face) {
        auto const& n  = region.getBlock(offset(pos, face));
        auto        nf = stairFacing(n);
        return !nf || *nf != dir || stairHalf(n) != half;
    };
    auto const& front = region.getBlock(offset(pos, dir));
    if (auto f1 = stairFacing(front); f1 && stairHalf(front) == half) {
        if (!sameAxis(*f1, dir) && canTakeShape((*f1 + 2) & 3)) return *f1 == ccw(dir) ? 3 : 4;
    }
    auto const& back = region.getBlock(offset(pos, (dir + 2) & 3));
    if (auto f2 = stairFacing(back); f2 && stairHalf(back) == half) {
        if (!sameAxis(*f2, dir) && canTakeShape(*f2)) return *f2 == ccw(dir) ? 1 : 2;
    }
    return 0;
}

Block const& computeWall(BlockSource& region, BlockPos const& pos, Block const& wall) {
    auto const&       above     = region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z});
    bool              aboveWall = isAnyWall(above);
    bool              aboveFull = !above.isAir() && above.canProvideFullSupport(0);
    std::array<int, 4> side{};
    for (int f = 0; f < 4; ++f) {
        if (!connectsTo(region.getBlock(offset(pos, f)), f)) continue;
        bool tall = aboveFull || (aboveWall && wallSide(above, f) > 0);
        side[f]   = tall ? 2 : 1;
    }
    bool post;
    if (aboveWall && wallPost(above)) {
        post = true;
    } else {
        bool sN = side[0] == 0, wN = side[1] == 0, nN = side[2] == 0, eN = side[3] == 0;
        bool noneOrUneven = (sN && wN && nN && eN) || sN != nN || wN != eN;
        if (noneOrUneven) post = true;
        else if ((side[2] == 2 && side[0] == 2) || (side[3] == 2 && side[1] == 2)) post = false;
        else post = aboveFull;
    }
    Block const* b = &wall;
    for (int f = 0; f < 4; ++f) {
        if (auto const* nb = withState(*b, kWallStates[f], side[f])) b = nb;
    }
    if (auto const* nb = withState(*b, "complementaries:wall_post", post ? 1 : 0)) b = nb;
    return *b;
}

int rakedShapeForFacing(int facing) { return (facing & 1) == 0 ? 0 : 1; }

int computeRakedShape(BlockSource& region, BlockPos const& pos, int current) {
    std::array<bool, 4> n{};
    int                 count = 0;
    for (int f = 0; f < 4; ++f) {
        n[f] = kindOf(region.getBlock(offset(pos, f))) == Kind::RakedGravel;
        count += n[f] ? 1 : 0;
    }
    bool S = n[0], W = n[1], N = n[2], E = n[3];
    if (count == 0) return current;
    bool ns = N || S, ew = E || W;
    if (ns && !ew) return 0;
    if (ew && !ns) return 1;
    if (count == 2) {
        if (N && E) return 2;
        if (N && W) return 3;
        if (S && E) return 4;
        if (S && W) return 5;
    }
    return current;
}

bool updateShape(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    switch (kindOf(block)) {
    case Kind::Stairs: {
        int shape = computeStairShape(region, pos, block);
        if (stateOf(block, "complementaries:shape") == shape) return false;
        auto const* nb = withState(block, "complementaries:shape", shape);
        return nb && game::setBlock(region, pos, *nb);
    }
    case Kind::Wall: {
        auto const& nb = computeWall(region, pos, block);
        return &nb != &block && game::setBlock(region, pos, nb);
    }
    case Kind::RakedGravel: {
        auto const& above = region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z});
        if (!above.isAir() && above.getMaterial().mSolid && above.getTypeName().find("fence_gate") == std::string::npos) {
            auto const* gravel = game::blockWith("minecraft:gravel");
            return gravel && game::setBlock(region, pos, *gravel);
        }
        int cur   = stateOf(block, "complementaries:shape").value_or(0);
        int shape = computeRakedShape(region, pos, cur);
        if (shape == cur) return false;
        auto const* nb = withState(block, "complementaries:shape", shape);
        return nb && game::setBlock(region, pos, *nb);
    }
    default:
        return false;
    }
}

} // namespace cm::feature::building
