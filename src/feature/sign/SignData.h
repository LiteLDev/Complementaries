#pragma once

// Game-independent data of the sign unit: wood types, dye colours, text metrics, the per-arm data
// of sign posts and its packing into the display entities' synched variant fields (read by the
// entity's Molang and by the client text renderer).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

namespace cm::feature::sign {

// ---- ids ----------------------------------------------------------------------------------------
inline constexpr std::array<std::string_view, 12> kWoods =
    {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "bamboo", "crimson", "warped", "pale_oak"};

inline constexpr std::string_view kSignPostEntity   = "complementaries:sign_post";
inline constexpr std::string_view kTextDisplayEntity = "complementaries:text_display";
inline constexpr std::string_view kNoticeBoard      = "complementaries:notice_board";
inline constexpr std::string_view kDoormat          = "complementaries:doormat";
inline constexpr std::string_view kFacingState      = "complementaries:facing";
inline constexpr std::string_view kHasBookState     = "complementaries:has_book";
inline constexpr std::string_view kPoweredState     = "complementaries:powered";

/// "complementaries:sign_post_<wood>".
[[nodiscard]] std::string signPostItemId(int wood);
/// Wood index of a sign post item id, nullopt for other items.
[[nodiscard]] std::optional<int> woodOfSignPostItem(std::string_view itemId);
/// Wood index of a planks/fence/... id containing a wood name ("minecraft:spruce_fence" -> 1), 0 if none.
[[nodiscard]] int woodOfBlock(std::string_view blockId);

// ---- dyes ---------------------------------------------------------------------------------------
/// Java DyeColor order: 0 white .. 15 black. Black is the default text colour.
inline constexpr int kDefaultDye = 15;
[[nodiscard]] std::optional<int> dyeOfItem(std::string_view itemId);
/// Text colour (0xRRGGBB) of a dye on signs.
[[nodiscard]] std::uint32_t dyeTextColor(int dye);
/// Darker outline colour of glowing text.
[[nodiscard]] std::uint32_t dyeOutlineColor(int dye);
/// Closest `§` colour code for vanilla name tags (light colours, since name tags have a dark background).
[[nodiscard]] char dyeFormatCode(int dye);

// ---- text ---------------------------------------------------------------------------------------
/// Width in font pixels of `text` in the default Minecraft font (UTF-8; unknown glyphs count 6).
[[nodiscard]] int textWidth(std::string_view text);
/// Removes `§x` formatting codes and control characters (newlines too unless `keepNewlines`).
[[nodiscard]] std::string stripFormatting(std::string_view text, bool keepNewlines = false);
/// Cuts `text` (already stripped) so it is at most `maxWidth` font pixels wide.
[[nodiscard]] std::string fitWidth(std::string_view text, int maxWidth);
/// Word-wraps `text` into lines of at most `maxWidth` pixels (keeps explicit newlines).
[[nodiscard]] std::vector<std::string> wrapText(std::string_view text, int maxWidth);

inline constexpr int kSignPostLineWidth = 90;
inline constexpr int kDoormatLineWidth  = 75;
inline constexpr int kDoormatLines      = 3;
inline constexpr int kBoardLineWidth    = 90;

// ---- sign post data -----------------------------------------------------------------------------
struct Arm {
    bool        active  = false;
    int         wood    = 0;
    float       yaw     = 0.0f; ///< direction the arrow points to (Minecraft yaw: 0 south, 90 west)
    bool        left    = false;
    std::string text;
    int         color   = kDefaultDye;
    bool        glowing = false;
};

struct Post {
    Arm          up;
    Arm          down;
    bool         waxed  = false;
    std::int64_t entity = 0; ///< ActorUniqueID of the display entity (0 = none yet)

    [[nodiscard]] bool empty() const { return !up.active && !down.active; }
    [[nodiscard]] Arm&       arm(bool upper) { return upper ? up : down; }
    [[nodiscard]] Arm const& arm(bool upper) const { return upper ? up : down; }
};

[[nodiscard]] nlohmann::json toJson(Post const& post);
[[nodiscard]] Post           postFromJson(nlohmann::json const& j);

/// Normalises a yaw to [0, 360).
[[nodiscard]] float normalizeYaw(float yaw);
/// Yaw (Minecraft convention) of the direction from (fromX, fromZ) to (toX, toZ).
[[nodiscard]] float yawTowards(double fromX, double fromZ, double toX, double toZ);

/// Packed arm (bit 0 active, 1 left, 2-5 wood, 6-14 yaw degrees, 15-18 dye, 19 glowing).
[[nodiscard]] int packArm(Arm const& arm);
[[nodiscard]] Arm unpackArm(int packed);

/// Packed text display (bits 0-1 kind, 2-3 facing, 4-7 dye, 8 glowing, 9-12 text scale step).
enum class DisplayKind : int { Board = 0, Doormat = 1 };
struct DisplayInfo {
    DisplayKind kind    = DisplayKind::Board;
    int         facing  = 0;
    int         color   = kDefaultDye;
    bool        glowing = false;
};
[[nodiscard]] int         packDisplay(DisplayInfo const& info);
[[nodiscard]] DisplayInfo unpackDisplay(int packed);

/// Name tag text of a sign post display: the lines of the active arms (up first), each prefixed by
/// its `§` colour code for vanilla clients.
[[nodiscard]] std::string postNameTag(Post const& post);
/// Text lines prefixed with one colour code (vanilla name tag fallback).
[[nodiscard]] std::string colouredLines(std::vector<std::string> const& lines, int dye);

// ---- positions ----------------------------------------------------------------------------------
/// "<dim>:<x>:<y>:<z>".
[[nodiscard]] std::string posKey(int dim, int x, int y, int z);
struct KeyPos {
    int dim, x, y, z;
};
[[nodiscard]] std::optional<KeyPos> parsePosKey(std::string_view key);

} // namespace cm::feature::sign
