#pragma once

// Pure logic of the circuit unit (no game headers): directions, display digits and chains,
// speaker modes. Unit-tested in tests/circuit/.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace cm::feature::circuit {

enum class Kind : std::uint8_t { None, Relayer, Speaker, Display, Illuminator };

/// Speaker message modes, in the order of the screen's dropdown.
enum class SpeakerMode : int { Chat = 0, Status = 1, Title = 2, Narrator = 3 };
inline constexpr int kSpeakerModeCount = 4;

// ---- directions (Bedrock Facing: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east) -------------
struct Offset {
    int x, y, z;
};
[[nodiscard]] constexpr Offset facingOffset(int facing) {
    switch (facing) {
    case 0:
        return {0, -1, 0};
    case 1:
        return {0, 1, 0};
    case 2:
        return {0, 0, -1};
    case 3:
        return {0, 0, 1};
    case 4:
        return {-1, 0, 0};
    default:
        return {1, 0, 0};
    }
}
[[nodiscard]] constexpr int oppositeFacing(int facing) { return facing ^ 1; }

/// `minecraft:cardinal_direction` index (the game orders the values south, west, north, east) -> Facing.
[[nodiscard]] constexpr int cardinalToFacing(int cardinal) {
    constexpr std::array<int, 4> map{3, 4, 2, 5};
    return map[static_cast<std::size_t>(cardinal) & 3];
}
/// Facing (2..5) -> `minecraft:cardinal_direction` index.
[[nodiscard]] constexpr int facingToCardinal(int facing) {
    switch (facing) {
    case 3:
        return 0;
    case 4:
        return 1;
    case 2:
        return 2;
    default:
        return 3;
    }
}

/// For a block whose front faces `front` (horizontal Facing), the Facing towards the right hand of a
/// player standing in front of it and looking at it.
[[nodiscard]] constexpr int viewerRight(int front) {
    // the viewer looks along -front; their right is that direction turned clockwise (seen from above)
    switch (front) {
    case 2: // front north: viewer looks south, right = west
        return 4;
    case 3: // front south: viewer looks north, right = east
        return 5;
    case 4: // front west: viewer looks east, right = south
        return 3;
    default: // front east: viewer looks west, right = north
        return 2;
    }
}
[[nodiscard]] constexpr int viewerLeft(int front) { return oppositeFacing(viewerRight(front)); }

// ---- crystal display ---------------------------------------------------------------------------
/// `complementaries:chain` values.
enum class Chain : int { Alone = 0, Tens = 1, Units = 2 };

/// Role of a display in a row: `index` = position from the left end (0-based), `length` = number of
/// displays in the row. Displays pair up from the left; an odd one out at the right end is alone.
[[nodiscard]] constexpr Chain chainRole(int index, int length) {
    if (length <= 1) return Chain::Alone;
    if (index % 2 == 1) return Chain::Units;
    return index + 1 < length ? Chain::Tens : Chain::Alone;
}

/// Value shown by a pair (tens display power, units display power): 0..99.
[[nodiscard]] constexpr int pairValue(int tensPower, int unitsPower) {
    return std::clamp(tensPower, 0, 9) * 10 + std::clamp(unitsPower, 0, 9);
}

/// Seven-segment masks (bit 0 = a (top), b (top right), c (bottom right), d (bottom), e (bottom
/// left), f (top left), g (middle) = bit 6).
inline constexpr std::array<std::uint8_t, 10> kSegments{
    0b0111111, // 0
    0b0000110, // 1
    0b1011011, // 2
    0b1001111, // 3
    0b1100110, // 4
    0b1101101, // 5
    0b1111101, // 6
    0b0000111, // 7
    0b1111111, // 8
    0b1101111, // 9
};

/// The digits a display shows: {left, right} for a lone display (-1 = blank), {big, -1} chained.
struct Digits {
    int left  = -1;
    int right = -1;
    int big   = -1;
};
[[nodiscard]] constexpr Digits displayDigits(int power, Chain chain) {
    power = std::clamp(power, 0, 15);
    Digits d;
    if (chain == Chain::Alone) {
        d.left  = power >= 10 ? power / 10 : -1;
        d.right = power % 10;
    } else {
        d.big = std::min(power, 9);
    }
    return d;
}

/// Light level of a redstone illuminator receiving `power`.
[[nodiscard]] constexpr int illuminatorLight(int power) { return std::clamp(power, 0, 15); }

/// The first `maxChars` characters (UTF-8 code points) of `s`; never splits a character.
[[nodiscard]] inline std::string truncateUtf8(std::string_view s, int maxChars) {
    std::size_t i     = 0;
    int         chars = 0;
    while (i < s.size() && chars < maxChars) {
        auto c   = static_cast<unsigned char>(s[i]);
        int  len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (i + static_cast<std::size_t>(len) > s.size()) break;
        i += static_cast<std::size_t>(len);
        ++chars;
    }
    return std::string(s.substr(0, i));
}

/// Clamps a speaker volume to 1..range.
[[nodiscard]] constexpr int clampVolume(int volume, int range) { return std::clamp(volume, 1, std::max(range, 1)); }

} // namespace cm::feature::circuit
