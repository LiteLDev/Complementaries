#pragma once

// U16 `tool`: slingshot, bubble blower (+ bubble block), soap, wrench, flute, confetti popper,
// antique ink and slice map. Behaviour reference: docs/spec/tool.md.
//
// Everything runs on the server thread (BDS, or the integrated server of a client local world).
// Hooks and listeners live in Module.cpp and dispatch to the per-tool files.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"

class Actor;
class Block;
class BlockSource;
class CompoundTag;
class Dimension;
class ItemStack;
class ItemStackBase;
class Level;
class Player;

namespace cm::feature::tool {

// ---- configuration (`config/tool.json`) ---------------------------------------------------------
struct SlingshotConfig {
    bool                     enabled            = true;
    int                      chargeTime         = 20;
    float                    rangeMultiplier    = 1.0f;
    float                    stasisDeceleration = 0.9625f;
    bool                     placeBlocks        = true;
    std::vector<std::string> ammoBlacklist      = {};
};
struct BubbleBlowerConfig {
    bool enabled          = true;
    int  bubbleLifetime   = 1200;
    int  bubbleBlockCost  = 5;
    bool breakWhenTouched = true;
};
struct SoapConfig {
    bool enabled = true;
};
struct WrenchConfig {
    bool                     enabled            = true;
    bool                     bypassWhenSneaking = false;
    std::vector<std::string> blacklist          = {};
};
struct FluteConfig {
    bool enabled       = true;
    int  unboundRadius = 64;
    int  boundDistance = 500;
};
struct ConfettiPopperConfig {
    bool enabled = true;
};
struct AntiqueInkConfig {
    bool enabled = true;
};
struct SliceMapConfig {
    bool  enabled         = true;
    float rangeMultiplier = 1.0f;
};
struct ToolConfig {
    int                  version = 1;
    SlingshotConfig      slingshot;
    BubbleBlowerConfig   bubbleBlower;
    SoapConfig           soap;
    WrenchConfig         wrench;
    FluteConfig          flute;
    ConfettiPopperConfig confettiPopper;
    AntiqueInkConfig     antiqueInk;
    SliceMapConfig       sliceMap;
};
[[nodiscard]] ToolConfig const& config();

// ---- ids ----------------------------------------------------------------------------------------
namespace ids {
inline constexpr std::string_view Slingshot      = "complementaries:slingshot";
inline constexpr std::string_view BubbleBlower   = "complementaries:bubble_blower";
inline constexpr std::string_view BubbleBlock    = "complementaries:bubble_block";
inline constexpr std::string_view Soap           = "complementaries:soap";
inline constexpr std::string_view Wrench         = "complementaries:wrench";
inline constexpr std::string_view Flute          = "complementaries:flute";
inline constexpr std::string_view ConfettiPopper = "complementaries:confetti_popper";
inline constexpr std::string_view AntiqueInk     = "complementaries:antique_ink";
inline constexpr std::string_view EmptySliceMap  = "complementaries:empty_slice_map";
} // namespace ids

// ---- common helpers (Common.cpp) ----------------------------------------------------------------
/// Bedrock formatting codes: fmtCode("r7") = "§r§7".
[[nodiscard]] inline std::string fmtCode(std::string_view codes) {
    std::string out;
    for (char c : codes) {
        out += "\xc2\xa7";
        out += c;
    }
    return out;
}
[[nodiscard]] Level* level();
/// The current server tick (0 without a level).
[[nodiscard]] std::uint64_t currentTick();
/// An item stack of `id` (empty stack when the id is unknown).
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1, int aux = 0);
[[nodiscard]] std::string itemId(ItemStackBase const& item);
[[nodiscard]] bool        isItem(ItemStackBase const& item, std::string_view id);
[[nodiscard]] bool        isCreative(Player const& player);
[[nodiscard]] Player*     playerById(std::int64_t uniqueId);
[[nodiscard]] Actor*      actorById(std::int64_t uniqueId);
[[nodiscard]] Vec3        lookVector(Actor const& actor);
[[nodiscard]] Vec3        eyePos(Actor const& actor);
[[nodiscard]] Vec3        center(BlockPos const& pos);
[[nodiscard]] float       length(Vec3 const& v);
[[nodiscard]] Vec3        normalized(Vec3 const& v);
/// Unit offset of a Bedrock face id (0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
[[nodiscard]] BlockPos faceOffset(int face);
[[nodiscard]] BlockSource* regionOf(int dim);

