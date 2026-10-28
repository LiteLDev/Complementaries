// Rope block: support distance, connections, placement, pulling up and bells.

#include <algorithm>
#include <set>

#include "mc/world/Direction.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BellBlockActor.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"

#include "feature/rope/Common.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::rope::ropes {

namespace {
std::set<core::GlobalPos> gUpdate; ///< ropes to recompute next tick
std::set<core::GlobalPos> gRetry;  ///< ropes next to unloaded chunks, retried every second

constexpr int kMaxDistance = 7;

bool isBell(Block const& b) { return b.getTypeName() == "minecraft:bell"; }

/// Blocks a rope can hang from (the block above the rope).
bool supportsFromAbove(BlockSource& r, BlockPos const& above) {
    auto const& b = r.getBlock(above);
    if (b.isAir() || isRope(b)) return false;
    if (b.canProvideSupport(0, BlockSupportType::Center)) return true; // sturdy bottom face
    auto const& t = b.getBlockType();
    return isChain(b) || isBell(b) || b.getTypeName().ends_with("lantern") || t.isFenceBlock() || t.isWallBlock()
        || t.isThinFenceBlock() || isPulley(b);
}

/// A sturdy side face the rope may be tied to (`towardRope` = face of the neighbour that touches the rope).
bool sideSupport(BlockSource& r, BlockPos const& side, int towardRope) {
    auto const& b = r.getBlock(side);
    if (b.isAir() || isRope(b)) return false;
    return b.canProvideSupport(static_cast<uchar>(towardRope), BlockSupportType::Center);
}

/// Things that hang from or stand under a rope (the block below the rope).
bool attachesBelow(Block const& b) {
    if (b.isAir()) return false;
    if (isRope(b) || isChain(b) || isBell(b) || isPulley(b)) return true;
    auto const& id = b.getTypeName();
    if (id.ends_with("lantern")) return true;
    return b.canProvideSupport(1, BlockSupportType::Center); // sturdy top face (rope reaches the floor)
}

bool connectsSide(Block const& nb) {
    auto const& t = nb.getBlockType();
    return t.isFenceBlock() || t.isWallBlock() || t.isThinFenceBlock();
}

bool hangs(BlockSource& r, BlockPos const& pos) {
    auto above = neighbor(pos, 1);
    return isRope(r.getBlock(above)) || supportsFromAbove(r, above);
}

bool neighboursLoaded(BlockSource& r, BlockPos const& pos) {
    for (int f = 0; f < 6; ++f) {
        auto n = neighbor(pos, f);
        if (n.y < r.getMinHeight() || n.y >= r.getMaxHeight()) continue;
        if (!r.hasChunksAt(n, 0, false)) return false;
    }
    return true;
}

struct Shape {
    bool up = false, down = false, north = false, south = false, west = false, east = false, knot = false;
    int  distance = kMaxDistance;
};

Shape shapeAt(BlockSource& r, BlockPos const& pos) {
    Shape s;
    s.distance    = distanceAt(r, pos);
    s.up          = hangs(r, pos);
    s.down        = attachesBelow(r.getBlock(neighbor(pos, 0)));
    bool* side[6] = {nullptr, nullptr, &s.north, &s.south, &s.west, &s.east};
    int   h       = 0;
    for (int f = 2; f < 6; ++f) {
        auto        np = neighbor(pos, f);
        auto const& nb = r.getBlock(np);
        bool        c  = false;
        if (isRope(nb)) c = !(s.up && hangs(r, np)); // two hanging ropes side by side stay apart
        else if (connectsSide(nb)) c = true;
        else if (config().blockSideAttachment && !s.up && sideSupport(r, np, oppositeFace(f))) c = true;
        *side[f]  = c;
        h        += c ? 1 : 0;
    }
    bool vertical = s.up || s.down;
    bool straight = h == 0 || (h == 2 && !vertical && ((s.north && s.south) || (s.west && s.east)));
    s.knot        = !straight;
    return s;
}

Block const* ropeWith(Block const& base, Shape const& s) {
    return &withStates(
        base,
        {
            {"complementaries:up", s.up},
            {"complementaries:down", s.down},
            {"complementaries:north", s.north},
            {"complementaries:south", s.south},
            {"complementaries:west", s.west},
            {"complementaries:east", s.east},
            {"complementaries:knot", s.knot},
            {"complementaries:distance", std::min(s.distance, kMaxDistance)}
    }
    );
}


} // namespace

