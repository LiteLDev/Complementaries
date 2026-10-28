#pragma once

// Internal interface between the tweak module (event wiring) and the individual tweaks.

#include <optional>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockSource;
class HitResult;
class Player;

namespace cm::feature::tweak {

/// A player using an item on a block face (PlayerInteractBlockEvent).
struct UseOn {
    Player&            player;
    ItemStack const&   item;
    std::string const& itemId; ///< "" for an empty hand
    BlockPos           pos;
    int                face; ///< Bedrock face: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
    Block const&       block;
    Vec3               clickPos;
};

// ---- block ids ------------------------------------------------------------------------------------
inline constexpr char kStickBlock[]      = "complementaries:stick";
inline constexpr char kBlazeRodBlock[]   = "complementaries:blaze_rod";
inline constexpr char kBreezeRodBlock[]  = "complementaries:breeze_rod";
inline constexpr char kGunpowderBlock[]  = "complementaries:gunpowder";
inline constexpr char kHangingPotBlock[] = "complementaries:hanging_flower_pot";
inline constexpr char kRopeBlock[]       = "complementaries:rope"; ///< owned by the rope unit
inline constexpr char kBrickEntity[]     = "complementaries:throwable_brick";
inline constexpr char kSlimeballEntity[] = "complementaries:throwable_slimeball";

/// Iron / copper chains (any oxidation, waxed or not), the pre-1.21.110 `minecraft:chain`.
[[nodiscard]] bool isChain(std::string const& blockId);

/// What the PlayerInteractBlockEvent / PlayerUseItemEvent listeners run (selftests call them directly:
/// simulated players don't go through the network handlers that fire those events).
/// True when a tweak took the click.
bool dispatchUseOn(Player& player, ItemStack const& item, BlockPos const& pos, int face, Vec3 const& clickPos);
bool dispatchUse(Player& player, ItemStack const& item);

// ---- interactions: each returns true when it took the click (the event is cancelled) ---------------
bool sticksUseOn(UseOn const& use);
bool gunpowderUseOn(UseOn const& use);
bool hangingPotUseOn(UseOn const& use);
bool bellChainUseOn(UseOn const& use);
bool bottleXpUseOn(UseOn const& use);
bool cauldronUseOn(UseOn const& use);
bool throwUseOn(UseOn const& use);

/// Using an item in the air (PlayerUseItemEvent). True when handled.
bool throwUse(Player& player, std::string const& itemId);
bool infoUse(Player& player, std::string const& itemId);

/// A player interacting with a mob. True when handled (vanilla interaction skipped).
bool zombieHorseInteract(Player& player, Actor& target);

/// A projectile hit. True when the hit is fully handled (the vanilla hit is skipped).
bool projectileHit(Actor& projectile, HitResult const& hit);

// ---- drops ------------------------------------------------------------------------------------------
/// The items a block of this unit drops (empty for other blocks).
[[nodiscard]] std::vector<ItemStack> dropsOf(Block const& block);
/// The next removal of the block at `pos` drops nothing (burnt gunpowder, creative breaking).
void suppressDrops(core::GlobalPos const& pos);
void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now);
void clearDropSuppression();

// ---- support (hanging pots need a block above, trails a solid block below) ----------------------------
void supportBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now);
void supportTick();
void supportClear();

// ---- sticks -----------------------------------------------------------------------------------------
/// The block of the stick kind of `itemId` when that kind is enabled ("" otherwise).
[[nodiscard]] std::string stickBlockFor(std::string const& itemId);
/// The item of a stick block id ("" for other blocks).
[[nodiscard]] std::string stickItemFor(std::string const& blockId);

// ---- gunpowder --------------------------------------------------------------------------------------
[[nodiscard]] bool isTrail(Block const& block);
/// Recomputes the connections of the trail at `pos` (no-op for other blocks).
void refreshTrail(BlockSource& region, BlockPos const& pos);
/// Places a trail with its connections and updates the neighbours. False when not possible.
bool placeTrail(BlockSource& region, BlockPos const& pos);
/// Lights the trail at `pos` (no-op when it is not an unlit trail).
void igniteTrail(BlockSource& region, BlockPos const& pos);
/// Lights whatever burnable thing is at `pos` (trail, TNT, campfire, candle).
void igniteAt(BlockSource& region, BlockPos const& pos);
void gunpowderBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now);
void gunpowderTick();
void gunpowderLoad();
void gunpowderSave();
void gunpowderClear();
[[nodiscard]] std::size_t burningCount();

// ---- hanging flower pots ----------------------------------------------------------------------------
/// The plant index (1..) of a plant item, 0 when it cannot go into a hanging pot.
[[nodiscard]] int plantIndex(std::string const& itemId);
[[nodiscard]] std::string plantItem(int index);
[[nodiscard]] int plantCount();
/// The plant index stored in a hanging pot block (0 = empty).
[[nodiscard]] int potPlant(Block const& block);
/// `block` (a hanging pot) holding plant `index`.
[[nodiscard]] Block const* withPotPlant(Block const& block, int index);

// ---- bells ------------------------------------------------------------------------------------------
/// Rings the bell at the end of the chain / rope column through `pos`. False when there is none.
bool ringThroughChain(BlockSource& region, BlockPos const& pos, Actor* source);

// ---- cauldron ---------------------------------------------------------------------------------------
/// The dye colour (0 white .. 15 black) closest to an ARGB colour.
[[nodiscard]] int nearestDye(int argb);
/// `itemId` recoloured to `dye` ("" when the item is not dyeable). `aux` is in/out (beds).
[[nodiscard]] std::string recolor(std::string const& itemId, int dye, int& aux);

// ---- info -------------------------------------------------------------------------------------------
[[nodiscard]] std::string clockText(int dayTicks);

// ---- bottling ---------------------------------------------------------------------------------------
/// Total experience points of a player.
[[nodiscard]] long long totalXp(Player& player);

} // namespace cm::feature::tweak
