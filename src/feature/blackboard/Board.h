#pragma once

// Blackboard data (pure, no game headers): 16x16 pixels plus the glow / wax flags.
//
// Pixel values follow Supplementaries: 0 blank (also what black draws), 1 white, 15 orange and every
// other dye its Java dye id (2 magenta, 3 light blue, ..., 14 red).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cm::feature::blackboard {

inline constexpr int kSize   = 16;
inline constexpr int kPixels = kSize * kSize;

using Pixels = std::array<std::uint8_t, kPixels>;

struct Board {
    Pixels px{}; ///< row-major: index = y * 16 + x, (0, 0) = top left as seen from the front
    bool   glow  = false;
    bool   waxed = false;

    [[nodiscard]] std::uint8_t get(int x, int y) const { return px[static_cast<std::size_t>(y * kSize + x)]; }
    void set(int x, int y, std::uint8_t v) { px[static_cast<std::size_t>(y * kSize + x)] = v & 15; }
    /// No pixel drawn.
    [[nodiscard]] bool blank() const;
    /// Nothing worth storing (Java `BlackboardData.EMPTY`).
    [[nodiscard]] bool empty() const { return blank() && !glow && !waxed; }
    void               clear() { px.fill(0); }

    friend bool operator==(Board const&, Board const&) = default;
};

/// 256 hex digits, one per pixel, row-major from the top left.
[[nodiscard]] std::string           toHex(Pixels const& px);
[[nodiscard]] std::optional<Pixels> parseHex(std::string_view hex);

/// Java dye ids: 0 white, 1 orange, 2 magenta, ..., 14 red, 15 black.
[[nodiscard]] std::uint8_t byteOfDye(int dyeId);
[[nodiscard]] int          dyeOfByte(std::uint8_t value);
/// Dye map colour of a pixel value (0xRRGGBB; 0 and 1 are white chalk).
[[nodiscard]] std::uint32_t rgbOf(std::uint8_t value);
/// Bedrock text formatting code ("§f", ...) closest to a pixel value; blank is "§0".
[[nodiscard]] std::string_view formatCodeOf(std::uint8_t value);
/// Item id of the dye with the given Java dye id ("minecraft:white_dye", ...).
[[nodiscard]] std::string_view dyeItem(int dyeId);

/// The chalk value of an item (Java tags `supplementaries:blackboard_<colour>`): white dye draws 1;
/// black dye, coal and charcoal erase (0); with `colored` every other dye draws its colour.
[[nodiscard]] std::optional<std::uint8_t> chalkOf(std::string_view itemId, bool colored);

/// Pixel art of the drawing as 16 lines of coloured block characters (tooltip preview).
[[nodiscard]] std::vector<std::string> pixelArt(Pixels const& px);

/// Bit mask (bit v) of the pixel values used by rows [row0, row0 + rows).
[[nodiscard]] std::uint32_t colorMask(Pixels const& px, int row0, int rows);
/// The display entity properties of one half (0 = rows 0-7, 1 = rows 8-15): 22 ints of 6 pixels.
[[nodiscard]] std::array<int, 22> packHalf(Pixels const& px, int half);

} // namespace cm::feature::blackboard
