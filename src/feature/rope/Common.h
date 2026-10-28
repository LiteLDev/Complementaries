#pragma once

// Shared helpers of the rope unit (server thread).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class BlockType;
class ItemStack;
class ItemStackBase;
class Player;
class Vec3;

namespace cm::feature::rope {

inline constexpr std::string_view kRope       = "complementaries:rope";
inline constexpr std::string_view kRopeArrow  = "complementaries:rope_arrow";
inline constexpr std::string_view kPulley     = "complementaries:pulley_block";
inline constexpr std::string_view kFlaxCrop   = "complementaries:flax_crop";
inline constexpr std::string_view kWildFlax   = "complementaries:wild_flax";
inline constexpr std::string_view kFlaxBlock  = "complementaries:flax_block";
inline constexpr std::string_view kFlax       = "complementaries:flax";
inline constexpr std::string_view kFlaxSeeds  = "complementaries:flax_seeds";
inline constexpr std::string_view kCrank      = "complementaries:crank";
inline constexpr std::string_view kTurnTable  = "complementaries:turn_table";
inline constexpr int              kArrowCharges = 32; ///< rope arrow max durability

/// `config/rope.json`.
struct RopeConfig {
    int    version             = 1;
    bool   blockSideAttachment = true;
    bool   bellRinging         = true;
    int    bellChainLength     = 16;
    bool   ropeArrowEnabled    = true;
    bool   pulleyMovesBlocks   = true;
    double flaxGrowthMultiplier = 1.0;
};
[[nodiscard]] RopeConfig const& config();

// ---- block types (resolved once a world exists) -------------------------------------------------
struct Types {
    BlockType const* rope      = nullptr;
    BlockType const* pulley    = nullptr;
    BlockType const* flaxCrop  = nullptr;
    BlockType const* flaxBlock = nullptr;
    BlockType const* wildFlax  = nullptr;
};
[[nodiscard]] Types const& types();
/// Looks the block types up again (world ready / client join). Also marks the rope climbable.
void resolveTypes();
void clearTypes();
/// Sets the climbable flag of the rope block type (server and client). False if not registered.
bool markRopeClimbable();

[[nodiscard]] bool isRope(Block const& b);
[[nodiscard]] bool isPulley(Block const& b);
[[nodiscard]] bool isFlaxCrop(Block const& b);
[[nodiscard]] bool isFlaxBlock(Block const& b);
/// Vanilla chains (`minecraft:chain`, `minecraft:iron_chain`, copper chains).
[[nodiscard]] bool isChain(Block const& b);
/// The vanilla chain block id of this game version ("minecraft:iron_chain" or "minecraft:chain").
[[nodiscard]] std::string const& chainId();

/// Bool / int state of `b` (0 when missing).
[[nodiscard]] int stateOf(Block const& b, std::string_view name);
/// `b` with the given states changed (b itself when a state is unknown).
[[nodiscard]] Block const& withStates(Block const& b, std::vector<std::pair<std::string_view, int>> const& states);

// ---- positions -----------------------------------------------------------------------------------
/// Bedrock FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
[[nodiscard]] BlockPos neighbor(BlockPos const& pos, int face);
[[nodiscard]] int      oppositeFace(int face);
/// The server block source of dimension `dim` (nullptr when the dimension is not loaded).
[[nodiscard]] BlockSource* serverRegion(int dim);
[[nodiscard]] bool         isServerRegion(BlockSource const& region);
[[nodiscard]] bool         loaded(BlockSource& region, BlockPos const& pos);
[[nodiscard]] core::GlobalPos gpos(BlockSource const& region, BlockPos const& pos);

// ---- world edits ---------------------------------------------------------------------------------
bool set(BlockSource& region, BlockPos const& pos, Block const& block);
bool setAir(BlockSource& region, BlockPos const& pos);
/// Breaks the block like a player would (loot + particles).
bool destroy(BlockSource& region, BlockPos const& pos, bool drops);
/// Air, plants, snow layers, water... (things a placed block replaces).
[[nodiscard]] bool replaceable(BlockSource& region, BlockPos const& pos);
/// Pulleys may move it: not air/liquid, breakable, no block entity.
[[nodiscard]] bool movable(BlockSource& region, BlockPos const& pos);
/// Plays a block sound (place / break) of `block` at `pos`, heard by everyone near.
void blockSound(BlockSource& region, BlockPos const& pos, Block const& block, bool place, Actor const& by);

// ---- items ---------------------------------------------------------------------------------------
[[nodiscard]] ItemStack makeStack(std::string_view id, int count, int aux = 0);
[[nodiscard]] std::string itemId(ItemStackBase const& item);
/// Adds `item` to the inventory (drops the rest at the player's feet).
void giveOrDrop(Player& player, ItemStack const& item);
void dropAt(BlockSource& region, Vec3 const& pos, ItemStack const& item);
/// Removes `n` of the selected item (survival only).
void consumeSelected(Player& player, int n = 1);
[[nodiscard]] bool survival(Player const& player);
/// The online player with this unique id (nullptr when gone).
[[nodiscard]] Player* playerById(std::int64_t uid);

// ---- deferred work -------------------------------------------------------------------------------
/// Runs `fn` on the server thread after `ticks` ticks (1 = next tick).
void later(int ticks, std::function<void()> fn);
/// Ticks since the module was installed.
[[nodiscard]] std::uint64_t now();
namespace detail {
void runLater(); ///< module tick
void clearLater();
} // namespace detail

// ---- sub-module hooks (called by the module) -------------------------------------------------------
namespace ropes {
void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& prev, Block const& next);
void tick();
void reset();
/// Recomputes the rope at `pos` now (state / break). Returns false if it is not a rope.
bool update(BlockSource& region, BlockPos const& pos);
/// A rope block shaped for `pos` (nullptr if the rope block is not registered).
[[nodiscard]] Block const* shapedRope(BlockSource& region, BlockPos const& pos);
/// Distance the rope at `pos` would have (7 = unsupported).
[[nodiscard]] int distanceAt(BlockSource& region, BlockPos const& pos);
/// The lowest rope of the column going down from `pos` (pos itself when the block below isn't a rope).
[[nodiscard]] BlockPos bottomOf(BlockSource& region, BlockPos const& pos);
/// Places a rope under the rope column containing `pos`. Returns the new rope's position.
std::optional<BlockPos> addRopeDown(BlockSource& region, BlockPos const& pos);
/// Removes the lowest rope of the column starting at `pos` (only if it is not `pos` itself).
std::optional<BlockPos> removeRopeDown(BlockSource& region, BlockPos const& pos, bool allowSelf = false);
/// Rings a bell the rope/chain column at `pos` hangs from. Returns true if a bell rang.
bool ringConnectedBell(BlockSource& region, BlockPos const& pos, Actor* by);
/// Handles the use of a rope (with `held` in hand). Returns true when the click is consumed.
bool onUse(Player& player, BlockPos const& pos, int face, ItemStack const& held);
/// Rope item used on any block: true when the vanilla placement must be refused.
bool refusePlacement(Player& player, BlockPos const& clicked, int face);
} // namespace ropes

