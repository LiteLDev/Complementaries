#pragma once

// Shared helpers of the storage unit (sack, presents, quiver, lunch basket): ids, item user data,
// content records in the world DB and small player helpers. Server thread only.

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class CompoundTag;
class ItemStack;
class Player;
class Vec3;

namespace cm::feature::storage {

// ---- ids -------------------------------------------------------------------------------------------

inline constexpr std::string_view kSack        = "complementaries:sack";
inline constexpr std::string_view kQuiver      = "complementaries:quiver";
inline constexpr std::string_view kLunchBasket = "complementaries:lunch_basket";
inline constexpr std::string_view kViewEntity  = "complementaries:storage_view";
inline constexpr std::string_view kStateOpen   = "complementaries:open";
inline constexpr std::string_view kStatePacked = "complementaries:packed";

/// The 16 dye colours in Java order ("white" ... "black").
extern std::array<std::string_view, 16> const kColors;

enum class Kind { None, Sack, Present, TrappedPresent };

/// Kind of a block or item id ("complementaries:present_red" -> Present).
[[nodiscard]] Kind kindOf(std::string_view id);
/// Present colour of an id: "" (undyed) or "red", ... (nullopt if not a present id).
[[nodiscard]] std::optional<std::string> presentColor(std::string_view id);
/// The present id for `trapped` and `color` ("" = undyed).
[[nodiscard]] std::string presentId(bool trapped, std::string_view color);

// ---- items -----------------------------------------------------------------------------------------

[[nodiscard]] ItemStack makeStack(std::string_view id, int count = 1, int aux = 0);
[[nodiscard]] std::string itemId(ItemStack const& stack);
[[nodiscard]] bool        isEmpty(ItemStack const& stack);
[[nodiscard]] int         countOf(ItemStack const& stack);

/// SNBT of a whole stack (id, count, aux, user data) and back. Empty string <-> empty stack.
[[nodiscard]] std::string stackToSnbt(ItemStack const& stack);
[[nodiscard]] ItemStack   stackFromSnbt(std::string const& snbt);

/// The `cm_storage` compound of an item's user data (nullptr when absent).
[[nodiscard]] CompoundTag const* storageTag(ItemStack const& stack);
/// The `cm_storage` compound, created if missing.
CompoundTag& storageTagMut(ItemStack& stack);
[[nodiscard]] std::string tagString(ItemStack const& stack, std::string_view key);
[[nodiscard]] int         tagInt(ItemStack const& stack, std::string_view key, int fallback = 0);
void                      setTagString(ItemStack& stack, std::string_view key, std::string const& value);
void                      setTagInt(ItemStack& stack, std::string_view key, int value);

/// The content uid of an item ("" when it has none) / assigns one if missing.
[[nodiscard]] std::string itemUid(ItemStack const& stack);
std::string               ensureUid(ItemStack& stack);
[[nodiscard]] std::string newUid();

// ---- content records ("cm:storage:inv:<uid>") -----------------------------------------------------

/// The stacks of a content record, resized to `size` (missing record = all empty).
[[nodiscard]] std::vector<ItemStack> loadContents(std::string const& uid, int size);
void                                 saveContents(std::string const& uid, std::vector<ItemStack> const& items);
void                                 eraseContents(std::string const& uid);
[[nodiscard]] bool                   contentsEmpty(std::vector<ItemStack> const& items);
/// Forgets cached records (world closed).
void clearCaches();

/// Lore lines describing `items` (Supplementaries' shulker-style tooltip: 5 lines, then "and N more").
/// `selected` >= 0 marks that slot with "> ".
[[nodiscard]] std::vector<std::string> contentLore(std::vector<ItemStack> const& items, int selected = -1);
/// Display name of a stack ("Arrow of Swiftness").
[[nodiscard]] std::string stackName(ItemStack const& stack);

// ---- block records ("cm:storage:block:<dim>:<x>:<y>:<z>") -----------------------------------------

struct BlockRecord {
    std::string uid;
    bool        packed = false;
    std::string recipient;
    std::string sender;
    std::string description;
};
[[nodiscard]] std::optional<BlockRecord> loadBlockRecord(core::GlobalPos const& pos);
void                                     saveBlockRecord(core::GlobalPos const& pos, BlockRecord const& rec);
void                                     eraseBlockRecord(core::GlobalPos const& pos);
/// The record of the block at `pos`, created (with a fresh uid) when missing.
[[nodiscard]] BlockRecord blockRecordOrNew(core::GlobalPos const& pos);

// ---- blocks ----------------------------------------------------------------------------------------

[[nodiscard]] Kind kindOf(Block const& block);
[[nodiscard]] bool boolState(Block const& block, std::string_view state);
/// Sets a bool state of the block at `pos` (keeps the block id and other states).
void setBoolState(BlockSource& region, BlockPos const& pos, std::string_view state, bool value);
[[nodiscard]] BlockSource* regionOf(int dim);

// ---- players ---------------------------------------------------------------------------------------

void actionbar(Player& player, std::string const& text);
/// Adds the stack to the player's inventory, dropping what does not fit at their feet.
void giveOrDrop(Player& player, ItemStack stack);
void spawnItem(BlockSource& region, ItemStack const& stack, Vec3 const& pos);
void playSound(BlockSource& region, Vec3 const& pos, std::string const& sound);
/// Finds a player by unique id (nullptr if gone).
[[nodiscard]] Player* playerById(long long uniqueId);
[[nodiscard]] long long playerKey(Player const& player);
/// Every inventory slot of a player: 0..size-1 = inventory, -1 = off-hand. `fn(slot, stack)`;
/// returning true stops the iteration.
void forEachSlot(Player& player, std::function<bool(int, ItemStack const&)> const& fn);
[[nodiscard]] ItemStack const& slotItem(Player& player, int slot);
void                           setSlotItem(Player& player, int slot, ItemStack const& stack);

} // namespace cm::feature::storage
