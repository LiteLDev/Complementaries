#pragma once

// U14 flag: flags (with banner patterns and a helper cloth entity), awnings, buntings and the
// dragon banner pattern. See docs/spec/flag.md.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class ItemStackBase;
class Player;

namespace cm::feature::flag {

struct FlagConfig {
    int    version            = 1;
    bool   flagEnabled        = true;
    bool   stickPole          = true;
    int    poleLength         = 16;
    bool   awningEnabled      = true;
    bool   awningSlant        = true;
    bool   awningShiftThrough = true;
    double awningBounceAngle  = 69.44395478041653; // atan(16 / 6) in degrees
    bool   buntingsEnabled    = true;
    bool   dragonPattern      = true;
};

// ---- colours ----------------------------------------------------------------------------------------
inline constexpr int kColors = 16;
/// Java dye names in Java order (index = Java dye id; Bedrock ItemColor = 15 - index).
inline constexpr std::array<std::string_view, kColors> kColorNames{
    "white",
    "orange",
    "magenta",
    "light_blue",
    "yellow",
    "lime",
    "pink",
    "gray",
    "light_gray",
    "cyan",
    "purple",
    "blue",
    "brown",
    "green",
    "red",
    "black",
};
[[nodiscard]] inline constexpr int toItemColor(int javaColor) { return 15 - javaColor; }
[[nodiscard]] inline constexpr int fromItemColor(int itemColor) { return 15 - itemColor; }
/// "red" -> 14, -1 when unknown.
[[nodiscard]] int colorIndex(std::string_view name);
/// Translated colour name ("Red").
[[nodiscard]] std::string colorName(int javaColor, Player const* player = nullptr);

// ---- patterns ---------------------------------------------------------------------------------------
/// Pattern codes in the order of the resource pack atlases (entity property value = (i+1)*16+colour).
/// Must match `$Patterns` in tools/textures/flag.ps1.
inline constexpr std::array<std::string_view, 43> kPatternCodes{
    "bs",  "ts",  "ls",  "rs",  "cs",  "ms",  "drs", "dls", "ss",  "cr",  "sc",  "bt",  "tt",     "bts", "tts",
    "ld",  "rd",  "lud", "rud", "mc",  "mr",  "vh",  "hh",  "vhr", "hhb", "bo",  "cbo", "gra",    "gru", "bri",
    "glb", "cre", "sku", "flo", "moj", "pig", "bl",  "br",  "tl",  "tr",  "flw", "gus", "dragon",
};
inline constexpr int kMaxLayers = 6;
[[nodiscard]] int    patternIndex(std::string_view code); ///< -1 when unknown
/// English fallback name of a pattern ("Bordure").
[[nodiscard]] std::string_view patternEnglish(std::string_view code);
/// The item a pattern needs in the loom ("" = none).
[[nodiscard]] std::string_view patternItem(std::string_view code);
/// "Red Bordure" in the player's (or the default) locale.
[[nodiscard]] std::string layerName(std::string_view code, int javaColor, Player const* player = nullptr);

struct Layer {
    std::string pattern;                            ///< Bedrock code ("bo")
    int         color                          = 0; ///< Java dye index
    bool        operator==(Layer const&) const = default;
};
using Layers = std::vector<Layer>;

/// Banner-style pattern layers stored on an item (`Patterns: [{Pattern, Color}]`, Bedrock colours).
[[nodiscard]] Layers readLayers(ItemStackBase const& item);
/// Writes the layers (and a lore listing them) on the item; an empty list removes them.
void                         writeLayers(ItemStack& item, Layers const& layers, Player const* player = nullptr);
[[nodiscard]] nlohmann::json layersToJson(Layers const& layers);
[[nodiscard]] Layers         layersFromJson(nlohmann::json const& j);
/// Entity property value of a layer (0 = none).
[[nodiscard]] int layerProperty(Layer const& layer);

// ---- items / blocks ---------------------------------------------------------------------------------
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1, int aux = 0);
/// "complementaries:flag_red" -> 14; -1 when not a flag id.
[[nodiscard]] int  flagColor(std::string_view id);
[[nodiscard]] bool isFlag(std::string_view id);
[[nodiscard]] bool isAwning(std::string_view id);
[[nodiscard]] bool isBuntingCeiling(std::string_view id);
[[nodiscard]] bool isBuntingWall(std::string_view id);
[[nodiscard]] bool isBuntingItem(std::string_view id);
/// Colour of a bunting block/item id (ceiling or wall), -1 if none.
[[nodiscard]] int  buntingColor(std::string_view id);
[[nodiscard]] bool isStick(std::string_view id);
/// Java colour of a dye item (`minecraft:red_dye`, `minecraft:ink_sac`, ...), -1 if not a dye.
[[nodiscard]] int dyeColor(std::string_view itemId);

inline constexpr std::string_view kClothId = "complementaries:flag_cloth";

