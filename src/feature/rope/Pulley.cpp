// Pulley block: one slot of rope or chain (world DB), winds the column hanging under it and moves
// the block hanging at its end.

#include <map>
#include <set>

#include "ll/api/form/SimpleForm.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/rope/Common.h"
#include "feature/rope/Rope.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::rope::pulley {

namespace {
constexpr int kMaxStack = 64;

std::map<core::GlobalPos, Content>       gCache;
std::map<core::GlobalPos, std::uint64_t> gLastTurn; ///< tick of the last turn of each pulley

std::string dbKey(core::GlobalPos const& p) { return "cm:rope:pulley:" + p.key(); }

bool acceptsItem(std::string const& id) { return id == kRope || id == chainId() || id == "minecraft:chain"; }

/// Item id that matches a column block (rope / the chain's own id).
std::string columnItem(Block const& b) {
    if (isRope(b)) return std::string(kRope);
    if (isChain(b)) return b.getTypeName();
    return {};
}

int typeFor(Content const& c) {
    if (c.count <= 0 || c.item.empty()) return 0;
    return c.item == kRope ? 1 : 2;
}

void refreshState(BlockSource& r, BlockPos const& pos, Content const& c, bool flip) {
    auto const& b = r.getBlock(pos);
    if (!isPulley(b)) return;
    int         flipped = stateOf(b, "complementaries:flipped");
    auto const& nb      = withStates(
        b,
        {
            {"complementaries:type",    typeFor(c)               },
            {"complementaries:flipped", flip ? !flipped : flipped}
    }
    );
    if (&nb != &b) set(r, pos, nb);
}

/// Moves the block at `from` to `to` (to must be replaceable or part of the column being wound).
bool moveBlock(BlockSource& r, BlockPos const& from, BlockPos const& to) {
    auto const& b = r.getBlock(from);
    if (!set(r, to, b)) return false;
    setAir(r, from);
    return true;
}

Block const* blockForItem(std::string const& item) {
    if (item == kRope) return game::blockWith(kRope);
    return game::blockWith(item == "minecraft:chain" ? chainId() : item);
}

std::string itemName(Player const& p, std::string const& id) {
    if (id == kRope) return trFor(p, "cm.rope.item.rope", "Rope");
    return trFor(p, "cm.rope.item.chain", "Chain");
}
} // namespace

Content contentAt(core::GlobalPos const& pos) {
    if (auto it = gCache.find(pos); it != gCache.end()) return it->second;
    Content c;
    if (auto j = game::db::load(dbKey(pos))) {
        c.item  = j->value("item", std::string{});
        c.count = j->value("count", 0);
    }
    gCache[pos] = c;
    return c;
}

void setContent(core::GlobalPos const& pos, Content const& c) {
    gCache[pos] = c;
    if (c.count <= 0 || c.item.empty()) game::db::erase(dbKey(pos));
    else
        game::db::save(
            dbKey(pos),
            {
                {"item",  c.item },
                {"count", c.count}
        }
        );
}

void reset() {
    gCache.clear();
    gLastTurn.clear();
}

} // namespace cm::feature::rope::pulley

