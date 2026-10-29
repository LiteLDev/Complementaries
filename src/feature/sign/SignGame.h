#pragma once

// Game adapters of the sign unit (server thread): items, sounds, drops and the display entities.

#include <cstdint>
#include <string>
#include <string_view>

class Actor;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;
class Vec3;

namespace cm::feature::sign {

/// An item stack by id ("minecraft:compass"). Empty stack if the id is unknown.
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1);
/// The id of a stack ("" when empty).
[[nodiscard]] std::string itemId(ItemStack const& item);

/// The block source of a dimension (nullptr if the dimension isn't loaded).
[[nodiscard]] BlockSource* regionOf(int dim);
/// The chunk holding `pos` is loaded.
[[nodiscard]] bool loadedAt(BlockSource& region, BlockPos const& pos);

/// Drops an item entity at `pos`.
void dropItem(BlockSource& region, Vec3 const& pos, ItemStack const& item);
/// Plays a named sound (resource pack sound id) to the players near `pos`.
void playSound(int dim, Vec3 const& pos, std::string const& sound, float volume = 1.0f, float pitch = 1.0f);

/// Consumes one item from the player's selected slot (nothing in creative).
void consumeSelected(Player& player);
/// Damages the selected tool by one (nothing in creative).
void damageSelected(Player& player);

// ---- display entities ---------------------------------------------------------------------------
/// Spawns a display entity; returns its unique id (0 on failure).
[[nodiscard]] std::int64_t spawnDisplay(BlockSource& region, std::string_view type, Vec3 const& pos);
/// The loaded actor with unique id `id` (nullptr when absent / removed).
[[nodiscard]] Actor* findActor(std::int64_t id);
/// Writes the synched data of a display entity (only what changed is sent).
void applyDisplay(Actor& actor, Vec3 const& pos, int variant, int markVariant, std::string const& name);
/// Removes an actor from the world.
void removeActor(Actor& actor);

} // namespace cm::feature::sign