/// Plays a named sound (vanilla or resource-pack sound event) to players near `pos`.
void playSound(int dim, Vec3 const& pos, std::string const& name, float volume = 1.0f, float pitch = 1.0f);
/// Spawns a particle effect (vanilla or `complementaries:*` from the resource pack).
void particle(int dim, Vec3 const& pos, std::string const& effect);
/// Sends the actor's velocity to the clients.
void syncMotion(Actor& actor);
void setVelocity(Actor& actor, Vec3 const& v);

/// Runs `fn` on the next server tick (world edits after a cancelled interaction event).
void nextTick(std::function<void()> fn);
/// Runs the queued next-tick tasks (Module.cpp calls it every tick).
void runQueued();

/// The player's selected (main hand) stack is replaced with `item` and synced.
void setMainhand(Player& player, ItemStack const& item);
void setOffhand(Player& player, ItemStack const& item);
/// Uses one item of the main hand / off hand stack (nothing in creative).
void consumeMainhand(Player& player, int count = 1);
void consumeOffhand(Player& player, int count = 1);
/// Damages the main hand item by `amount` (Unbreaking aware, breaks it at 0). Nothing in creative.
void damageMainhand(Player& player, int amount = 1);

/// Integer custom data of an item stack (`cm_*` keys in its user data).
[[nodiscard]] int intData(ItemStackBase const& item, std::string const& key, int fallback = 0);
void              setIntData(ItemStackBase& item, std::string const& key, int value);
void              removeData(ItemStackBase& item, std::string const& key);
[[nodiscard]] std::string stringData(ItemStackBase const& item, std::string const& key);
void                      setStringData(ItemStackBase& item, std::string const& key, std::string const& value);
/// Replaces lore lines that start with `prefix` by `line` (removes them when `line` is empty).
void setLoreLine(ItemStackBase& item, std::string const& prefix, std::string const& line);

/// Block states by name (`Block::BlockStatesType` style: int / bool / string values).
using StateValue = std::variant<int, float, bool, std::string>;
using States     = std::vector<std::pair<std::string, StateValue>>;
[[nodiscard]] States       blockStates(Block const& block);
/// `id` with `states` (states the block doesn't have are dropped). nullptr if `id` is unknown or
/// the combination is invalid.
[[nodiscard]] Block const* blockWithStates(std::string const& id, States const& states);
/// Replaces the block at `pos`, keeping its block entity data (contents, text, ...).
bool                       replaceKeepingData(BlockSource& region, BlockPos const& pos, Block const& block);

/// Supplementaries' Stasis enchantment (stored as `cm_stasis`, see docs/spec/tool.md).
[[nodiscard]] int stasisLevel(ItemStackBase const& item);
void              setStasisLevel(ItemStackBase& item, int level);
[[nodiscard]] int enchantLevel(ItemStackBase const& item, int enchantType);

// ---- slingshot (Slingshot.cpp) ------------------------------------------------------------------
namespace slingshot {
/// Charge time in ticks for this slingshot (Quick Charge aware).
[[nodiscard]] int   chargeTicks(ItemStackBase const& sling);
/// Power 0..1 after `usedTicks` of charging (bow-like curve).
[[nodiscard]] float power(int usedTicks, ItemStackBase const& sling);
/// Fires from `player` (release after `usedTicks`). Returns the number of projectiles launched.
int                 release(Player& player, ItemStack const& sling, int usedTicks);
/// Launches one projectile carrying `ammo` (count 1) from `from` with `velocity`.
bool                launch(Player& owner, ItemStack const& ammo, Vec3 const& from, Vec3 const& velocity, int loyalty, int stasis);
void                tick();
void                clear();
[[nodiscard]] int   activeProjectiles();
} // namespace slingshot

