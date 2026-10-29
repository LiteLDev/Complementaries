#include "feature/blackboard/Board.h"

#include <algorithm>

namespace cm::feature::blackboard {

namespace {
constexpr std::array<std::string_view, 16> kDyeItems{
    "minecraft:white_dye",  "minecraft:orange_dye", "minecraft:magenta_dye", "minecraft:light_blue_dye",
    "minecraft:yellow_dye", "minecraft:lime_dye",   "minecraft:pink_dye",    "minecraft:gray_dye",
    "minecraft:light_gray_dye", "minecraft:cyan_dye", "minecraft:purple_dye", "minecraft:blue_dye",
    "minecraft:brown_dye",  "minecraft:green_dye",  "minecraft:red_dye",     "minecraft:black_dye",
};
// Map colours of the dyes, indexed by Java dye id.
constexpr std::array<std::uint32_t, 16> kDyeRgb{
    0xFFFFFF, 0xD87F33, 0xB24CD8, 0x6699D8, 0xE5E533, 0x7FCC19, 0xF27FA5, 0x4C4C4C,
    0x999999, 0x4C7F99, 0x7F3FB2, 0x334CB2, 0x664C33, 0x667F33, 0x993333, 0x191919,
};
// Text colours by pixel value.
constexpr std::array<std::string_view, 16> kCodes{
    "§0", "§f", "§u", "§b", "§e", "§a", "§d", "§8", "§7", "§3", "§5", "§9", "§n", "§2", "§4", "§6",
};
constexpr char kHexDigits[] = "0123456789abcdef";
} // namespace

bool Board::blank() const {
    return std::all_of(px.begin(), px.end(), [](std::uint8_t v) { return v == 0; });
}

std::string toHex(Pixels const& px) {
    std::string out(kPixels, '0');
    for (std::size_t i = 0; i < px.size(); ++i) out[i] = kHexDigits[px[i] & 15];
    return out;
}

std::optional<Pixels> parseHex(std::string_view hex) {
    if (hex.size() != kPixels) return std::nullopt;
    Pixels px{};
    for (std::size_t i = 0; i < hex.size(); ++i) {
        char c = hex[i];
        if (c >= '0' && c <= '9') px[i] = static_cast<std::uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') px[i] = static_cast<std::uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') px[i] = static_cast<std::uint8_t>(c - 'A' + 10);
        else return std::nullopt;
    }
    return px;
}

std::uint8_t byteOfDye(int dyeId) {
    switch (dyeId) {
    case 15:
        return 0; // black
    case 0:
        return 1; // white
    case 1:
        return 15; // orange
    default:
        return static_cast<std::uint8_t>(dyeId & 15);
    }
}

int dyeOfByte(std::uint8_t value) {
    switch (value & 15) {
    case 0:
        return 15;
    case 1:
        return 0;
    case 15:
        return 1;
    default:
        return value & 15;
    }
}

std::uint32_t rgbOf(std::uint8_t value) {
    if ((value & 15) <= 1) return 0xFFFFFF;
    return kDyeRgb[static_cast<std::size_t>(dyeOfByte(value))];
}

std::string_view formatCodeOf(std::uint8_t value) { return kCodes[value & 15]; }

std::string_view dyeItem(int dyeId) { return kDyeItems[static_cast<std::size_t>(dyeId & 15)]; }

std::optional<std::uint8_t> chalkOf(std::string_view itemId, bool colored) {
    if (itemId == "minecraft:coal" || itemId == "minecraft:charcoal") return std::uint8_t{0};
    for (int dye = 0; dye < 16; ++dye) {
        if (itemId != kDyeItems[static_cast<std::size_t>(dye)]) continue;
        if (!colored && dye != 0 && dye != 15) return std::nullopt;
        return byteOfDye(dye);
    }
    return std::nullopt;
}

std::vector<std::string> pixelArt(Pixels const& px) {
    std::vector<std::string> lines;
    lines.reserve(kSize);
    for (int y = 0; y < kSize; ++y) {
        std::string line;
        int         last = -1;
        for (int x = 0; x < kSize; ++x) {
            int v = px[static_cast<std::size_t>(y * kSize + x)] & 15;
            if (v != last) {
                line += formatCodeOf(static_cast<std::uint8_t>(v));
                last = v;
            }
            line += "█";
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::uint32_t colorMask(Pixels const& px, int row0, int rows) {
    std::uint32_t mask = 0;
    for (int y = row0; y < row0 + rows && y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) mask |= 1u << (px[static_cast<std::size_t>(y * kSize + x)] & 15);
    }
    return mask & ~1u; // blank needs no layer
}

std::array<int, 22> packHalf(Pixels const& px, int half) {
    std::array<int, 22> out{};
    for (int n = 0; n < 128; ++n) {
        int v = px[static_cast<std::size_t>(half * 128 + n)] & 15;
        out[static_cast<std::size_t>(n / 6)] |= v << (4 * (n % 6));
    }
    return out;
}

} // namespace cm::feature::blackboard
