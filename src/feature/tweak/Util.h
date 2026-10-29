#pragma once

// Small game helpers shared by the tweaks (server thread only).

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

class Block;
class BlockSource;
class Player;
class Vec3;

namespace cm::feature::tweak {

using Sound = SharedTypes::Legacy::LevelSoundEvent;

/// A stack of `id` ("minecraft:stick"); null when the id is unknown.
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1, int aux = 0);
/// "minecraft:stick" style id of a stack ("" for an empty stack).
[[nodiscard]] std::string itemId(ItemStack const& item);
/// "minecraft:stone" style id of a block.
[[nodiscard]] std::string const& blockName(Block const& block);

/// Removes `n` items from the selected hotbar slot (creative players keep their items).
void consumeSelected(Player& player, int n = 1);
/// Replaces the selected stack (and syncs the inventory).
void setSelected(Player& player, ItemStack const& item);
/// Gives `item` to the player, dropping what does not fit.
void giveOrDrop(Player& player, ItemStack item);
/// Drops an item entity at the centre of `pos`.
void dropItem(BlockSource& region, BlockPos const& pos, ItemStack const& item);

/// Plays a level sound for everyone around `pos`.
void playSound(BlockSource& region, Sound sound, Vec3 const& pos, int data = -1);
/// Plays the sound of `block` (place / break / hit).
void playBlockSound(BlockSource& region, Sound sound, BlockPos const& pos, Block const& block);
/// Shows `text` above the hotbar of `player`.
void showTip(Player const& player, std::string const& text);
/// Runs a command as the server (no output).
void runCommand(std::string const& command);

/// The block source of dimension `dim` (nullptr when not loaded).
[[nodiscard]] BlockSource* regionOf(int dim);
/// The player with this unique id (nullptr when gone).
[[nodiscard]] Player* findPlayer(ActorUniqueID id);

/// Runs `fn` on the next server tick (world edits after a cancelled interaction are rolled back).
void defer(std::function<void()> fn);
/// Runs the deferred work (called by the module every server tick).
void runDeferred();
/// Drops the deferred work (world closed).
void clearDeferred();
/// Server ticks since the mod was enabled (advanced by the module every tick).
[[nodiscard]] long long currentTick();
void advanceTick();

/// The block next to `pos` through Bedrock face `face` (0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
[[nodiscard]] BlockPos offset(BlockPos const& pos, int face);
/// The axis of a face: 0 X, 1 Y, 2 Z.
[[nodiscard]] int faceAxis(int face);
/// `block` with one integer / boolean state changed (nullptr if it has no such state).
[[nodiscard]] Block const* withState(Block const& block, std::string_view state, int value);
/// Solid, full blocks (support for trails, blocking for connections).
[[nodiscard]] bool isSolid(Block const& block);
/// The block type has its own use action (chests, doors, ...).
[[nodiscard]] bool isInteractive(Block const& block);

} // namespace cm::feature::tweak
