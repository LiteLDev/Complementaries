#pragma once

// Feature entry points of the storage unit, called by the module's event handlers (Storage.cpp)
// and by the selftests.

#include <string>

#include "core/GlobalPos.h"
#include "feature/storage/Common.h"

class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace cm::feature::storage {

/// `config/storage.json`
struct StorageConfig {
    int    version                   = 1;
    bool   sackPenalty               = true;
    int    sackIncrement             = 2;
    int    sackSlots                 = 9;
    int    quiverSlots               = 6;
    double quiverSkeletonSpawnChance = 0.03;
    int    lunchBasketSlots          = 6;
    bool   presentsAllowDyeing       = true;
};
[[nodiscard]] StorageConfig const& config();
[[nodiscard]] int                  sackSlots();
[[nodiscard]] int                  quiverSlots();
[[nodiscard]] int                  basketSlots();

// ---- placed blocks (Blocks.cpp) -------------------------------------------------------------------

/// A sack / present was placed from `item`: moves the item's uid and present data to the block.
void onBlockPlaced(core::GlobalPos const& pos, ItemStack const& item);
/// A sack / present block (`oldId`) disappeared: drops it with its contents (see the spec).
void onBlockRemoved(core::GlobalPos const& pos, std::string const& oldId, bool creativeBreak);
/// A piston moved the block away (`minecraft:moving_block`); called once the push is over.
void onBlockPushed(core::GlobalPos const& pos, std::string const& oldId);
/// Lets the sack at `pos` fall when nothing supports it. Returns the new position.
core::GlobalPos checkFall(core::GlobalPos const& pos);
/// True while this unit moves / recolours its own blocks (not a removal).
[[nodiscard]] bool movingBlocks();
/// Removes one of our blocks with its record and contents, without drops (selftest cleanup).
void removeQuietly(core::GlobalPos const& pos);
/// Right-click on one of our blocks (deferred out of the interact event).
void interactBlock(Player& player, core::GlobalPos const& pos);
/// The item a block drops (uid, present data, lore); empty unpacked blocks give a plain item.
[[nodiscard]] ItemStack blockItem(std::string const& id, BlockRecord const* rec);

bool openSack(Player& player, core::GlobalPos const& pos);
[[nodiscard]] bool canOpenPresent(Player& player, BlockRecord const& rec);
enum class PresentAction { Keep = 0, Put = 1, Take = 2 };
/// The present screen's submit: content action, recipient, description, pack.
void applyPresentForm(Player& player, core::GlobalPos const& pos, PresentAction action, std::string const& recipient,
                      std::string const& description, bool pack);
/// Opens a packed trapped present: activates its content upwards.
void triggerTrappedPresent(core::GlobalPos const& pos);
/// Right-click with a dye: recolours the present. True if it changed.
bool dyePresent(Player& player, core::GlobalPos const& pos, std::string const& color);

/// Overencumbered: sacks carried and the effect check (every 27 ticks).
[[nodiscard]] int sackCount(Player& player);
void              overencumberedTick(Player& player);

// ---- selectable container items (Items.cpp) -------------------------------------------------------

[[nodiscard]] bool isArrow(ItemStack const& stack);
[[nodiscard]] bool isFood(ItemStack const& stack);
[[nodiscard]] bool isRangedWeapon(ItemStack const& stack);
/// Inventory slot of the first quiver (-1 off-hand, -2 none).
[[nodiscard]] int findItem(Player& player, std::string_view id);
/// The selected slot of a quiver / basket (moved to the next non-empty slot if needed; -1 = empty).
[[nodiscard]] int selectedSlot(std::vector<ItemStack> const& items, int sel);
/// Rewrites the lore of a sack / present / quiver / basket item from its record (`owner`: the player
/// holding it, for the quiver's lent arrows).
void updateLore(ItemStack& stack, Player* owner = nullptr);
/// Rewrites the lore of every item with `uid` in the player's inventory.
void refreshItemLore(Player& player, std::string const& uid);
/// Next non-empty slot (sneak + use / sneak + swing); shows it on the action bar.
void cycleSelection(Player& player, int slot);
/// Opens the quiver / basket screen for the item in `slot`.
bool openItemScreen(Player& player, int slot);

/// The quiver lends its selected arrows to the off-hand while a bow / crossbow is held.
void quiverTick(Player& player);
/// Puts every lent stack back into its quiver (disconnect, before opening the screen, ...).
void returnProxies(Player& player);

/// The basket in `slot` has a selected food the player can eat now (else its screen opens instead).
[[nodiscard]] bool basketCanEat(Player& player, int slot);
/// Eats the selected food of the basket in `basket` (end of the eating animation).
bool basketEat(Player& player, ItemStack& basket);

/// A skeleton with a quiver died: its quiver (with a few arrows).
[[nodiscard]] ItemStack skeletonQuiver();

} // namespace cm::feature::storage
