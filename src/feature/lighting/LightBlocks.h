#pragma once

// Lighting blocks of U9: identification and block-state helpers (server thread).
//
// Custom states are read from the block's serialized states (so string states such as the vanilla
// `minecraft:block_face` trait work too) and written through `Block::tryGetFromRegistry`.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

class Block;
class BlockPos;
class BlockSource;
class Vec3;

namespace cm::feature::lighting {

enum class Kind { None, Sconce, SconceLever, CandleHolder, Lamp, WallLantern };
enum class Flame { Torch, Soul, Ender, Glow, Green };

struct BlockInfo {
    Kind  kind  = Kind::None;
    Flame flame = Flame::Torch;
};

inline constexpr std::string_view kSconces[] = {"sconce", "sconce_soul", "sconce_ender", "sconce_glow", "sconce_green"};
inline constexpr std::string_view kLamps[]   = {"stone_lamp", "blackstone_lamp", "deepslate_lamp", "end_stone_lamp"};
inline constexpr std::string_view kColors[]  = {
    "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
    "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black",
};
/// `complementaries:lantern` state value -> lantern item id.
inline constexpr std::string_view kLanterns[] = {
    "minecraft:lantern",
    "minecraft:soul_lantern",
    "minecraft:copper_lantern",
    "minecraft:exposed_copper_lantern",
    "minecraft:weathered_copper_lantern",
    "minecraft:oxidized_copper_lantern",
    "minecraft:waxed_copper_lantern",
    "minecraft:waxed_exposed_copper_lantern",
    "minecraft:waxed_weathered_copper_lantern",
    "minecraft:waxed_oxidized_copper_lantern",
};
/// Wall lantern swing frames: `complementaries:swing` value -> degrees (see the geometry).
inline constexpr int kSwingFrames[] = {0, 5, 10, 15, 20, -5, -10, -15, -20};

inline constexpr char const* kLit     = "complementaries:lit";
inline constexpr char const* kPowered = "complementaries:powered";
inline constexpr char const* kCandles = "complementaries:candles";
inline constexpr char const* kLantern = "complementaries:lantern";
inline constexpr char const* kSwing   = "complementaries:swing";
inline constexpr char const* kFace    = "minecraft:block_face";
inline constexpr char const* kCardinal = "minecraft:cardinal_direction";

/// Every block id of the unit (full ids).
[[nodiscard]] std::vector<std::string> allBlockIds();

[[nodiscard]] BlockInfo classify(std::string_view id);
[[nodiscard]] BlockInfo classify(Block const& block);
[[nodiscard]] inline bool isLightable(Kind k) {
    return k == Kind::Sconce || k == Kind::SconceLever || k == Kind::CandleHolder;
}

using StateValue = std::variant<int, float, bool, std::string>;
using StateList  = std::vector<std::pair<std::string, StateValue>>;

/// The block's states as stored ("complementaries:lit" -> true, "minecraft:block_face" -> "north").
[[nodiscard]] StateList states(Block const& block);
[[nodiscard]] bool        boolState(Block const& block, std::string_view name, bool def = false);
[[nodiscard]] int         intState(Block const& block, std::string_view name, int def = 0);
[[nodiscard]] std::string stringState(Block const& block, std::string_view name);

/// `block` with `overrides` applied (nullptr if the combination doesn't exist).
[[nodiscard]] Block const* withStates(Block const& block, StateList const& overrides);
/// The block `id` with `overrides` applied to its default states.
[[nodiscard]] Block const* makeBlock(std::string_view id, StateList const& overrides = {});

/// Index into `kLanterns` (-1 if `itemId` is not a lantern).
[[nodiscard]] int lanternIndex(std::string_view itemId);

/// "north"/"south"/"west"/"east" for a Bedrock FacingID 2..5 ("" otherwise).
[[nodiscard]] std::string_view faceName(int facing);
/// Unit vector (dx, dz) from a block attached with `face` towards the block that supports it.
[[nodiscard]] std::pair<int, int> towardsSupport(std::string_view face);

/// The block holds water in its liquid layer.
[[nodiscard]] bool waterlogged(BlockSource& region, BlockPos const& pos);

} // namespace cm::feature::lighting