Block const* shapedRope(BlockSource& r, BlockPos const& pos) {
    auto const* base = game::blockWith(kRope);
    if (!base) return nullptr;
    return ropeWith(*base, shapeAt(r, pos));
}

int distanceAt(BlockSource& r, BlockPos const& pos) {
    auto        above = neighbor(pos, 1);
    auto const& a     = r.getBlock(above);
    int         d     = kMaxDistance;
    if (isRope(a)) d = stateOf(a, "complementaries:distance");
    else if (supportsFromAbove(r, above)) return 0;
    for (int f = 2; f < 6; ++f) {
        auto        np = neighbor(pos, f);
        auto const& nb = r.getBlock(np);
        if (isRope(nb)) {
            d = std::min(d, stateOf(nb, "complementaries:distance") + 1);
        } else if (config().blockSideAttachment && sideSupport(r, np, oppositeFace(f))) {
            return 0;
        }
    }
    return std::min(d, kMaxDistance);
}

bool update(BlockSource& r, BlockPos const& pos) {
    if (!loaded(r, pos)) return false;
    auto const& b = r.getBlock(pos);
    if (!isRope(b)) return false;
    if (!neighboursLoaded(r, pos)) { // never judge a rope by an unloaded neighbour: retry later
        gRetry.insert(gpos(r, pos));
        return true;
    }
    auto s = shapeAt(r, pos);
    if (s.distance >= kMaxDistance) {
        destroy(r, pos, true);
        return true;
    }
    auto const* nb = ropeWith(b, s);
    if (nb != &b) set(r, pos, *nb);
    return true;
}

BlockPos bottomOf(BlockSource& r, BlockPos const& pos) {
    BlockPos p = pos;
    while (true) {
        auto below = neighbor(p, 0);
        if (!loaded(r, below) || !isRope(r.getBlock(below))) return p;
        p = below;
    }
}

std::optional<BlockPos> addRopeDown(BlockSource& r, BlockPos const& pos) {
    if (!isRope(r.getBlock(pos))) return std::nullopt;
    auto target = neighbor(bottomOf(r, pos), 0);
    if (!loaded(r, target) || !replaceable(r, target)) return std::nullopt;
    auto const* rope = shapedRope(r, target);
    if (!rope || !set(r, target, *rope)) return std::nullopt;
    return target;
}

std::optional<BlockPos> removeRopeDown(BlockSource& r, BlockPos const& pos, bool allowSelf) {
    if (!isRope(r.getBlock(pos))) return std::nullopt;
    auto bottom = bottomOf(r, pos);
    if (bottom == pos && !allowSelf) return std::nullopt;
    if (!setAir(r, bottom)) return std::nullopt;
    return bottom;
}

bool ringConnectedBell(BlockSource& r, BlockPos const& pos, Actor* by) {
    BlockPos p = pos;
    for (int i = 0; i <= config().bellChainLength; ++i) {
        p = neighbor(p, 1);
        if (!loaded(r, p)) return false;
        auto const& b = r.getBlock(p);
        if (isBell(b)) {
            auto* actor = const_cast<BlockActor*>(r.getBlockEntity(p));
            if (!actor || actor->getType() != BlockActorType::Bell) return false;
            auto dir = by ? static_cast<Direction::Type>(game::playerFacing(*by)) : Direction::Type::South;
            static_cast<BellBlockActor*>(actor)->ring(dir, r, by, true);
            return true;
        }
        if (!isRope(b) && !isChain(b)) return false;
    }
    return false;
}

