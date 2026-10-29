// Placeable gunpowder: a fuse that burns along and lights TNT (docs/spec/tweak.md §1.2).

#include <algorithm>
#include <format>
#include <map>
#include <set>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::tweak {

namespace {
constexpr char kBurning[]     = "complementaries:burning";
constexpr int  kMaxStage      = 8;
constexpr char kDbKey[]       = "cm:tweak:burning";
constexpr int  kSideFaces[4]  = {2, 3, 4, 5};
constexpr char const* kSide[] = {"complementaries:north", "complementaries:south", "complementaries:west", "complementaries:east"};

struct Fuse {
    int ticks = 0;
};
std::map<core::GlobalPos, Fuse>& burning() {
    static std::map<core::GlobalPos, Fuse> m;
    return m;
}
std::set<core::GlobalPos>& pendingRefresh() {
    static std::set<core::GlobalPos> s;
    return s;
}

bool isTnt(Block const& b) { return blockName(b) == "minecraft:tnt"; }

bool isFireSource(Block const& b) {
    auto const& n = blockName(b);
    return n == "minecraft:fire" || n == "minecraft:soul_fire" || n == "minecraft:lava" || n == "minecraft:flowing_lava";
}

/// 0 none, 1 side, 2 up (the trail climbs the side of the neighbour block).
int connection(BlockSource& region, BlockPos const& pos, int face) {
    BlockPos     n  = offset(pos, face);
    Block const& nb = region.getBlock(n);
    if (isTrail(nb) || isTnt(nb)) return 1;
    if (isSolid(nb)) {
        if (!isSolid(region.getBlock(offset(pos, 1))) && isTrail(region.getBlock(offset(n, 1)))) return 2;
        return 0;
    }
    return isTrail(region.getBlock(offset(n, 0))) ? 1 : 0;
}

Block const* connected(BlockSource& region, BlockPos const& pos, Block const& base) {
    Block const* b = &base;
    for (int i = 0; i < 4; ++i) {
        if (auto const* next = withState(*b, kSide[i], connection(region, pos, kSideFaces[i]))) b = next;
    }
    return b;
}

/// Every position whose trail connections may change when `pos` changes.
template <class F>
void forAround(BlockPos const& pos, F&& fn) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int face : kSideFaces) {
            auto n = offset(pos, face);
            fn(BlockPos{n.x, n.y + dy, n.z});
        }
        if (dy != 0) fn(BlockPos{pos.x, pos.y + dy, pos.z});
    }
    fn(pos);
}

void queueRefreshAround(BlockSource& region, BlockPos const& pos) {
    int dim = static_cast<int>(region.getDimensionId());
    forAround(pos, [&](BlockPos const& p) { pendingRefresh().insert(game::globalPos(dim, p)); });
}

void primeTnt(BlockSource& region, BlockPos const& pos) {
    game::clearBlock(region, pos);
    static constexpr char const* kDims[] = {"overworld", "nether", "the_end"};
    int dim = static_cast<int>(region.getDimensionId());
    if (dim < 0 || dim > 2) return;
    // block centre as a number: "-5.5" would be the centre of block -6
    runCommand(std::format("execute in {} run summon tnt {:.1f} {} {:.1f}", kDims[dim], pos.x + 0.5, pos.y, pos.z + 0.5));
    playSound(region, Sound::Fuse, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f});
}

void spread(BlockSource& region, BlockPos const& pos) {
    for (int face = 0; face < 6; ++face) igniteAt(region, offset(pos, face));
    // trails one block up or down the side
    for (int face : kSideFaces) {
        auto n = offset(pos, face);
        for (int dy : {-1, 1}) {
            BlockPos p{n.x, n.y + dy, n.z};
            if (isTrail(region.getBlock(p))) igniteTrail(region, p);
        }
    }
}
} // namespace

bool isTrail(Block const& block) { return blockName(block) == kGunpowderBlock; }

