#pragma once

// U10 `building`: ash, stairs / slabs / walls, daub and timber frames, raked gravel, soap / flint /
// feather blocks and sugar cubes (docs/spec/building.md). Server side; works with vanilla clients.

#include <cstdint>
#include <optional>
#include <string_view>

class Block;
class BlockPos;
class BlockSource;
class Player;

namespace cm::feature::building {

/// `config/building.json`.
struct BuildingConfig {
    int    version            = 1;
    double ashFromFireChance  = 1.0;
    bool   ashFromBurningMobs = true;
    bool   rainWashAsh        = true;
    bool   swapOnShift        = false;
    bool   axesStrip          = true;
    bool   replaceDaub        = true;
    int    horseSpeedDuration = 10;
    bool   sugarCubeDissolves = true;
};

inline constexpr std::string_view kAsh         = "complementaries:ash";
inline constexpr std::string_view kRakedGravel = "complementaries:raked_gravel";
inline constexpr std::string_view kSugarCube   = "complementaries:sugar_cube";
inline constexpr std::string_view kFeather     = "complementaries:feather_block";
inline constexpr std::string_view kDaub        = "complementaries:daub";

enum class Kind : std::uint8_t {
    None,
    Ash,
    Slab,
    Stairs,
    Wall,
    RakedGravel,
    SugarCube,
    Feather,
    Timber,     ///< timber_frame / timber_brace / timber_cross_brace
    DaubFramed, ///< daub_frame / daub_brace / daub_cross_brace
    Daub,
};

/// The unit's kind of a block (cached per block type; `None` for every other block).
[[nodiscard]] Kind kindOf(Block const& block);
/// Forgets the cached block types (a new world registers its blocks again).
void resetKinds();

// ---- states -------------------------------------------------------------------------------------
[[nodiscard]] std::optional<int> stateOf(Block const& block, std::string_view state);
/// `block` with `state` set to `value` (nullptr if the block has no such state).
[[nodiscard]] Block const* withState(Block const& block, std::string_view state, int value);

/// Java 2D facing (0 S, 1 W, 2 N, 3 E) of our stairs or vanilla stairs; nullopt for other blocks.
[[nodiscard]] std::optional<int> stairFacing(Block const& block);
/// 0 bottom, 1 top (upside down).
[[nodiscard]] std::optional<int> stairHalf(Block const& block);

// ---- shapes (Shapes.cpp) ------------------------------------------------------------------------
/// Stair shape 0 straight, 1 inner_left, 2 inner_right, 3 outer_left, 4 outer_right.
[[nodiscard]] int computeStairShape(BlockSource& region, BlockPos const& pos, Block const& stairs);
/// The wall block with connections / post updated for its neighbours.
[[nodiscard]] Block const& computeWall(BlockSource& region, BlockPos const& pos, Block const& wall);
/// Raked gravel shape from its neighbours, `current` when they don't decide it.
[[nodiscard]] int computeRakedShape(BlockSource& region, BlockPos const& pos, int current);
/// Raked gravel shape for a player looking along `facing` (Java 2D facing).
[[nodiscard]] int rakedShapeForFacing(int facing);
/// Recomputes the connected shape of our stairs / walls / raked gravel at `pos`. Returns true if
/// the block changed (or turned back into gravel).
bool updateShape(BlockSource& region, BlockPos const& pos);

// ---- gravity (Gravity.cpp) ----------------------------------------------------------------------
/// Checks `pos` in `delay` ticks: a falling block with nothing under it moves down.
void scheduleFall(BlockSource& region, BlockPos const& pos, int delay = 2);
/// Ash, raked gravel and sugar cubes fall.
[[nodiscard]] bool fallsLikeSand(Kind kind);
/// The block below `pos` lets falling blocks through.
[[nodiscard]] bool isFree(BlockSource& region, BlockPos const& pos);
/// Water touches the block at `pos` on any side.
[[nodiscard]] bool touchesWater(BlockSource& region, BlockPos const& pos);
/// Removes a sugar cube with a fizz.
void dissolve(BlockSource& region, BlockPos const& pos);
/// Makes sure ash / sugar cubes at `pos` get their periodic weather tick (a queued block tick
/// every 20 to 120 s, kept in the chunk's tick queue).
void ensureWeatherTick(BlockSource& region, BlockPos const& pos, Block const& block);
/// Rain on a periodic tick: a sugar cube dissolves, ash loses a layer when `wash` (1 in 2).
void rainTick(BlockSource& region, BlockPos const& pos, bool wash);
/// Runs due falls (server tick).
void tickGravity(std::uint64_t tick);
void clearGravity();

// ---- ash (Gravity.cpp) --------------------------------------------------------------------------
// Integer block states are stored as indices into their value range: `complementaries:layers`
// (1..8) is read and written as 0..7 from C++. These helpers use real layer counts.
/// Layers (1..8) of an ash block, 0 for other blocks.
[[nodiscard]] int ashLayers(Block const& block);
/// The ash block with `layers` (1..8) layers.
[[nodiscard]] Block const* ashBlock(int layers);
/// Adds `layers` of ash at `pos` (air / ash only); returns the layers placed.
int addAsh(BlockSource& region, BlockPos const& pos, int layers);
/// Layers a burned block leaves for `chance` (> 1: more layers), using `roll` in [0, 1).
[[nodiscard]] int ashLayersFor(double chance, double roll);

} // namespace cm::feature::building
