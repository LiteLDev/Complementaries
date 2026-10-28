// Awnings and buntings: placement rules, survival and the bounce normal.

#include "feature/flag/Flag.h"

#include <cmath>
#include <numbers>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/Blocks.h"
#include "mod/Config.h"

namespace cm::feature::flag {

namespace {
std::optional<AwningState> awningAt(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) return std::nullopt;
    return awningState(region.getBlock(pos));
}
} // namespace

std::optional<AwningState> awningState(Block const& block) {
    if (!isAwning(game::blockId(block))) return std::nullopt;
    AwningState s;
    s.facing2d = game::intState(block, "complementaries:facing").value_or(2) & 3;
    s.bottom   = game::intState(block, "complementaries:bottom").value_or(0) != 0;
    s.slanted  = game::intState(block, "complementaries:slanted").value_or(0) != 0;
    return s;
}

bool awningCanSurvive(BlockSource& region, BlockPos const& pos, AwningState const& s) {
    int      facing = dirFrom2d(s.facing2d);
    BlockPos behind = offset(pos, oppositeDir(facing));
    if (solid(region, behind)) return true;
    if (auto b = awningAt(region, behind)) {
        if (!b->slanted) return true;
        if (s.bottom) return true;
    }
    if (auto ba = awningAt(region, offset(behind, Up)); ba && ba->slanted) return true;
    return solid(region, offset(pos, clockwise(facing))) || solid(region, offset(pos, counterClockwise(facing)));
}

std::optional<AwningState>
awningPlacement(BlockSource& region, BlockPos const& pos, int face, double clickY, std::array<int, 6> const& looking) {
    auto const&      cfg = config::get<FlagConfig>();
    std::vector<int> dirs(looking.begin(), looking.end());
    bool             slanted = false;
    if (auto clickedOn = awningAt(region, offset(pos, oppositeDir(face)))) {
        int dir = oppositeDir(dirFrom2d(clickedOn->facing2d));
        if (looking[0] == dir) {
            std::erase(dirs, dir);
            dirs.insert(dirs.begin(), dir);
            if (clickedOn->slanted) slanted = true;
        }
    }
    for (int direction : dirs) {
        if (!horizontal(direction)) continue;
        int                   opposite = oppositeDir(direction);
        bool                  bottom   = face != Down && (face == Up || !(clickY > 0.5));
        std::vector<BlockPos> behind;
        if (cfg.awningSlant) {
            behind.push_back(offset(pos, direction));
            if (!bottom) behind.push_back(offset(offset(pos, direction), Up));
        }
        for (size_t i = 0; i < behind.size(); ++i) {
            auto b = awningAt(region, behind[i]);
            if (b && b->slanted && dirFrom2d(b->facing2d) == oppositeDir(direction)) {
                bottom  = i == 0;
                slanted = true;
                break;
            }
        }
        AwningState s{dir2d(opposite), bottom, slanted};
        if (awningCanSurvive(region, pos, s)) return s;
    }
    return std::nullopt;
}

bool toggleAwningSlant(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    auto        s = awningState(b);
    if (!s) return false;
    if (!place(
            region,
            pos,
            game::blockId(b),
            {
                {"complementaries:facing",  s->facing2d       },
                {"complementaries:bottom",  s->bottom ? 1 : 0 },
                {"complementaries:slanted", s->slanted ? 0 : 1}
    }
        ))
        return false;
    playBlockSound(region, pos, "use.cloth", 1.f, 0.95f);
    return true;
}

std::array<double, 3> awningNormal(AwningState const& s, double angleDeg) {
    if (!s.slanted) return {0, 1, 0};
    double a = (90.0 - angleDeg) * std::numbers::pi / 180.0;
    // rotate (0,1,0) about +X by a, then about +Y by -yRot(facing) (Java yRot: S 0, W 90, N 180, E 270)
    double x = 0, y = std::cos(a), z = std::sin(a);
    double t  = -(s.facing2d * 90.0) * std::numbers::pi / 180.0;
    double nx = x * std::cos(t) + z * std::sin(t);
    double nz = -x * std::sin(t) + z * std::cos(t);
    return {nx, y, nz};
}

std::array<double, 3> reflect(std::array<double, 3> const& v, std::array<double, 3> const& n) {
    double d = 2 * (v[0] * n[0] + v[1] * n[1] + v[2] * n[2]);
    return {v[0] - d * n[0], v[1] - d * n[1], v[2] - d * n[2]};
}

// ---- buntings ------------------------------------------------------------------------------------------------

bool buntingCeilingCanSurvive(BlockSource& region, BlockPos const& pos) {
    return sturdyFace(region, offset(pos, Up), Down);
}

bool buntingWallCanSurvive(BlockSource& region, BlockPos const& pos, int facing2d) {
    int facing = dirFrom2d(facing2d);
    return sturdyFace(region, offset(pos, oppositeDir(facing)), facing);
}

std::optional<BuntingPlacement>
buntingPlacement(BlockSource& region, BlockPos const& pos, int color, std::array<int, 6> const& looking) {
    std::string const name(kColorNames[color]);
    for (int direction : looking) {
        if (direction == Down) continue; // the attachment direction (up) reversed
        if (direction == Up) {
            if (!buntingCeilingCanSurvive(region, pos)) continue;
            // axis of the player's horizontal look direction: 0 = z, 1 = x
            int horiz = North;
            for (int d : looking)
                if (horizontal(d)) {
                    horiz = d;
                    break;
                }
            return BuntingPlacement{"complementaries:bunting_" + name, (horiz == West || horiz == East) ? 1 : 0};
        }
        // wall block: its own placement tries the horizontal looking directions in order
        for (int d : looking) {
            if (!horizontal(d)) continue;
            int facing2d = dir2d(oppositeDir(d));
            if (buntingWallCanSurvive(region, pos, facing2d))
                return BuntingPlacement{"complementaries:bunting_wall_" + name, facing2d};
        }
    }
    return std::nullopt;
}

} // namespace cm::feature::flag