void refreshTrail(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 1, false)) return;
    Block const& b = region.getBlock(pos);
    if (!isTrail(b)) return;
    if (!isSolid(region.getBlock(offset(pos, 0)))) return; // the placement filter pops it
    auto const* next = connected(region, pos, b);
    if (next && !(*next == b)) game::setBlock(region, pos, *next);
}

bool placeTrail(BlockSource& region, BlockPos const& pos) {
    if (!game::replaceable(region, pos) || !isSolid(region.getBlock(offset(pos, 0)))) return false;
    auto const* base = game::blockWith(kGunpowderBlock);
    if (!base) return false;
    auto const* block = connected(region, pos, *base);
    if (!game::setBlock(region, pos, *block)) return false;
    forAround(pos, [&](BlockPos const& p) {
        if (!(p == pos)) refreshTrail(region, p);
    });
    // a trail placed next to fire lights at once
    for (int face = 0; face < 6; ++face) {
        if (isFireSource(region.getBlock(offset(pos, face)))) {
            igniteTrail(region, pos);
            break;
        }
    }
    return true;
}

void igniteTrail(BlockSource& region, BlockPos const& pos) {
    if (!config().placeableGunpowder) return;
    Block const& b = region.getBlock(pos);
    if (!isTrail(b) || game::intState(b, kBurning).value_or(0) != 0) return;
    auto const* lit = withState(b, kBurning, 1);
    if (!lit || !game::setBlock(region, pos, *lit)) return;
    burning()[game::globalPos(region, pos)] = Fuse{};
    playSound(region, Sound::Ignite, Vec3{pos.x + 0.5f, pos.y + 0.1f, pos.z + 0.5f});
}

void igniteAt(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) return;
    Block const& b    = region.getBlock(pos);
    auto const&  name = blockName(b);
    if (isTrail(b)) {
        igniteTrail(region, pos);
    } else if (name == "minecraft:tnt") {
        primeTnt(region, pos);
    } else if (name == "minecraft:campfire" || name == "minecraft:soul_campfire") {
        if (game::intState(b, "extinguished").value_or(0) == 1) {
            if (auto const* lit = withState(b, "extinguished", 0)) game::setBlock(region, pos, *lit);
        }
    } else if (name.ends_with("candle") || name.ends_with("candle_cake")) {
        if (game::intState(b, "lit").value_or(1) == 0) {
            if (auto const* lit = withState(b, "lit", 1)) game::setBlock(region, pos, *lit);
        }
    }
}

bool gunpowderUseOn(UseOn const& use) {
    if (!config().placeableGunpowder) return false;
    auto& region   = use.player.getDimensionBlockSource();
    auto  playerId = use.player.getOrCreateUniqueID();
    int   dim      = static_cast<int>(region.getDimensionId());

    // lighting a trail
    if (isTrail(use.block) && (use.itemId == "minecraft:flint_and_steel" || use.itemId == "minecraft:fire_charge")) {
        if (game::intState(use.block, kBurning).value_or(0) != 0) return true; // already burning: nothing to use up
        BlockPos pos     = use.pos;
        auto     itemUse = use.itemId;
        defer([=] {
            auto* player = findPlayer(playerId);
            auto* r      = regionOf(dim);
            if (!player || !r) return;
            Block const& b = r->getBlock(pos);
            if (!isTrail(b) || game::intState(b, kBurning).value_or(0) != 0) return;
            igniteTrail(*r, pos);
            log().info("tweak: {} lit gunpowder at {} {} {}", player->getRealName(), pos.x, pos.y, pos.z);
            if (itemUse == "minecraft:fire_charge") {
                consumeSelected(*player);
            } else if (!player->isCreative()) {
                ItemStack item = player->getSelectedItem();
                if (itemId(item) == itemUse) {
                    item.hurtAndBreak(1, player);
                    setSelected(*player, item);
                }
            }
        });
        return true;
    }

    if (use.itemId != "minecraft:gunpowder") return false;
    if (isInteractive(use.block) && !use.player.isSneaking()) return false;
    BlockPos target = game::replaceable(region, use.pos) && !isTrail(use.block) ? use.pos : offset(use.pos, use.face);
    if (!game::replaceable(region, target) || !isSolid(region.getBlock(offset(target, 0)))) return false;
    defer([=] {
        auto* player = findPlayer(playerId);
        auto* r      = regionOf(dim);
        if (!player || !r || itemId(player->getSelectedItem()) != "minecraft:gunpowder") return;
        if (!placeTrail(*r, target)) return;
        playBlockSound(*r, Sound::Place, target, r->getBlock(target));
        consumeSelected(*player);
        log().info("tweak: {} placed gunpowder at {} {} {}", player->getRealName(), target.x, target.y, target.z);
    });
    return true;
}

void gunpowderBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now) {
    if (!config().placeableGunpowder) return;
    bool relevant = isTrail(previous) != isTrail(now) || isTnt(previous) != isTnt(now) || isSolid(previous) != isSolid(now);
    if (relevant) queueRefreshAround(region, pos);
    if (isFireSource(now) && !isFireSource(previous)) {
        int dim = static_cast<int>(region.getDimensionId());
        for (int face = 0; face < 6; ++face) {
            auto p = offset(pos, face);
            if (isTrail(region.getBlock(p))) {
                defer([dim, p] {
                    if (auto* r = regionOf(dim)) igniteTrail(*r, p);
                });
            }
        }
    }
}

void gunpowderTick() {
    if (!pendingRefresh().empty()) {
        auto todo = std::move(pendingRefresh());
        pendingRefresh().clear();
        for (auto const& gp : todo) {
            if (auto* r = regionOf(gp.dim)) refreshTrail(*r, game::blockPos(gp));
        }
    }
    if (burning().empty()) return;
    int const speed  = std::clamp(config().gunpowderBurnTicks, 1, 20);
    int const spreadAge = std::clamp(config().gunpowderSpreadAge, 1, kMaxStage);
    std::vector<core::GlobalPos> done;
    // copy: spreading inserts new fuses
    auto snapshot = burning();
    for (auto const& [gp, fuse] : snapshot) {
        auto* region = regionOf(gp.dim);
        auto  pos    = game::blockPos(gp);
        if (!region || !region->hasChunksAt(pos, 0, false)) continue; // resumes when loaded again
        Block const& b = region->getBlock(pos);
        if (!isTrail(b)) {
            done.push_back(gp);
            continue;
        }
        auto& f = burning()[gp];
        if (++f.ticks < speed) continue;
        f.ticks   = 0;
        int stage = game::intState(b, kBurning).value_or(0) + 1;
        if (stage > kMaxStage) {
            suppressDrops(gp);
            game::clearBlock(*region, pos);
            done.push_back(gp);
            continue;
        }
        if (auto const* next = withState(b, kBurning, stage)) game::setBlock(*region, pos, *next);
        if (stage == spreadAge) spread(*region, pos);
    }
    for (auto const& gp : done) burning().erase(gp);
}

std::size_t burningCount() { return burning().size(); }

void gunpowderLoad() {
    burning().clear();
    auto data = game::db::load(kDbKey);
    if (!data || !data->is_array()) return;
    for (auto const& e : *data) {
        if (!e.is_array() || e.size() != 4) continue;
        burning()[core::GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()}] = Fuse{};
    }
    if (!burning().empty()) log().info("tweak: resuming {} burning gunpowder trails", burning().size());
}

void gunpowderSave() {
    if (!game::db::available()) return;
    if (burning().empty()) {
        game::db::erase(kDbKey);
        return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (auto const& [gp, f] : burning()) arr.push_back({gp.dim, gp.x, gp.y, gp.z});
    game::db::save(kDbKey, arr);
}

void gunpowderClear() {
    burning().clear();
    pendingRefresh().clear();
}

} // namespace cm::feature::tweak