namespace cm::feature::rope {

bool turnPulley(BlockSource& r, BlockPos const& pos, bool clockwise, Actor* by) {
    if (!loaded(r, pos) || !isPulley(r.getBlock(pos))) return false;
    auto g = gpos(r, pos);
    auto c = pulley::contentAt(g);

    // the column of rope / chain hanging under the pulley
    BlockPos    top = neighbor(pos, 0);
    std::string kind;
    BlockPos    bottom    = top;
    bool        hasColumn = false;
    if (loaded(r, top)) {
        kind = pulley::columnItem(r.getBlock(top));
        if (!kind.empty()) {
            hasColumn = true;
            while (true) {
                auto below = neighbor(bottom, 0);
                if (!loaded(r, below) || pulley::columnItem(r.getBlock(below)) != kind) break;
                bottom = below;
            }
        }
    }

    if (clockwise) {
        // wind: take the lowest piece back, pull the hanging block up into its place
        if (!hasColumn) return false;
        if (c.count > 0 && c.item != kind) return false;
        if (c.count >= pulley::kMaxStack) return false;
        auto const& piece = r.getBlock(bottom);
        auto        hang  = neighbor(bottom, 0);
        // only a block that hangs (nothing under it) is pulled up: never the ground
        auto under   = neighbor(hang, 0);
        bool hanging = movable(r, hang) && pulley::columnItem(r.getBlock(hang)).empty() && loaded(r, under)
                    && replaceable(r, under);
        if (config().pulleyMovesBlocks && hanging) {
            if (!pulley::moveBlock(r, hang, bottom)) return false;
        } else if (!setAir(r, bottom)) {
            return false;
        }
        c.item   = kind;
        c.count += 1;
        pulley::setContent(g, c);
        pulley::gLastTurn[g] = now();
        pulley::refreshState(r, pos, c, true);
        if (by) blockSound(r, bottom, piece, false, *by);
        log().info(
            "rope: pulley at {} {} {} wound in {} ({} left on it: {})",
            pos.x,
            pos.y,
            pos.z,
            kind,
            c.item,
            c.count
        );
        return true;
    }

    // unwind: lower one more piece, pushing the hanging block down first
    if (c.count <= 0 || c.item.empty()) return false;
    if (hasColumn && kind != c.item && !(c.item == "minecraft:chain" && kind == chainId())) return false;
    BlockPos target = hasColumn ? neighbor(bottom, 0) : top;
    if (!loaded(r, target)) return false;
    if (!replaceable(r, target)) {
        auto below = neighbor(target, 0);
        if (!config().pulleyMovesBlocks || !movable(r, target) || !loaded(r, below) || !replaceable(r, below))
            return false;
        if (!pulley::moveBlock(r, target, below)) return false;
    }
    auto const* block = pulley::blockForItem(c.item);
    if (!block || !set(r, target, *block)) return false;
    c.count -= 1;
    if (c.count <= 0) c = {};
    pulley::setContent(g, c);
    pulley::gLastTurn[g] = now();
    pulley::refreshState(r, pos, c, true);
    if (by) blockSound(r, target, *block, true, *by);
    log().info(
        "rope: pulley at {} {} {} lowered a piece to {} {} {}",
        pos.x,
        pos.y,
        pos.z,
        target.x,
        target.y,
        target.z
    );
    return true;
}

namespace pulley {

namespace {
void openForm(Player& player, core::GlobalPos g);

void withPulley(Player& p, core::GlobalPos g, std::function<void(BlockSource&, BlockPos const&)> const& fn) {
    auto* r = serverRegion(g.dim);
    if (!r) return;
    auto pos = game::blockPos(g);
    if (!loaded(*r, pos) || !isPulley(r->getBlock(pos))) return;
    if (static_cast<int>(p.getDimensionId()) != g.dim) return;
    fn(*r, pos);
}

void putHeld(Player& p, core::GlobalPos g) {
    withPulley(p, g, [&](BlockSource& r, BlockPos const& pos) {
        auto held = p.getSelectedItem();
        auto id   = itemId(held);
        if (!acceptsItem(id)) {
            p.sendMessage(trFor(p, "cm.rope.pulley.wrong_item", "Only ropes or chains can be wound on a pulley"));
            return;
        }
        if (id == "minecraft:chain") id = chainId();
        auto c = contentAt(g);
        if (c.count > 0 && c.item != id) {
            p.sendMessage(trFor(p, "cm.rope.pulley.wrong_item", "Only ropes or chains can be wound on a pulley"));
            return;
        }
        int room = kMaxStack - c.count;
        if (room <= 0) {
            p.sendMessage(trFor(p, "cm.rope.pulley.full", "The pulley is full"));
            return;
        }
        int n    = std::min<int>(room, held.mCount);
        c.item   = id;
        c.count += n;
        if (survival(p)) consumeSelected(p, n);
        setContent(g, c);
        refreshState(r, pos, c, false);
        log().info("rope: {} put {} {} into the pulley at {}", p.getRealName(), n, id, g.key());
    });
}

void takeOut(Player& p, core::GlobalPos g) {
    withPulley(p, g, [&](BlockSource& r, BlockPos const& pos) {
        auto c = contentAt(g);
        if (c.count <= 0) return;
        giveOrDrop(p, makeStack(c.item, c.count));
        log().info("rope: {} took {} {} out of the pulley at {}", p.getRealName(), c.count, c.item, g.key());
        setContent(g, {});
        refreshState(r, pos, {}, false);
    });
}

void openForm(Player& player, core::GlobalPos g) {
    auto        c = contentAt(g);
    std::string content =
        c.count > 0 ? trFor(player, "cm.rope.pulley.content", "Wound: {} x{}", itemName(player, c.item), c.count)
                    : trFor(player, "cm.rope.pulley.empty", "Empty");
    ll::form::SimpleForm form;
    form.setTitle(trFor(player, "gui.supplementaries.pulley_block", "Pulley"));
    form.setContent(content);
    auto held = itemId(player.getSelectedItem());
    if (acceptsItem(held)) {
        form.appendButton(
            trFor(player, "cm.rope.pulley.put", "Put in held {}", itemName(player, held)),
            [g](Player& p) { putHeld(p, g); }
        );
    }
    if (c.count > 0)
        form.appendButton(trFor(player, "cm.rope.pulley.take", "Take out"), [g](Player& p) { takeOut(p, g); });
    form.appendButton(trFor(player, "cm.rope.pulley.turn_cw", "Turn clockwise"), [g](Player& p) {
        withPulley(p, g, [&](BlockSource& r, BlockPos const& pos) { turnPulley(r, pos, true, &p); });
    });
    form.appendButton(trFor(player, "cm.rope.pulley.turn_ccw", "Turn counter-clockwise"), [g](Player& p) {
        withPulley(p, g, [&](BlockSource& r, BlockPos const& pos) { turnPulley(r, pos, false, &p); });
    });
    form.sendTo(player);
    log().info("rope: opened the pulley screen at {} for {}", g.key(), player.getRealName());
}
} // namespace

bool onUse(Player& player, BlockPos const& pos) {
    if (player.isSneaking() && !player.getSelectedItem().isNull()) return false; // place against it
    auto g   = gpos(player.getDimensionBlockSource(), pos);
    auto uid = player.getOrCreateUniqueID().rawID;
    later(1, [uid, g] {
        if (auto* p = playerById(uid)) openForm(*p, g);
    });
    return true;
}

void onCrankUsed(Player& player, BlockPos const& crankPos, Block const& crank) {
    auto& r = player.getDimensionBlockSource();
    // The crank sits on a face of the block it turns: facing points away from that block.
    int facing = -1;
    for (auto name : {"minecraft:facing_direction", "complementaries:facing"}) {
        if (auto v = game::intState(crank, name)) {
            facing = *v;
            break;
        }
    }
    std::vector<BlockPos> candidates;
    if (facing >= 0 && facing < 6) candidates.push_back(neighbor(crankPos, oppositeFace(facing)));
    for (int f = 0; f < 6; ++f) candidates.push_back(neighbor(crankPos, f));
    bool clockwise = !player.isSneaking();
    auto uid       = player.getOrCreateUniqueID().rawID;
    auto dim       = static_cast<int>(r.getDimensionId());
    auto usedAt    = now();
    for (auto const& c : candidates) {
        if (!loaded(r, c) || !isPulley(r.getBlock(c))) continue;
        later(1, [uid, dim, c, clockwise, usedAt] {
            auto* rg = serverRegion(dim);
            if (!rg) return;
            // the crank unit may turn the pulley itself (turnPulley): then this fallback stays quiet
            if (auto it = gLastTurn.find(gpos(*rg, c)); it != gLastTurn.end() && it->second >= usedAt) return;
            turnPulley(*rg, c, clockwise, playerById(uid));
        });
        return;
    }
}

namespace {
std::set<core::GlobalPos> gRestoring; ///< pulleys whose rotation we are undoing

bool nextToTurnTable(BlockSource& r, BlockPos const& pos) {
    for (int f = 0; f < 6; ++f) {
        auto n = neighbor(pos, f);
        if (loaded(r, n) && r.getBlock(n).getTypeName() == kTurnTable) return true;
    }
    return false;
}
} // namespace

void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& prev, Block const& next) {
    if (!isPulley(prev)) return;
    if (isPulley(next)) {
        // A turn table "rotates" the pulley by changing its facing: keep the facing, turn the wheel.
        auto g = gpos(region, pos);
        if (gRestoring.erase(g)) return;
        if (stateOf(prev, "minecraft:block_face") == stateOf(next, "minecraft:block_face")) return;
        if (!nextToTurnTable(region, pos)) return;
        int face = stateOf(prev, "minecraft:block_face");
        later(1, [g, face] {
            auto* r = serverRegion(g.dim);
            if (!r) return;
            auto p = game::blockPos(g);
            if (!loaded(*r, p) || !isPulley(r->getBlock(p))) return;
            auto const& cur      = r->getBlock(p);
            auto const& restored = withStates(
                cur,
                {
                    {"minecraft:block_face", face}
            }
            );
            if (&restored != &cur) {
                gRestoring.insert(g);
                if (!set(*r, p, restored)) gRestoring.erase(g);
            }
            turnPulley(*r, p, true, nullptr);
        });
        return;
    }
    auto g = gpos(region, pos);
    auto c = contentAt(g);
    setContent(g, {});
    gCache.erase(g);
    if (c.count <= 0) return;
    later(1, [g, c] {
        if (auto* r = serverRegion(g.dim)) {
            dropAt(
                *r,
                Vec3{static_cast<float>(g.x) + 0.5f, static_cast<float>(g.y) + 0.5f, static_cast<float>(g.z) + 0.5f},
                makeStack(c.item, c.count)
            );
        }
    });
}

} // namespace pulley
} // namespace cm::feature::rope
