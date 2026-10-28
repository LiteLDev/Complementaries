#pragma once

// U7 mechanism: spring launcher, bellows, turn table, cog block and crank (docs/spec/mechanism.md).
// Internal declarations shared by the unit's .cpp files. Everything runs on the server thread.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/mechanism/RotationRules.h"

class AABB;
class Actor;
class Block;
class BlockPos;
class BlockSource;
class Player;
class Vec3;

namespace cm::feature::mechanism {

// ---- configuration (config/mechanism.json) -------------------------------------------------------
struct MechanismConfig {
    int version = 1;
    struct SpringLauncher {
        bool  enabled            = true;
        float launcherVelocity   = 1.5f; ///< blocks/tick
        float fallHeightRequired = 5.0f; ///< blocks
    } springLauncher;
    struct Bellows {
        bool  enabled    = true;
        int   period     = 78; ///< cycle length at power 1 (ticks)
        float power      = 2.0f;
        int   range      = 5;
        bool  heatBlocks = true;
    } bellows;
    struct TurnTable {
        bool enabled        = true;
        int  period         = 20; ///< rotation period at power 1 (ticks)
        bool rotateEntities = true;
    } turnTable;
    struct CogBlock {
        bool enabled = true;
    } cogBlock;
    struct Crank {
        bool enabled = true;
    } crank;
};

/// The config with values clamped to their ranges (refreshed every tick and on install).
[[nodiscard]] MechanismConfig const& config();
void                                 refreshConfig();

// ---- ids ----------------------------------------------------------------------------------------
namespace ids {
inline constexpr std::string_view kLauncher  = "complementaries:piston_launcher";
inline constexpr std::string_view kHead      = "complementaries:piston_launcher_head";
inline constexpr std::string_view kArm       = "complementaries:piston_launcher_arm";
inline constexpr std::string_view kBellows   = "complementaries:bellows";
inline constexpr std::string_view kTurnTable = "complementaries:turn_table";
inline constexpr std::string_view kCog       = "complementaries:cog_block";
inline constexpr std::string_view kCrank     = "complementaries:crank";
} // namespace ids

enum class Kind { None, Launcher, Head, Arm, Bellows, TurnTable, Cog, Crank };

[[nodiscard]] Kind kindOf(Block const& block);
[[nodiscard]] Kind kindAt(BlockSource& region, BlockPos const& pos);
/// Kinds that need the per-tick update (redstone input / animation).
[[nodiscard]] inline bool isTicking(Kind k) {
    return k == Kind::Launcher || k == Kind::Bellows || k == Kind::TurnTable || k == Kind::Cog;
}

// ---- block states -------------------------------------------------------------------------------
using StateList = std::vector<std::pair<std::string, rotation::Value>>;

/// The block's states as stored in its serialization id (names as in `/setblock`).
[[nodiscard]] StateList                      statesOf(Block const& block);
[[nodiscard]] std::optional<rotation::Value> stateOf(Block const& block, std::string_view name);
[[nodiscard]] int                            intStateOf(Block const& block, std::string_view name, int fallback = 0);
[[nodiscard]] bool boolStateOf(Block const& block, std::string_view name, bool fallback = false);
/// `id` with `states` (missing states keep their defaults). nullptr when the combination is unknown.
[[nodiscard]] Block const* blockFrom(std::string_view id, StateList const& states);
/// `block` with some states replaced.
[[nodiscard]] Block const* withStates(Block const& block, StateList const& changes);

/// Direction (0 down, 1 up, 2 north, 3 south, 4 west, 5 east) a mechanism block points to:
/// launcher / bellows / turn table: towards the player who placed it; head / arm: the launcher's;
/// crank: away from the block it hangs on. -1 for other blocks.
[[nodiscard]] int facingOf(Block const& block);
/// States that make `kind` face `dir` (the inverse of `facingOf`).
[[nodiscard]] StateList facingStates(Kind kind, int dir);

[[nodiscard]] BlockPos offset(BlockPos const& pos, int dir, int n = 1);

/// Sets a block (neighbour + client updates).
bool place(BlockSource& region, BlockPos const& pos, Block const& block);
bool placeAir(BlockSource& region, BlockPos const& pos);
/// Drops `id` x1 at `pos` (as a broken block would).
void dropItem(BlockSource& region, BlockPos const& pos, std::string_view id);

// ---- redstone -----------------------------------------------------------------------------------
/// Input strength of a consumer block (launcher, bellows, turn table): 0..15. A drop to 0 is only
/// reported after 2 ticks: rewriting the block's own state (stage, rotating...) may rebuild its
/// circuit component, which reads 0 until the next redstone update.
[[nodiscard]] int consumerPower(BlockSource& region, BlockPos const& pos);
/// Forgets the remembered power of `pos` (all positions when empty).
void forgetPower(std::optional<core::GlobalPos> const& pos);
/// Server ticks counted by the unit.
[[nodiscard]] std::uint64_t currentTick();
/// The signal the neighbour of `pos` in direction `dir` sends into `pos`. Cogs are not sources here,
/// and power that a dust line / block got from a cog `ignore` accepts is not counted either.
[[nodiscard]] int
signalInto(BlockSource& region, BlockPos const& pos, int dir, std::function<bool(BlockPos const&)> const& ignore = {});
/// Debug: (strength, direction) of the circuit component at `pos`, (-1, -1) if none.
[[nodiscard]] std::pair<int, int> componentInfo(BlockSource& region, BlockPos const& pos);

/// Every entity (players included) whose box overlaps `box`.
[[nodiscard]] std::vector<Actor*> entitiesIn(BlockSource& region, AABB const& box);

// ---- effects ------------------------------------------------------------------------------------
void playSound(BlockSource& region, Vec3 const& pos, std::string const& name, float volume = 1.f, float pitch = 1.f);
void spawnParticle(BlockSource& region, Vec3 const& pos, std::string const& effect);
/// Sets an entity's velocity (players are sent the motion).
void setMotion(Actor& actor, Vec3 const& motion);
/// Turns an entity by `degrees` of yaw (players are sent the new head rotation).
void               turnEntity(Actor& actor, float degrees);
void               actionBar(Player& player, std::string const& text);
[[nodiscard]] Vec3 centerOf(BlockPos const& pos);

// ---- runtime ------------------------------------------------------------------------------------
/// Runs `fn` at the start of the next server tick (world edits outside of game callbacks).
void defer(std::function<void()> fn);
/// Starts / stops updating a ticking mechanism block.
void                       track(BlockSource const& region, BlockPos const& pos);
[[nodiscard]] bool         isTracked(core::GlobalPos const& pos);
[[nodiscard]] BlockSource* regionOf(int dim);

// subsystems (one .cpp each); `tick` runs once per server tick for each loaded tracked block
namespace launcher {
/// Drops the runtime state of a removed block.
void erase(core::GlobalPos const& pos);
void tick(BlockSource& region, BlockPos const& pos, Block const& block);
/// Extends now if possible (launching what is in front). Returns false if blocked / extended.
bool fire(BlockSource& region, BlockPos const& pos);
/// A neighbour of a launcher part changed (deferred to the next tick).
void check(BlockSource& region, BlockPos const& pos);
/// An entity is about to take fall damage on top of `below`: true if the launcher absorbs it.
bool absorbFall(Actor& actor, float distance);
void playerBroke(BlockSource& region, BlockPos const& pos, bool creative);
void reset();
} // namespace launcher

namespace bellows {
/// Drops the runtime state of a removed block.
void erase(core::GlobalPos const& pos);
void tick(BlockSource& region, BlockPos const& pos, Block const& block);
/// One manual pump (player use / stepped on).
void press(BlockSource& region, BlockPos const& pos);
void reset();
} // namespace bellows

namespace turntable {
/// Drops the runtime state of a removed block.
void erase(core::GlobalPos const& pos);
void tick(BlockSource& region, BlockPos const& pos, Block const& block);
/// Rotates the block in front of the table once. Returns the rotated block id ("" if nothing).
std::string rotateFront(BlockSource& region, BlockPos const& pos);
/// Toggles clockwise / counter-clockwise. Returns the new `inverted`.
bool toggle(BlockSource& region, BlockPos const& pos);
void reset();
} // namespace turntable

namespace cog {
/// Resolves every loaded cog cluster (called once per tick with all tracked cogs of a dimension).
void tickAll(BlockSource& region, std::vector<BlockPos> const& cogs);
} // namespace cog

namespace crank {
/// Turns the crank one step. Returns the new power (-1 if `pos` is no crank).
int turn(BlockSource& region, BlockPos const& pos, bool ccw, Player* player);
/// The block behind changed: pops the crank off when it lost its support.
void check(BlockSource& region, BlockPos const& pos);
/// A crank at `pos` pointing to `facing` would hang on a block that can hold it.
[[nodiscard]] bool supported(BlockSource& region, BlockPos const& pos, int facing);
} // namespace crank

} // namespace cm::feature::mechanism
