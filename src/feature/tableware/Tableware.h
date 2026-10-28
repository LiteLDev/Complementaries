#pragma once

// U13 tableware: goblet, urn, planter, flower box, book piles, pancake, candy, jar boat.
// Behaviour: docs/spec/tableware.md. Everything is server side (the integrated server on the
// client); the visuals are data driven by block states, so vanilla clients see them too.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mc/common/FacingID.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

class BlockSource;
class ItemStack;
class Player;

namespace cm::feature::tableware {

struct TablewareConfig {
    int                      version              = 1;
    bool                     placeableBooks       = true;
    bool                     writtenBooks         = true;
    bool                     mixedBooks           = false;
    float                    bookPower            = 0.333f;
    float                    enchantedBookPower   = 0.6f;
    float                    urnEntitySpawnChance = 0.1f;
    std::vector<std::string> urnSpawnMobs{"minecraft:silverfish"};
    bool                     planterGrowthBoost     = true;
    float                    jarBoatShipwreckChance = 0.25f;
};

[[nodiscard]] TablewareConfig const& config();

// ---- ids ------------------------------------------------------------------------------------------
inline constexpr std::string_view kGoblet         = "complementaries:goblet";
inline constexpr std::string_view kUrn            = "complementaries:urn";
inline constexpr std::string_view kPlanter        = "complementaries:planter";
inline constexpr std::string_view kFlowerBox      = "complementaries:flower_box";
inline constexpr std::string_view kBookPile       = "complementaries:book_pile";
inline constexpr std::string_view kBookPileLying  = "complementaries:book_pile_horizontal";
inline constexpr std::string_view kPancake        = "complementaries:pancake";
inline constexpr std::string_view kJarBoat        = "complementaries:jar_boat";
inline constexpr std::string_view kCandy          = "complementaries:candy";

// ---- block helpers ----------------------------------------------------------------------------------
using StateValue = Block::BlockStateValueType;
using StateList  = std::vector<std::pair<std::string, StateValue>>;

[[nodiscard]] bool is(Block const& block, std::string_view id);
/// Integer custom state (`def` when missing).
[[nodiscard]] int state(Block const& block, std::string_view name, int def = 0);
/// `minecraft:cardinal_direction` ("north", ...; "south" when missing).
[[nodiscard]] std::string direction(Block const& block);
/// `block` with some states changed (nullptr if the result does not exist).
[[nodiscard]] Block const* with(Block const& block, StateList const& changes);
/// The block `id` with `states` (others default).
[[nodiscard]] Block const* make(std::string_view id, StateList const& states = {});
bool place(BlockSource& region, BlockPos const& pos, Block const& block);
/// Unit offset of a cardinal direction name.
[[nodiscard]] std::pair<int, int> offsetOf(std::string_view dir);
/// The direction name facing `face` (side faces only, "" for up/down).
[[nodiscard]] std::string directionOfFace(FacingID face);
/// Cardinal direction from the player to... the opposite of where the player looks (a placed
/// block's front faces the player).
[[nodiscard]] std::string directionTowards(Player const& player);

// ---- item helpers -----------------------------------------------------------------------------------
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1, int aux = 0);
[[nodiscard]] bool      isEmpty(ItemStack const& item);
[[nodiscard]] std::string itemId(ItemStack const& item); ///< "" for an empty stack
[[nodiscard]] bool      creative(Player const& player);
/// Removes `n` of the held item (not in creative).
void consumeHeld(Player& player, int n = 1);
/// Uses up one held item and hands out `result` (bottle exchange; creative keeps the held item and
/// still receives `result` only when it doesn't have one).
void exchangeHeld(Player& player, ItemStack const& result);
void giveOrDrop(Player& player, ItemStack item);
[[nodiscard]] std::string itemToSnbt(ItemStack const& item);
[[nodiscard]] std::optional<ItemStack> itemFromSnbt(std::string const& snbt);

// ---- world helpers ----------------------------------------------------------------------------------
/// `cm:tableware:<kind>:<dim>:<x>:<y>:<z>`
[[nodiscard]] std::string posKey(std::string_view kind, int dim, BlockPos const& pos);
/// Runs `fn` on the next server tick.
void defer(std::function<void()> fn);
/// Spawns items / a mob at a block on the next tick (dimension looked up again then).
void dropLater(int dim, BlockPos const& pos, std::vector<ItemStack> items);
void spawnMobLater(int dim, BlockPos const& pos, std::string const& id);
void playSound(Player& player, int levelSoundEvent, BlockPos const& pos, int data = -1);
[[nodiscard]] float randomFloat();
[[nodiscard]] int   randomInt(int bound);

// ---- interactions -----------------------------------------------------------------------------------
struct UseContext {
    Player&          player;
    BlockSource&     region;
    BlockPos         pos;
    Block const&     block;
    ItemStack const& held;
    FacingID         face;
    Vec3             hit; ///< position inside the block, 0..1 on each axis
};

/// Handles a right click of `player` on `pos`. Returns true when the click is ours (the caller
/// cancels the vanilla interaction). With `immediate` the effect is applied now (tests), otherwise
/// on the next server tick.
bool handleUse(Player& player, BlockPos const& pos, FacingID face, Vec3 const& click, bool immediate);

/// A handler checks (`apply` false) or performs (`apply` true) an interaction.
using UseHandler = bool (*)(UseContext const& ctx, bool apply);

namespace goblet {
bool onUse(UseContext const& ctx, bool apply);
void onRemoved(int dim, BlockPos const& pos);
/// Fluid state for a bottle item ("" item → 0 = not a goblet fluid).
[[nodiscard]] int fluidOf(ItemStack const& item);
} // namespace goblet

namespace flower_box {
bool onUse(UseContext const& ctx, bool apply);
void onRemoved(int dim, BlockPos const& pos, Block const& prev);
/// 1..15 for supported plants, 0 otherwise.
[[nodiscard]] int plantOf(std::string_view itemId);
[[nodiscard]] std::string_view plantItem(int value);
void onPlaced(Player& player, BlockPos const& pos, FacingID face);
} // namespace flower_box

namespace book_pile {
bool onUse(UseContext const& ctx, bool apply);
bool onPlaceUse(UseContext const& ctx, bool apply);
void onRemoved(int dim, BlockPos const& pos, Block const& prev);
/// Enchanting power of the book piles around an enchanting table.
[[nodiscard]] float enchantPower(BlockSource& region, BlockPos const& table);
void installHooks();
void uninstallHooks();
} // namespace book_pile

namespace pancake {
bool onUse(UseContext const& ctx, bool apply);
} // namespace pancake

namespace urn {
void onRemoved(int dim, BlockPos const& pos, Block const& prev);
/// Mobs released by treasure urns so far (tests).
[[nodiscard]] int releases();
void installHooks();
void uninstallHooks();
} // namespace urn

namespace planter {
/// A plant at `plantPos` (or with its support at `pos`) stands on a planter.
[[nodiscard]] bool supports(BlockSource const& region, BlockPos const& pos);
void installHooks();
void uninstallHooks();
} // namespace planter

namespace jar_boat {
void installHooks();
void uninstallHooks();
} // namespace jar_boat

} // namespace cm::feature::tableware
