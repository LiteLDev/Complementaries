// Wrench: rotates blocks around the axis of the clicked face (sneak = counter-clockwise) and turns
// entities.

#include <algorithm>
#include <array>

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/tool/Tool.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::tool::wrench {

namespace {

struct V3 {
    int x = 0, y = 0, z = 0;
    bool operator==(V3 const&) const = default;
};

V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
int dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// Rotates `v` by 90 degrees about the unit axis `n`: clockwise as seen from the tip of `n`
/// (looking at the clicked face), counter-clockwise when `ccw`.
V3 rot(V3 v, V3 n, bool ccw) {
    V3  c = cross(n, v);
    int d = dot(n, v);
    if (ccw) return {c.x + n.x * d, c.y + n.y * d, c.z + n.z * d};
    return {-c.x + n.x * d, -c.y + n.y * d, -c.z + n.z * d};
}

// Bedrock face order: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
constexpr std::array<V3, 6> kFaces = {{{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}};
constexpr std::array<std::string_view, 6> kFaceNames = {"down", "up", "north", "south", "west", "east"};

int faceIndex(V3 v) {
    for (int i = 0; i < 6; ++i)
        if (kFaces[i] == v) return i;
    return -1;
}

/// A horizontal order of directions as faces (e.g. {south, west, north, east}).
using Order = std::array<int, 4>;
constexpr Order kSWNE    = {3, 4, 2, 5}; // "direction" of most blocks
constexpr Order kEWSN    = {5, 4, 3, 2}; // stairs "weirdo_direction", trapdoor "direction"

int orderIndex(Order const& o, int face) {
    for (int i = 0; i < 4; ++i)
        if (o[i] == face) return i;
    return -1;
}

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

/// Blocks whose rotation would break a structure or detach them.
bool unrotatable(std::string_view id) {
    static constexpr std::array<std::string_view, 22> parts = {
        "bed", "_door", "minecraft:door", "piston_arm", "portal", "torch", "ladder", "wall_sign", "wall_banner",
        "button", "lever", "tripwire", "vine", "cocoa", "bell", "tall_grass", "large_fern", "sunflower",
        "lilac", "rose_bush", "peony", "pitcher_plant",
    };
    for (auto p : parts)
        if (contains(id, p)) return true;
    return id == "minecraft:wooden_door" || contains(id, "dripleaf");
}

bool sixWay(std::string_view id) {
    static constexpr std::array<std::string_view, 12> six = {
        "observer", "dispenser", "dropper", "piston", "barrel", "end_rod", "lightning_rod", "command_block",
        "amethyst_cluster", "amethyst_bud", "crafter", "chain",
    };
    for (auto p : six)
        if (contains(id, p)) return true;
    return false;
}

bool pairedChest(BlockSource& region, BlockPos const& pos, std::string_view id) {
    if (!contains(id, "chest") || contains(id, "ender_chest")) return false;
    for (int f = 2; f <= 5; ++f) {
        auto o = kFaces[f];
        if (game::blockIdAt(region, BlockPos{pos.x + o.x, pos.y, pos.z + o.z}) == id) return true;
    }
    return false;
}

/// Rotates a horizontal direction (as a face index) about the vertical axis.
int rotHorizontal(int face, bool ccw) { return faceIndex(rot(kFaces[face], kFaces[1], ccw)); }

} // namespace

Block const* rotated(Block const& block, int face, bool ccw) {
    std::string id = block.getTypeName();
    if (unrotatable(id)) return nullptr;
    auto const& bl = config().wrench.blacklist;
    if (std::find(bl.begin(), bl.end(), id) != bl.end()) return nullptr;
    if (face < 0 || face > 5) face = 1;
    bool verticalClick = face <= 1;
    V3   axis          = kFaces[face];
    auto states        = blockStates(block);
    bool changed       = false;
    for (auto& [name, value] : states) {
        if (name == "minecraft:cardinal_direction" && std::holds_alternative<std::string>(value)) {
            auto& s  = std::get<std::string>(value);
            int   fi = std::find(kFaceNames.begin(), kFaceNames.end(), s) - kFaceNames.begin();
            if (fi < 2 || fi > 5) continue;
            s       = kFaceNames[rotHorizontal(fi, ccw)];
            changed = true;
        } else if ((name == "direction" || name == "weirdo_direction") && std::holds_alternative<int>(value)) {
            Order const& o  = (name == "weirdo_direction" || contains(id, "trapdoor")) ? kEWSN : kSWNE;
            int&         v  = std::get<int>(value);
            if (v < 0 || v > 3) continue;
            v       = orderIndex(o, rotHorizontal(o[v], ccw));
            changed = true;
        } else if (name == "ground_sign_direction" && std::holds_alternative<int>(value)) {
            int& v  = std::get<int>(value);
            v       = ((v + (ccw ? -4 : 4)) % 16 + 16) % 16;
            changed = true;
        } else if ((name == "facing_direction" && std::holds_alternative<int>(value))
                   || ((name == "minecraft:facing_direction" || name == "minecraft:block_face") && std::holds_alternative<std::string>(value))) {
            int cur = -1;
            if (std::holds_alternative<int>(value)) cur = std::get<int>(value);
            else cur = std::find(kFaceNames.begin(), kFaceNames.end(), std::get<std::string>(value)) - kFaceNames.begin();
            if (cur < 0 || cur > 5) continue;
            int next;
            if (sixWay(id) || !verticalClick) {
                if (!sixWay(id) && cur >= 2) next = rotHorizontal(cur, ccw); // horizontal-only block
                else next = faceIndex(rot(kFaces[cur], axis, ccw));
            } else {
                next = cur >= 2 ? faceIndex(rot(kFaces[cur], axis, ccw)) : cur;
            }
            if (next < 0 || next == cur) continue;
            if (std::holds_alternative<int>(value)) value = next;
            else value = std::string(kFaceNames[next]);
            changed = true;
        } else if (name == "pillar_axis" && std::holds_alternative<std::string>(value)) {
            auto& s   = std::get<std::string>(value);
            V3    a   = s == "x" ? V3{1, 0, 0} : s == "z" ? V3{0, 0, 1} : V3{0, 1, 0};
            V3    r   = rot(a, axis, ccw);
            std::string n = r.x ? "x" : r.z ? "z" : "y";
            if (n == s) continue;
            s       = n;
            changed = true;
        } else if (name == "rail_direction" && std::holds_alternative<int>(value)) {
            int& v = std::get<int>(value);
            if (v == 0 || v == 1) {
                v = 1 - v;
            } else if (v >= 2 && v <= 5) {
                // ascending east / west / north / south as faces
                constexpr std::array<int, 4> asc = {5, 4, 2, 3};
                int f = rotHorizontal(asc[v - 2], ccw);
                v     = static_cast<int>(std::find(asc.begin(), asc.end(), f) - asc.begin()) + 2;
            } else if (v >= 6 && v <= 9) {
                v = 6 + ((v - 6 + (ccw ? 3 : 1)) % 4);
            } else {
                continue;
            }
            changed = true;
        }
    }
    if (!changed) return nullptr;
    auto const* out = blockWithStates(id, states);
    return out == &block ? nullptr : out;
}

bool rotateAt(BlockSource& region, BlockPos const& pos, int face, bool ccw) {
    auto const& block = region.getBlock(pos);
    if (block.isAir()) return false;
    std::string id = block.getTypeName();
    if (pairedChest(region, pos, id)) return false;
    // an extended piston has its arm in front
    if (contains(id, "piston")) {
        for (auto o : kFaces) {
            if (contains(game::blockIdAt(region, BlockPos{pos.x + o.x, pos.y + o.y, pos.z + o.z}), "piston_arm")) return false;
        }
    }
    auto const* next = rotated(block, face, ccw);
    if (!next) return false;
    return replaceKeepingData(region, pos, *next);
}

bool useOnBlock(Player& player, BlockPos const& pos, int face) {
    if (!config().wrench.enabled) return false;
    auto& region = player.getDimensionBlockSource();
    bool  ccw    = player.isSneaking();
    if (!rotateAt(region, pos, face, ccw)) return false;
    damageMainhand(player, 1);
    playSound(static_cast<int>(region.getDimensionId()), center(pos), "random.click", 0.6f, ccw ? 0.8f : 1.0f);
    log().info("wrench rotated {} at {} {} {}", region.getBlock(pos).getTypeName(), pos.x, pos.y, pos.z);
    return true;
}

bool useOnEntity(Player& player, Actor& target) {
    if (!config().wrench.enabled || target.isPlayer()) return false;
    bool ccw = player.isSneaking();
    auto r   = target.getRotation();
    target.teleport(target.getPosition(), target.getDimensionId(), Vec2{r.x, r.y + (ccw ? -45.0f : 45.0f)});
    damageMainhand(player, 1);
    playSound(static_cast<int>(target.getDimensionId()), target.getPosition(), "random.click", 0.6f, ccw ? 0.8f : 1.0f);
    log().info("wrench turned a {}", target.getTypeName());
    return true;
}

} // namespace cm::feature::tool::wrench
