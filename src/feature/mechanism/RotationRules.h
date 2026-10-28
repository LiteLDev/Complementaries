#pragma once

// Pure rules (no game headers) for rotating block states by 90 degrees around an axis: used by the
// turn table. Unit-tested in tests/mechanism/RotationRulesTest.cpp.
//
// Directions use the Java / Bedrock `facing_direction` order: 0 down, 1 up, 2 north, 3 south,
// 4 west, 5 east. "Clockwise around axis A" means clockwise when looking at the face A points to
// (i.e. looking along -A), the way a turn table facing A turns.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace cm::feature::mechanism::rotation {

inline constexpr int kDown = 0, kUp = 1, kNorth = 2, kSouth = 3, kWest = 4, kEast = 5;

struct Vec {
    int                   x, y, z;
    friend constexpr bool operator==(Vec, Vec) = default;
};

[[nodiscard]] constexpr Vec dirVec(int d) {
    constexpr std::array<Vec, 6> v{
        {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}
    };
    return v[static_cast<std::size_t>(d) % 6];
}

[[nodiscard]] constexpr int vecDir(Vec v) {
    for (int d = 0; d < 6; ++d)
        if (dirVec(d) == v) return d;
    return -1;
}

[[nodiscard]] constexpr int opposite(int d) { return d ^ 1; }

[[nodiscard]] constexpr bool parallel(int a, int b) { return (a >> 1) == (b >> 1); }

/// `d` turned by 90 degrees around axis `axis` (clockwise seen from the `axis` side unless `ccw`).
[[nodiscard]] constexpr int rotateDir(int d, int axis, bool ccw) {
    if (parallel(d, axis)) return d;
    Vec a = dirVec(axis), v = dirVec(d);
    // clockwise seen from +A is a rotation by -90 degrees: v' = v x A; counter-clockwise: A x v
    Vec r = ccw ? Vec{a.y * v.z - a.z * v.y, a.z * v.x - a.x * v.z, a.x * v.y - a.y * v.x}
                : Vec{v.y * a.z - v.z * a.y, v.z * a.x - v.x * a.z, v.x * a.y - v.y * a.x};
    return vecDir(r);
}

/// A block state value as stored in the block's serialization states.
using Value = std::variant<int, bool, std::string>;

enum class Result {
    Unchanged,   ///< the state is not a direction / axis: keep it
    Changed,     ///< `value` was updated
    Unsupported, ///< a direction that cannot turn around this axis (the block must not rotate)
};

namespace detail {
inline constexpr std::array<std::string_view, 6> kDirNames{"down", "up", "north", "south", "west", "east"};

[[nodiscard]] inline int dirOfName(std::string_view s) {
    for (int d = 0; d < 6; ++d)
        if (kDirNames[d] == s) return d;
    return -1;
}

/// A horizontal direction turned around `axis`, or -1 if it would leave the horizontal plane.
[[nodiscard]] constexpr int rotateHorizontal(int d, int axis, bool ccw) {
    if (parallel(d, axis)) return d;
    int r = rotateDir(d, axis, ccw);
    return (r == kDown || r == kUp) ? -1 : r;
}

/// Maps a table of 4 horizontal directions (index -> direction).
template <std::size_t N>
[[nodiscard]] inline Result mapIndexed(Value& value, std::array<int, N> const& dirs, int axis, bool ccw) {
    auto* i = std::get_if<int>(&value);
    if (!i || *i < 0 || *i >= static_cast<int>(N)) return Result::Unchanged;
    int r = rotateHorizontal(dirs[static_cast<std::size_t>(*i)], axis, ccw);
    if (r < 0) return Result::Unsupported;
    for (std::size_t k = 0; k < N; ++k) {
        if (dirs[k] == r) {
            if (static_cast<int>(k) == *i) return Result::Unchanged;
            *i = static_cast<int>(k);
            return Result::Changed;
        }
    }
    return Result::Unsupported;
}

[[nodiscard]] inline bool vertical(int axis) { return axis == kUp || axis == kDown; }

/// Bit field of directions, `bits[k]` the direction of bit k.
template <std::size_t N>
[[nodiscard]] inline Result mapBits(Value& value, std::array<int, N> const& bits, int axis, bool ccw) {
    auto* i = std::get_if<int>(&value);
    if (!i) return Result::Unchanged;
    int out = 0;
    for (std::size_t k = 0; k < N; ++k) {
        if (!(*i & (1 << k))) continue;
        int r   = rotateDir(bits[k], axis, ccw);
        int idx = -1;
        for (std::size_t j = 0; j < N; ++j)
            if (bits[j] == r) idx = static_cast<int>(j);
        if (idx < 0) return Result::Unsupported;
        out |= 1 << idx;
    }
    if (out == *i) return Result::Unchanged;
    *i = out;
    return Result::Changed;
}
} // namespace detail