// ---- directions -------------------------------------------------------------------------------------
/// Bedrock FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
enum Dir : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
[[nodiscard]] BlockPos offset(BlockPos const& pos, int dir, int n = 1);
[[nodiscard]] int      oppositeDir(int dir);
[[nodiscard]] bool     horizontal(int dir);
/// Java 2D facing (0 S, 1 W, 2 N, 3 E) <-> Dir.
[[nodiscard]] int dirFrom2d(int facing2d);
[[nodiscard]] int dir2d(int dir);
[[nodiscard]] int clockwise(int dir);
[[nodiscard]] int counterClockwise(int dir);
/// The six directions ordered by how closely the actor looks along them (Java
/// `Direction.orderedByNearest`).
[[nodiscard]] std::array<int, 6> nearestLookingDirections(Actor const& actor);

// ---- flags -------------------------------------------------------------------------------------------
struct FlagData {
    int         color  = 0;
    int         facing = 2; ///< Java 2D facing of the block (towards the placer); the cloth extends opposite
    Layers      layers;
    std::string name; ///< custom name
};
[[nodiscard]] std::string             flagKey(core::GlobalPos const& pos);
[[nodiscard]] std::optional<FlagData> loadFlag(core::GlobalPos const& pos);
void                                  saveFlag(core::GlobalPos const& pos, FlagData const& data);
void                                  eraseFlag(core::GlobalPos const& pos);
/// The cloth entity of the flag at `pos` (nullptr if none).
[[nodiscard]] Actor* findCloth(BlockSource& region, BlockPos const& pos);
/// Spawns or updates the cloth of the flag at `pos`.
Actor* syncCloth(BlockSource& region, BlockPos const& pos, FlagData const& data);
void   removeCloth(BlockSource& region, BlockPos const& pos);
/// Moves a flag one block up/down along a stick pole. Returns true when it moved.
bool moveFlagAlongPole(BlockSource& region, BlockPos const& pos, bool up);
/// Opens the Flag loom form for the flag in the player's main hand.
void openLoom(Player& player);
/// Applies one loom step to `item` (no inventory checks). False when the flag is full.
bool addLayer(ItemStack& item, Layer const& layer, Player const* player = nullptr);
/// Copies the patterns of `from` onto `to` (same colour, `to` blank). False if not allowed.
bool copyLayers(ItemStackBase const& from, ItemStack& to, Player const* player = nullptr);
/// Colour of a banner (`minecraft:banner`) or flag item, -1 otherwise.
[[nodiscard]] int bannerOrFlagColor(ItemStackBase const& item);

// ---- awnings -----------------------------------------------------------------------------------------
struct AwningState {
    int  facing2d = 2;
    bool bottom   = false;
    bool slanted  = false;
};
[[nodiscard]] std::optional<AwningState> awningState(Block const& block);
[[nodiscard]] bool awningCanSurvive(BlockSource& region, BlockPos const& pos, AwningState const& s);
/// Java `getStateForPlacement` of awnings. `clickY` is the fraction of the click height in the block.
[[nodiscard]] std::optional<AwningState>
awningPlacement(BlockSource& region, BlockPos const& pos, int face, double clickY, std::array<int, 6> const& looking);
/// Empty-hand click: flips `slanted` (with the place sound). False if `pos` is no awning.
bool toggleAwningSlant(BlockSource& region, BlockPos const& pos);
/// Normal of the bounce surface (x, y, z).
[[nodiscard]] std::array<double, 3> awningNormal(AwningState const& s, double angleDeg);
/// Reflects `v` on the plane with normal `n` (unit).
[[nodiscard]] std::array<double, 3> reflect(std::array<double, 3> const& v, std::array<double, 3> const& n);

// ---- buntings ----------------------------------------------------------------------------------------
[[nodiscard]] bool buntingCeilingCanSurvive(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool buntingWallCanSurvive(BlockSource& region, BlockPos const& pos, int facing2d);
/// The block id + states a bunting item places at `pos`, or nullopt.
struct BuntingPlacement {
    std::string id;
    int         state = 0; ///< axis (ceiling) or facing (wall)
};
[[nodiscard]] std::optional<BuntingPlacement>
buntingPlacement(BlockSource& region, BlockPos const& pos, int color, std::array<int, 6> const& looking);

// ---- shared world helpers -----------------------------------------------------------------------------
[[nodiscard]] bool solid(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool sturdyFace(BlockSource& region, BlockPos const& pos, int face);
/// Places `id` with `states` at `pos` (block updates + clients). False if the id is unknown.
bool place(
    BlockSource&                                         region,
    BlockPos const&                                      pos,
    std::string_view                                     id,
    std::vector<std::pair<std::string_view, int>> const& states = {}
);
/// Breaks the block at `pos` dropping its loot (like a broken support).
void breakWithDrops(BlockSource& region, BlockPos const& pos);
void playBlockSound(
    BlockSource&       region,
    BlockPos const&    pos,
    std::string const& sound,
    float              volume = 1.f,
    float              pitch  = 1.f
);

} // namespace cm::feature::flag