bool onUse(Player& player, BlockPos const& pos, int face, ItemStack const& held) {
    (void)face;
    auto&      r   = player.getDimensionBlockSource();
    auto const id  = itemId(held);
    auto const uid = player.getOrCreateUniqueID().rawID;
    auto const dim = static_cast<int>(r.getDimensionId());
    if (id == kRope) {
        auto target = neighbor(bottomOf(r, pos), 0);
        if (!loaded(r, target) || !replaceable(r, target)) return false; // normal placement on the face
        later(1, [uid, dim, pos] {
            auto* pl = playerById(uid);
            auto* rg = serverRegion(dim);
            if (!pl || !rg) return;
            if (survival(*pl) && itemId(pl->getSelectedItem()) != kRope) return; // switched items meanwhile
            if (auto placed = addRopeDown(*rg, pos)) {
                consumeSelected(*pl);
                blockSound(*rg, *placed, rg->getBlock(*placed), true, *pl);
                log().info(
                    "rope: {} extended a rope down to {} {} {}",
                    pl->getRealName(),
                    placed->x,
                    placed->y,
                    placed->z
                );
            }
        });
        return true;
    }
    if (held.isNull()) {
        if (player.isSneaking()) return false;
        later(1, [uid, dim, pos] {
            auto* pl = playerById(uid);
            auto* rg = serverRegion(dim);
            if (!pl || !rg || !isRope(rg->getBlock(pos))) return;
            if (config().bellRinging && ringConnectedBell(*rg, pos, pl)) {
                log().info("rope: {} rang a bell through a rope", pl->getRealName());
                return;
            }
            auto const& rope = rg->getBlock(pos);
            if (auto removed = removeRopeDown(*rg, neighbor(pos, 0), true)) {
                if (survival(*pl)) giveOrDrop(*pl, makeStack(kRope, 1));
                blockSound(*rg, *removed, rope, false, *pl);
                log().info(
                    "rope: {} pulled up a rope from {} {} {}",
                    pl->getRealName(),
                    removed->x,
                    removed->y,
                    removed->z
                );
            }
        });
        return true;
    }
    if (id == kRopeArrow) return arrow::onUseOnRope(player, pos);
    return false;
}

bool refusePlacement(Player& player, BlockPos const& clicked, int face) {
    auto& r      = player.getDimensionBlockSource();
    auto  target = replaceable(r, clicked) ? clicked : neighbor(clicked, face);
    if (!loaded(r, target) || !replaceable(r, target)) return false; // vanilla refuses anyway
    return distanceAt(r, target) >= kMaxDistance;
}

void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& prev, Block const& next) {
    (void)prev;
    if (!types().rope) return;
    if (isRope(next)) gUpdate.insert(gpos(region, pos));
    // like neighbour updates: the ropes around a changed block are recomputed next tick
    for (int f = 0; f < 6; ++f) {
        auto n = neighbor(pos, f);
        if (n.y < region.getMinHeight() || n.y >= region.getMaxHeight()) continue;
        if (isRope(region.getBlock(n))) gUpdate.insert(gpos(region, n));
    }
}

void tick() {
    if (!gRetry.empty() && now() % 20 == 0) {
        gUpdate.insert(gRetry.begin(), gRetry.end());
        gRetry.clear();
    }
    if (gUpdate.empty()) return;
    auto update = std::move(gUpdate);
    gUpdate.clear();
    for (auto const& g : update) {
        if (auto* r = serverRegion(g.dim)) ropes::update(*r, game::blockPos(g));
    }
}

void reset() {
    gUpdate.clear();
    gRetry.clear();
}

} // namespace cm::feature::rope::ropes

namespace cm::feature::rope {
bool isRopeAt(BlockSource& region, BlockPos const& pos) { return loaded(region, pos) && isRope(region.getBlock(pos)); }
} // namespace cm::feature::rope