namespace pulley {
void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& prev, Block const& next);
bool onUse(Player& player, BlockPos const& pos);
void onCrankUsed(Player& player, BlockPos const& crankPos, Block const& crank);
void reset();
struct Content {
    std::string item;
    int         count = 0;
};
[[nodiscard]] Content contentAt(core::GlobalPos const& pos);
void setContent(core::GlobalPos const& pos, Content const& c);
} // namespace pulley

namespace flax {
void install();
void uninstall();
void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& prev, Block const& next);
void tick();
void load();
void save();
void reset();
/// Bone meal on a flax crop. Returns true if it grew.
bool boneMeal(BlockSource& region, BlockPos const& pos);
/// One random growth tick of the crop at `pos` (forced = ignore the chance).
bool growthTick(BlockSource& region, BlockPos const& pos, bool forced);
/// Marks a position whose next removal must not drop items (creative players).
void noDrops(core::GlobalPos const& pos);
[[nodiscard]] std::size_t tracked();
} // namespace flax

namespace arrow {
void install();
void uninstall();
void tick();
void load();
void save();
void reset();
/// Rope arrow used on a rope: winds the column into the arrow. True when consumed.
bool onUseOnRope(Player& player, BlockPos const& pos);
/// Spawns a rope arrow shot by `shooter` (bow logic), `power` 0..1. Returns the entity.
Actor* shoot(Player& shooter, int charges, float power, bool creative);
/// Marks a spawned entity as a rope arrow with `charges`.
void track(Actor& arrow, int charges, bool pickup);
/// Charges stored for a tracked arrow entity (-1 if untracked).
[[nodiscard]] int chargesOf(Actor const& arrow);
/// Converts crafted rope arrows (aux = charges) into durability.
void normalize(Player& player);
/// Handles a rope arrow hitting `hit` on `face`.
void onHitBlock(Actor& arrow, BlockPos const& hit, int face);
} // namespace arrow

} // namespace cm::feature::rope
