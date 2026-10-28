#pragma once

// U12 display: pedestal, item shelf, statue (blocks holding one item) and the hat stand entity.
//
// Bedrock custom blocks have no block entities: the displayed item of a block lives in the world
// DB (`cm:display:<dim>:<x>:<y>:<z>`) and in memory; a helper entity `complementaries:item_display`
// at the block holds a copy of the item in its main hand so every client (modded or vanilla) renders
// it with the resource pack's animations.

#include <optional>
#include <string>
#include <string_view>

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace cm::feature::display {

struct DisplayConfig {
    int    version              = 1;
    double crystalEnchanting    = 3.0;  ///< bookshelves of power of an end crystal on a pedestal
    bool   pedestalSpin         = true; ///< items on pedestals spin
    bool   pedestalSpecial      = true; ///< swords, tridents, end crystals get special poses
    bool   shelfTranslate       = true; ///< block items on shelves sit on the shelf
    bool   hatStandUnrestricted = false; ///< any item can go on a hat stand
    bool   hatStandSkibidi      = true; ///< hat stands in cauldrons & co. dance
};

inline constexpr std::string_view kPedestal      = "complementaries:pedestal";
inline constexpr std::string_view kItemShelf     = "complementaries:item_shelf";
inline constexpr std::string_view kStatue        = "complementaries:statue";
inline constexpr std::string_view kHatStand      = "complementaries:hat_stand";
inline constexpr std::string_view kDisplayEntity = "complementaries:item_display";

enum class Kind { None = 0, Pedestal = 1, Shelf = 2, Statue = 3 };

/// Pedestal `item_status` (Java DisplayStatus): none = cannot hold an item.
enum class PedestalStatus { None = 0, Empty = 1, Full = 2 };

/// Pedestal display types (Java PedestalBlockTile.DisplayType, globes not ported).
enum class PedestalType { Item, Block, Sword, Trident, Crystal };

/// Statue poses (Java StatueBlockTile.StatuePose, globes count as holding).
enum class StatuePose { Standing = 0, Holding = 1, Candle = 2, Sword = 3, Tool = 4 };

/// Helper entity modes (entity property `complementaries:mode`, see the RP client entity).
enum class DisplayMode {
    None          = 0,
    PedestalSpin  = 1,
    PedestalStill = 2,
    PedestalBlock = 3,
    PedestalBlockStill = 4,
    PedestalSword = 5,
    PedestalTrident = 6,
    PedestalCrystal = 7,
    Shelf         = 8,
    ShelfBlock    = 9,
    Statue        = 10,
};

[[nodiscard]] Kind kindOf(Block const& block);
[[nodiscard]] std::string_view blockIdOf(Kind kind);

// ---- classification (pure on item ids, see Display.cpp) --------------------------------------------
[[nodiscard]] PedestalType pedestalTypeOf(ItemStack const& item);
[[nodiscard]] StatuePose   statuePoseOf(ItemStack const& item);
[[nodiscard]] bool         isSwordId(std::string_view id);
[[nodiscard]] bool         isToolId(std::string_view id);
[[nodiscard]] bool         isCandleId(std::string_view id);

// ---- state of the display blocks (server thread) ---------------------------------------------------
/// The item displayed at `pos` (nullopt when empty / not a display block).
[[nodiscard]] std::optional<ItemStack> displayedItem(core::GlobalPos const& pos);
/// Comparator output of the display block at `pos` (15 with an item, else 0).
[[nodiscard]] int comparatorSignal(core::GlobalPos const& pos);

enum class UseResult { Pass, Placed, Taken };
/// Right click on the display block at `pos` with `hand` (the selected stack, changed in place).
/// Mirrors ItemDisplayTile#interactWithPlayerItem. Does world edits: call it outside a cancelled
/// interaction event (the module defers it by one tick).
UseResult use(Player& player, BlockSource& region, BlockPos const& pos);

/// Puts `item` (one of it) on the display block directly (tests, dispensers). False if not possible.
bool putItem(BlockSource& region, BlockPos const& pos, ItemStack const& item);
/// Removes and returns the displayed item.
std::optional<ItemStack> takeItem(BlockSource& region, BlockPos const& pos);

/// Re-evaluates block states (pedestal connections, statue lit/powered) and the helper entity now.
void refresh(BlockSource& region, BlockPos const& pos);
/// The helper entity at `pos` (nullptr if none).
[[nodiscard]] Actor* displayEntityAt(BlockSource& region, BlockPos const& pos);

/// Pedestal `item_status` computed like Java PedestalBlock#getStatus.
[[nodiscard]] PedestalStatus pedestalStatusFor(BlockSource& region, BlockPos const& pos, bool hasItem);

/// Java horizontal facing (0 S, 1 W, 2 N, 3 E) of an item shelf / statue block.
[[nodiscard]] int blockFacing(Block const& block);
/// A string block state ("" when missing).
[[nodiscard]] std::string stringState(Block const& block, std::string_view name);

// ---- hat stand (HatStand.cpp) -----------------------------------------------------------------------
/// Places a hat stand like HatStandItem#useOn. Returns the entity or nullptr.
Actor* placeHatStand(Player* player, BlockSource& region, BlockPos const& clicked, int face, float playerYaw);
/// Right click on a hat stand (HatStandEntity#interactAt). True if something happened.
bool useHatStand(Player& player, Actor& stand);
/// Damage handling (HatStandEntity#hurt). Returns true when the stand took the hit itself.
bool hurtHatStand(Actor& stand, Actor* attacker, bool creativeAttacker, int cause);
/// Breaks the stand, dropping itself and its hat when `drops`.
void dismantleHatStand(Actor& stand, bool drops);
[[nodiscard]] bool isHatStand(Actor const& actor);
[[nodiscard]] bool canWearOnHatStand(ItemStack const& item);

// ---- helpers shared by the unit (Util.cpp) ------------------------------------------------------------
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1, int aux = 0);
[[nodiscard]] std::string itemId(ItemStack const& item);
[[nodiscard]] std::string itemToSnbt(ItemStack const& item);
[[nodiscard]] std::optional<ItemStack> itemFromSnbt(std::string const& snbt);
/// Plays a named sound at a block position for every player of the dimension nearby.
void playSound(BlockSource& region, float x, float y, float z, std::string const& name, float volume = 1.0f, float pitch = 1.0f);
/// Triggers a BP entity event (`complementaries:<name>`) on `actor`.
void entityEvent(Actor& actor, std::string const& name);
/// Gives `item` to the player (selected slot if empty, else inventory, else dropped at `dropAt`).
void giveToPlayer(Player& player, ItemStack const& item);
/// Drops `item` at the centre of a block.
void dropItem(BlockSource& region, float x, float y, float z, ItemStack const& item);
[[nodiscard]] bool isCreative(Player const& player);
[[nodiscard]] int currentTick();

} // namespace cm::feature::display