// ---- bubble blower + bubble block (Bubble.cpp) ---------------------------------------------------
namespace bubble {
/// One tick of blowing (called while the player keeps using the blower).
void               blowTick(Player& player, int usedTicks);
/// Use on a block face: places a bubble block in front of it. True when handled.
bool               useOnBlock(Player& player, BlockPos const& clicked, int face);
/// Use in the air: refill from soap in the other hand. True when refilled.
bool               tryRefill(Player& player);
/// Places a bubble block (tracked; pops after its lifetime).
bool               placeBubble(int dim, BlockPos const& pos, int lifetime);
void               pop(int dim, BlockPos const& pos);
void               tick();
void               load();
void               save();
void               clear();
[[nodiscard]] bool isTracked(int dim, BlockPos const& pos);
} // namespace bubble

// ---- soap (Soap.cpp) ----------------------------------------------------------------------------
namespace soap {
/// The clean form of a block (nullptr when it is already clean / not washable).
[[nodiscard]] Block const* cleanBlock(Block const& block);
/// Washes the block at `pos` (signs too). True when something changed.
bool                       washBlock(BlockSource& region, BlockPos const& pos);
/// Washes an item stack in place. True when something changed.
bool                       washItem(ItemStack& item);
/// Washes an entity (sheep). True when something changed.
bool                       washEntity(Actor& target);
bool                       useOnBlock(Player& player, BlockPos const& pos);
bool                       useOnEntity(Player& player, Actor& target);
bool                       useInAir(Player& player);
} // namespace soap

// ---- wrench (Wrench.cpp) ------------------------------------------------------------------------
namespace wrench {
/// The block rotated around the axis of `face` (clockwise seen from that face); nullptr when it
/// can't be rotated.
[[nodiscard]] Block const* rotated(Block const& block, int face, bool ccw);
bool                       rotateAt(BlockSource& region, BlockPos const& pos, int face, bool ccw);
bool                       useOnBlock(Player& player, BlockPos const& pos, int face);
bool                       useOnEntity(Player& player, Actor& target);
} // namespace wrench

// ---- flute (Flute.cpp) --------------------------------------------------------------------------
namespace flute {
bool useInAir(Player& player);
bool useOnEntity(Player& player, Actor& target);
/// Calls the pets (bound or unbound) of `player`. Returns how many were teleported.
int  callPets(Player& player, ItemStack& flute);
void tick();
void clear();
} // namespace flute

// ---- confetti popper (Confetti.cpp) -------------------------------------------------------------
namespace confetti {
void burst(int dim, Vec3 const& from, Vec3 const& direction);
bool useInAir(Player& player);
} // namespace confetti

// ---- antique ink (AntiqueInk.cpp) ---------------------------------------------------------------
namespace antique {
[[nodiscard]] bool isAntiqueSign(int dim, BlockPos const& pos);
bool               applyToSign(BlockSource& region, BlockPos const& pos);
bool               removeFromSign(BlockSource& region, BlockPos const& pos);
bool               useOnBlock(Player& player, BlockPos const& pos);
bool               useInAir(Player& player);
[[nodiscard]] bool isAntiqueItem(ItemStackBase const& item);
void               setAntiqueItem(ItemStackBase& item, bool antique);
[[nodiscard]] bool isSign(Block const& block);
/// Sign text colour / glow of both sides (`color` ARGB).
bool               setSignLook(BlockSource& region, BlockPos const& pos, std::uint32_t color, bool glow);
[[nodiscard]] std::optional<std::uint32_t> signColor(BlockSource& region, BlockPos const& pos);
} // namespace antique

// ---- slice map (SliceMap.cpp) -------------------------------------------------------------------
namespace slicemap {
/// Turns the held empty slice map into a filled slice map. True when done.
bool                         useInAir(Player& player);
/// Creates a filled slice map item at height `y` around `center`.
[[nodiscard]] ItemStack      create(int dim, BlockPos const& center, int y);
/// Height of a slice map item (nullopt for other items).
[[nodiscard]] std::optional<int> sliceHeight(ItemStackBase const& item);
/// Renders `columns` columns of the map (progressively); returns how many were drawn.
int                          render(std::int64_t mapId, BlockPos const& around, int radius, int columns);
/// The map pixel (ABGR) at map coordinates (x, z).
[[nodiscard]] std::optional<std::uint32_t> pixel(std::int64_t mapId, int x, int z);
[[nodiscard]] std::int64_t   mapIdOf(ItemStackBase const& item);
void                         tick();
void                         clear();
} // namespace slicemap

} // namespace cm::feature::tool