/// Rotates one block state. `axis` is the turn table's facing.
[[nodiscard]] inline Result rotateState(std::string_view name, Value& value, int axis, bool ccw) {
    using namespace detail;
    // six-way facings
    if (name == "facing_direction") {
        auto* i = std::get_if<int>(&value);
        if (!i || *i < 0 || *i > 5) return Result::Unchanged;
        int r = rotateDir(*i, axis, ccw);
        if (r == *i) return Result::Unchanged;
        *i = r;
        return Result::Changed;
    }
    if (name == "minecraft:facing_direction" || name == "minecraft:block_face") {
        auto* s = std::get_if<std::string>(&value);
        if (!s) return Result::Unchanged;
        int d = dirOfName(*s);
        if (d < 0) return Result::Unchanged;
        int r = rotateDir(d, axis, ccw);
        if (r == d) return Result::Unchanged;
        *s = std::string(kDirNames[static_cast<std::size_t>(r)]);
        return Result::Changed;
    }
    // horizontal-only facings
    if (name == "minecraft:cardinal_direction") {
        auto* s = std::get_if<std::string>(&value);
        if (!s) return Result::Unchanged;
        int d = dirOfName(*s);
        if (d < 2) return Result::Unchanged;
        int r = rotateHorizontal(d, axis, ccw);
        if (r < 0) return Result::Unsupported;
        if (r == d) return Result::Unchanged;
        *s = std::string(kDirNames[static_cast<std::size_t>(r)]);
        return Result::Changed;
    }
    if (name == "direction") return mapIndexed(value, std::array{kSouth, kWest, kNorth, kEast}, axis, ccw);
    if (name == "weirdo_direction") return mapIndexed(value, std::array{kEast, kWest, kSouth, kNorth}, axis, ccw);
    if (name == "coral_direction") return mapIndexed(value, std::array{kWest, kEast, kNorth, kSouth}, axis, ccw);
    if (name == "torch_facing_direction") {
        auto* s = std::get_if<std::string>(&value);
        if (!s) return Result::Unchanged;
        int d = dirOfName(*s);
        if (d < 2) return Result::Unchanged; // "top" / "unknown"
        int r = rotateHorizontal(d, axis, ccw);
        if (r < 0) return Result::Unsupported;
        if (r == d) return Result::Unchanged;
        *s = std::string(kDirNames[static_cast<std::size_t>(r)]);
        return Result::Changed;
    }
    // axes
    if (name == "pillar_axis" || name == "portal_axis" || name == "minecraft:axis") {
        auto* s = std::get_if<std::string>(&value);
        if (!s || s->size() != 1) return Result::Unchanged;
        int d = *s == "y" ? kUp : *s == "x" ? kEast : *s == "z" ? kSouth : -1;
        if (d < 0) return Result::Unchanged;
        int         r   = rotateDir(d, axis, ccw);
        std::string out = (r == kUp || r == kDown) ? "y" : (r == kEast || r == kWest) ? "x" : "z";
        if (name == "portal_axis" && out == "y") return Result::Unsupported;
        if (out == *s) return Result::Unchanged;
        *s = out;
        return Result::Changed;
    }
    // vertical axis only
    if (name == "ground_sign_direction") {
        auto* i = std::get_if<int>(&value);
        if (!i) return Result::Unchanged;
        if (!vertical(axis)) return Result::Unsupported;
        // 0 south, increasing clockwise seen from above (4 west, 8 north, 12 east)
        bool cwFromAbove = (axis == kUp) != ccw;
        *i               = ((*i + (cwFromAbove ? 4 : -4)) % 16 + 16) % 16;
        return Result::Changed;
    }
    if (name == "lever_direction") {
        auto* s = std::get_if<std::string>(&value);
        if (!s) return Result::Unchanged;
        if (!vertical(axis)) return Result::Unsupported;
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kSwap{
            {{"up_north_south", "up_east_west"},
             {"up_east_west", "up_north_south"},
             {"down_north_south", "down_east_west"},
             {"down_east_west", "down_north_south"}}
        };
        for (auto const& [a, b] : kSwap) {
            if (*s == a) {
                *s = std::string(b);
                return Result::Changed;
            }
        }
        int d = dirOfName(*s);
        if (d < 2) return Result::Unchanged;
        *s = std::string(kDirNames[static_cast<std::size_t>(rotateDir(d, axis, ccw))]);
        return Result::Changed;
    }
    if (name == "rail_direction") {
        auto* i = std::get_if<int>(&value);
        if (!i || *i < 0 || *i > 9) return Result::Unchanged;
        if (!vertical(axis)) return Result::Unsupported;
        bool cw = (axis == kUp) != ccw; // clockwise seen from above
        // straight: 0 NS, 1 EW; ascending: 2 E, 3 W, 4 N, 5 S; curves: 6 SE, 7 SW, 8 NW, 9 NE
        static constexpr std::array<int, 10> kCw{1, 0, 5, 4, 2, 3, 7, 8, 9, 6};
        static constexpr std::array<int, 10> kCcw{1, 0, 4, 5, 3, 2, 9, 6, 7, 8};
        *i = (cw ? kCw : kCcw)[static_cast<std::size_t>(*i)];
        return Result::Changed;
    }
    if (name == "vine_direction_bits") {
        if (!vertical(axis)) return Result::Unsupported;
        return mapBits(value, std::array{kSouth, kWest, kNorth, kEast}, axis, ccw);
    }
    if (name == "multi_face_direction_bits") {
        return mapBits(value, std::array{kDown, kUp, kSouth, kWest, kNorth, kEast}, axis, ccw);
    }
    return Result::Unchanged;
}

/// States that mark blocks a turn table must leave alone (two-block structures).
[[nodiscard]] inline bool blocksRotation(std::string_view stateName) {
    return stateName == "head_piece_bit" || stateName == "upper_block_bit" || stateName == "occupied_bit";
}

} // namespace cm::feature::mechanism::rotation
