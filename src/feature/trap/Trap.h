#pragma once

// U17 trap: bamboo spikes (plain and tipped), cage, fodder. Internal declarations shared by the
// unit's .cpp files. See docs/spec/trap.md.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class BlockType;
class CompoundTag;
class ItemStack;
class Level;
class Mob;
class Player;
class Vec3;

namespace cm::feature::trap {

// ---- config ---------------------------------------------------------------------------------

struct SpikesConfig {
    bool enabled                 = true;
    bool tippedSpikes            = true;
    bool playerLoot              = false;
    bool onlyAllowHarmfulEffects = true;
};

struct CageConfig {
    bool                     enabled         = true;
    bool                     allowAllMobs    = false;
    bool                     allowAllBabies  = false;
    bool                     autoDetect      = false;
    bool                     persistentMobs  = false;
    int                      healthThreshold = 100;
    bool                     requireTaming   = true;
    std::vector<std::string> catchable{
        "minecraft:slime",    "minecraft:magma_cube", "minecraft:vex",    "minecraft:bee",       "minecraft:frog",
        "minecraft:tadpole",  "minecraft:allay",      "minecraft:endermite", "minecraft:silverfish", "minecraft:parrot",
        "minecraft:rabbit",   "minecraft:cat",        "minecraft:chicken", "minecraft:armadillo", "minecraft:bat",
        "minecraft:fox",      "minecraft:ocelot",     "minecraft:axolotl", "minecraft:cod",       "minecraft:salmon",
        "minecraft:pufferfish", "minecraft:tropicalfish"
    };
    std::vector<std::string> babyCatchable{"minecraft:cow", "minecraft:sheep", "minecraft:pig", "minecraft:goat"};
};

struct FodderConfig {
    bool                     enabled = true;
    std::vector<std::string> eaters{
        "minecraft:pig",
        "minecraft:cow",
        "minecraft:sheep",
        "minecraft:chicken",
        "minecraft:goat",
        "minecraft:mooshroom"
    };
};

struct TrapConfig {
    int          version = 1;
    SpikesConfig bambooSpikes;
    CageConfig   cage;
    FodderConfig fodder;
};

[[nodiscard]] TrapConfig const& config();
[[nodiscard]] TrapConfig&       mutableConfig(); ///< selftests only

// ---- ids ------------------------------------------------------------------------------------

inline constexpr std::string_view kSpikesId = "complementaries:bamboo_spikes";
inline constexpr std::string_view kTippedId = "complementaries:bamboo_spikes_tipped";
inline constexpr std::string_view kCageId   = "complementaries:cage";
inline constexpr std::string_view kFodderId = "complementaries:fodder";

inline constexpr std::string_view kTipState    = "complementaries:tip";
inline constexpr std::string_view kFullState   = "complementaries:full";
/// Fodder: eaten layers (0 = full, 7 = one layer left); Java `layers` = 8 - eaten.
inline constexpr std::string_view kEatenState  = "complementaries:eaten";

/// Tag of the mob copy shown inside a placed cage.
inline constexpr char const* kDisplayTag = "cm_cage_display";

inline constexpr int kMaxCharges = 16;
inline constexpr int kMaxLayers  = 8;

// ---- common helpers (Common.cpp) --------------------------------------------------------------

[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1, int aux = 0);
[[nodiscard]] std::string itemId(ItemStack const& item);
[[nodiscard]] bool        isBlock(Block const& block, std::string_view id);
[[nodiscard]] int         stateOf(Block const& block, std::string_view state, int fallback = 0);
/// Sets one integer state of the block at `pos` (keeps the other states).
bool setState(BlockSource& region, BlockPos const& pos, std::string_view state, int value);

void spawnItem(BlockSource& region, Vec3 const& pos, ItemStack const& item);
/// Sound by name (sound_definitions) for the players near `pos`.
void playSound(BlockSource& region, Vec3 const& pos, std::string const& name, float volume = 1.f, float pitch = 1.f);
void particle(BlockSource& region, Vec3 const& pos, std::string const& effect);
void blockParticles(BlockSource& region, BlockPos const& pos, Block const& block);
/// Runs a server command as the console (owner permission).
void runCommand(std::string const& command);

[[nodiscard]] uint64_t currentTick();
[[nodiscard]] bool     isCreative(Player const& player);
/// Puts `result` into the player's selected slot in place of one item of `held` (a stack keeps the
/// rest and `result` goes to the inventory, or is dropped).
void swapHeld(Player& player, ItemStack const& result);
/// Sends a message to the player's action bar.
void actionBar(Player& player, std::string const& text);

/// The user NBT of an item (nullptr when none).
[[nodiscard]] CompoundTag const* userData(ItemStack const& item);
/// Mutable user NBT (created when missing).
[[nodiscard]] CompoundTag& userDataMut(ItemStack& item);

/// Data key `cm:trap:<kind>:<dim>:<x>:<y>:<z>`.
[[nodiscard]] std::string dataKey(std::string_view kind, core::GlobalPos const& pos);
[[nodiscard]] std::optional<core::GlobalPos> parseKeySuffix(std::string_view suffix);

/// Unique id (raw) of an actor.
[[nodiscard]] int64_t uid(Actor const& actor);
[[nodiscard]] Actor*  actorByUid(int64_t id);

// ---- bamboo spikes (Spikes.cpp) -------------------------------------------------------------

namespace spikes {
struct Tip {
    int      potion  = -1;
    int      charges = 0;
    uint64_t last    = 0; ///< tick of the last used charge (not saved)
};

void onWorldReady();
void onWorldClosed();
/// Per-tick: entities inside spikes.
void tick(Level& level);

[[nodiscard]] std::optional<Tip> tipAt(core::GlobalPos const& pos);
/// Valid potion for tipping under the current config.
[[nodiscard]] bool isPotionValid(int potionId);
/// Tips untipped spikes at `pos` with `potionId` (Java `tryAddingPotion`). False when invalid,
/// already tipped or not spikes.
bool tryAddPotion(BlockSource& region, BlockPos const& pos, int potionId, int charges = kMaxCharges);
/// Applies the tip of the block to `actor` (Java `interactWithEntity`), consuming a charge.
void applyTip(BlockSource& region, BlockPos const& pos, Actor& actor);
/// Tick index of the tip colour state for a potion (1..8).
[[nodiscard]] int tipColor(int potionId);

/// A tipped spike item for `potionId` (NBT + name + lore).
[[nodiscard]] ItemStack tippedItem(int potionId, int charges = kMaxCharges);
/// Potion/charges of a tipped spike item (aux value of crafted items as a fallback).
[[nodiscard]] std::optional<Tip> itemTip(ItemStack const& item);

/// A tipped spike block disappeared: drop the item, forget the data.
void onRemoved(BlockSource& region, BlockPos const& pos, bool drop);
/// A tipped item was placed as spikes at `pos`.
void onPlaced(BlockSource& region, BlockPos const& pos, Tip const& tip);
/// Player used a lingering potion on spikes (deferred).
void useLingeringPotion(Player& player, BlockPos const& pos);
} // namespace spikes

// ---- cage (Cage.cpp) --------------------------------------------------------------------------

namespace cage {
void onWorldReady();
void onWorldClosed();
/// Every 20 ticks: keep the displays of placed cages in place.
void maintain(Level& level);

[[nodiscard]] bool isDisplay(Actor const& actor);
/// Item holds a mob.
[[nodiscard]] bool isFull(ItemStack const& item);

/// Catch rules. Empty when it can be caught; otherwise the failure message (already translated
/// for `player`), or "-" for silent failure (not living).
[[nodiscard]] std::string catchFailure(Actor& mob, Player& player);
/// The player catches `mob` with the cage in hand. True when caught.
bool tryCatch(Player& player, Actor& mob);
/// The player releases the mob of the held cage at `pos`. True when released.
bool release(Player& player, Vec3 const& pos);

/// A full cage item was placed at `pos`.
void onPlaced(BlockSource& region, BlockPos const& pos, CompoundTag const& data);
/// A full cage block disappeared.
void onRemoved(BlockSource& region, BlockPos const& pos, bool drop);
/// The mob stored in a placed cage (nullptr when empty).
[[nodiscard]] CompoundTag const* contents(core::GlobalPos const& pos);
/// The display actor of a placed cage.
[[nodiscard]] Actor* displayAt(core::GlobalPos const& pos);

/// Temporary catchable list (`/supplementaries cage <entity>`).
void addCommandMob(std::string const& type);

/// Spawns an actor from saved NBT at `pos` (new unique id).
Actor* spawnFromTag(BlockSource& region, CompoundTag tag, Vec3 const& pos, float yaw);
} // namespace cage

// ---- fodder (Fodder.cpp) ----------------------------------------------------------------------

namespace fodder {
void onWorldClosed();
void tick(Level& level);
/// Layers of fodder at `pos` (0 when not fodder).
[[nodiscard]] int layersAt(BlockSource& region, BlockPos const& pos);
/// Sets the layers (0 removes the block).
void setLayers(BlockSource& region, BlockPos const& pos, int layers);
/// Merges the fodder column around `pos` downwards (Java `updateShape`).
void settle(BlockSource& region, BlockPos const& pos);
/// A hoe used on fodder: one layer less.
void useHoe(Player& player, BlockPos const& pos);
/// The animal eats one layer at `pos` (end of the eating goal).
void eat(Mob& animal, BlockSource& region, BlockPos const& pos);
/// Starts the goal at once for `animal` (selftests).
void forceSearch(Actor& animal);
} // namespace fodder

} // namespace cm::feature::trap
