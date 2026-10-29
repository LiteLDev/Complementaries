// Lock block, doors, trapdoors and gates: use, placement, removal, redstone and timers.

#include "feature/lock/Lock.h"

#include <cmath>
#include <map>
#include <set>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::lock {

namespace {

constexpr int cw(int f) { return (f + 1) & 3; }
constexpr int ccw(int f) { return (f + 3) & 3; }

BlockPos offset(BlockPos const& p, int dx, int dy, int dz) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }
BlockPos above(BlockPos const& p) { return offset(p, 0, 1, 0); }
BlockPos below(BlockPos const& p) { return offset(p, 0, -1, 0); }
BlockPos side(BlockPos const& p, int facing) {
    auto [dx, dz] = game::facingOffset(facing);
    return offset(p, dx, 0, dz);
}
/// Bedrock FacingID -> Java 2D facing (-1 for up / down).
int faceToFacing(int face) {
    switch (face) {
    case 2:
        return 2; // north
    case 3:
        return 0; // south
    case 4:
        return 1; // west
    case 5:
        return 3; // east
    default:
        return -1;
    }
}

float pitch09() { return randomFloat() * 0.1f + 0.9f; }

void doorSound(int dim, BlockPos const& pos, bool open) {
    // gold and iron block sets: iron door sounds
    playSound(dim, pos, open ? "complementaries.lock.iron_door.open" : "complementaries.lock.iron_door.close", 1.0f, pitch09());
}
void trapdoorSound(int dim, BlockPos const& pos, bool open) {
    playSound(dim, pos, open ? "complementaries.lock.iron_trapdoor.open" : "complementaries.lock.iron_trapdoor.close", 1.0f, pitch09());
}

bool isBars(Block const& b) {
    auto const& id = b.getTypeName();
    return id == "minecraft:iron_bars" || id == "complementaries:gold_bars";
}

/// Toggles both halves of the door whose lower half is at `lower`.
void toggleDoor(int dim, BlockPos lower) {
    defer([dim, lower] {
        auto* region = regionOf(dim);
        if (!region) return;
        auto const& lo = region->getBlock(lower);
        auto        k  = kindOf(lo);
        if (!isDoor(k) || flag(lo, kUpper)) return;
        bool open = !flag(lo, kOpen);
        place(*region, lower, with(lo, {{kOpen, open}}));
        auto const& up = region->getBlock(above(lower));
        if (kindOf(up) == k && flag(up, kUpper)) place(*region, above(lower), with(up, {{kOpen, open}}));
        doorSound(dim, lower, open);
    });
}

void toggleTrapdoor(int dim, BlockPos pos) {
    defer([dim, pos] {
        auto* region = regionOf(dim);
        if (!region) return;
        auto const& b = region->getBlock(pos);
        if (!isTrapdoor(kindOf(b))) return;
        bool open = !flag(b, kOpen);
        place(*region, pos, with(b, {{kOpen, open}}));
        trapdoorSound(dim, pos, open);
    });
}

/// Java IronGateBlock.openGate: closes, or opens turned away from the player.
void openGate(BlockSource& region, BlockPos const& pos, Block const& b, float yaw) {
    if (flag(b, kOpen)) {
        place(region, pos, with(b, {{kOpen, 0}}));
    } else {
        int f = gateOpenFacing(yaw, state(b, kFacing));
        place(region, pos, with(b, {{kFacing, f}, {kOpen, 1}}));
    }
}

bool sameGateIgnoringWall(Block const& a, Block const& b) {
    return a.getTypeName() == b.getTypeName() && state(a, kFacing) == state(b, kFacing) && flag(a, kOpen) == flag(b, kOpen)
        && flag(a, kPowered) == flag(b, kPowered);
}

// ---- lock block timers (Java scheduleTick(20)) -----------------------------------------------------
std::map<core::GlobalPos, long long> gTimers; // pos -> due tick
constexpr char const*                kTimersKey = "cm:lock:timers";

// ---- tracked redstone readers ------------------------------------------------------------------------
std::set<core::GlobalPos> gTracked;
bool                      gTrackedDirty = false;
constexpr char const*     kTrackedKey   = "cm:lock:tracked";

} // namespace

// ---- lock block ------------------------------------------------------------------------------------

bool useLockBlock(Player& player, ItemStack const& held, Click const& click) {
    auto* region = regionOf(click.pos.dim);
    if (!region) return false;
    auto        bpos = game::blockPos(click.pos);
    auto const& b    = region->getBlock(bpos);
    if (flag(b, kPowered)) return true; // FAIL: consumed, nothing happens
    if (!handleKeyAction(player, held, click.sneaking, click.pos, "lock_block")) return true;
    auto pos = click.pos;
    defer([pos] {
        auto* region = regionOf(pos.dim);
        if (!region) return;
        auto        bp = game::blockPos(pos);
        auto const& cur = region->getBlock(bp);
        if (kindOf(cur) != Kind::LockBlock) return;
        place(*region, bp, with(cur, {{kPowered, 1}}));
        gTimers[pos] = detail::currentTick() + 20;
    });
    playSound(click.pos.dim, bpos, "complementaries.lock.iron_trapdoor.open", 1.0f, pitch09());
    log().info("lock: {} opened the lock block at {}", player.getRealName(), click.pos.key());
    return true;
}

void tickTimers() {
    if (gTimers.empty()) return;
    auto now = detail::currentTick();
    for (auto it = gTimers.begin(); it != gTimers.end();) {
        if (it->second > now) {
            ++it;
            continue;
        }
        auto pos = it->first;
        it       = gTimers.erase(it);
        auto* region = regionOf(pos.dim);
        if (!region) continue;
        auto bp = game::blockPos(pos);
        if (!region->hasChunksAt(bp, 0, false)) {
            gTimers[pos] = now + 20; // unpower once the chunk is back
            break;
        }
        auto const& b = region->getBlock(bp);
        if (kindOf(b) == Kind::LockBlock && flag(b, kPowered)) place(*region, bp, with(b, {{kPowered, 0}}));
    }
}

void loadTimers() {
    gTimers.clear();
    auto v = game::db::load(kTimersKey);
    if (!v || !v->is_array()) return;
    auto now = detail::currentTick();
    for (auto const& e : *v) {
        if (!e.is_array() || e.size() != 5) continue;
        gTimers[{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()}] = now + std::max(1, e[4].get<int>());
    }
}

void saveTimers() {
    auto arr = nlohmann::json::array();
    auto now = detail::currentTick();
    for (auto const& [p, due] : gTimers) arr.push_back({p.dim, p.x, p.y, p.z, static_cast<int>(std::max(1LL, due - now))});
    if (arr.empty()) game::db::erase(kTimersKey);
    else game::db::save(kTimersKey, arr);
}

void clearTimers() { gTimers.clear(); }

// ---- doors -----------------------------------------------------------------------------------------

bool useDoor(Player& player, ItemStack const& held, Click const& click) {
    auto* region = regionOf(click.pos.dim);
    if (!region) return false;
    auto        p     = game::blockPos(click.pos);
    auto const& b     = region->getBlock(p);
    auto        k     = kindOf(b);
    BlockPos    lower = flag(b, kUpper) ? below(p) : p;
    if (k == Kind::GoldDoor) {
        if (flag(b, kPowered)) return false; // locked by redstone: the click passes on
        toggleDoor(click.pos.dim, lower);
        return true;
    }
    if (player.isSpectator()) return true;
    if (handleKeyAction(player, held, click.sneaking, game::globalPos(click.pos.dim, lower), "door")) {
        toggleDoor(click.pos.dim, lower);
    }
    return true;
}

bool useTrapdoor(Player& player, ItemStack const& held, Click const& click) {
    auto* region = regionOf(click.pos.dim);
    if (!region) return false;
    auto        p = game::blockPos(click.pos);
    auto const& b = region->getBlock(p);
    if (kindOf(b) == Kind::GoldTrapdoor) {
        if (flag(b, kPowered)) return false;
        toggleTrapdoor(click.pos.dim, p);
        return true;
    }
    if (player.isSpectator()) return true;
    if (handleKeyAction(player, held, click.sneaking, click.pos, "trapdoor")) toggleTrapdoor(click.pos.dim, p);
    return true;
}

// ---- gates -----------------------------------------------------------------------------------------

int gateOpenFacing(float yaw, int facing) {
    auto wrap = [](float d) {
        d = std::fmod(d, 360.0f);
        if (d >= 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        return d;
    };
    int   best = facing;
    float min  = 360.0f;
    for (int d : {facing, (facing + 2) & 3}) {
        float diff = std::abs(wrap(static_cast<float>(d) * 90.0f - yaw));
        if (diff < min) {
            min  = diff;
            best = d;
        }
    }
    return best;
}

bool useGate(Player& player, ItemStack const&, Click const& click) {
    auto* region = regionOf(click.pos.dim);
    if (!region) return false;
    auto        p       = game::blockPos(click.pos);
    auto const& b       = region->getBlock(p);
    bool        gold    = kindOf(b) == Kind::GoldGate;
    bool        powered = flag(b, kPowered);
    if (!((!powered && gold) || !config().doorLikeGates)) return false;
    int   dim = click.pos.dim;
    float yaw = click.yaw;
    defer([dim, p, yaw] {
        auto* region = regionOf(dim);
        if (!region) return;
        auto const& cur = region->getBlock(p);
        if (!isGate(kindOf(cur))) return;
        bool wasOpen = flag(cur, kOpen);
        if (config().gateDoubleOpening) {
            for (auto np : {above(p), below(p)}) {
                auto const& nb = region->getBlock(np);
                if (isGate(kindOf(nb)) && sameGateIgnoringWall(nb, cur)) openGate(*region, np, nb, yaw);
            }
        }
        openGate(*region, p, cur, yaw);
        // Java plays the sound of the state before the click (open -> open sound)
        trapdoorSound(dim, p, wasOpen);
    });
    log().info("lock: {} used the gate at {}", player.getRealName(), click.pos.key());
    return true;
}

bool gateInWall(BlockSource& region, BlockPos const& pos, int facing) {
    auto const& up = region.getBlock(above(pos));
    if (up.canProvideFullSupport(0) || isGate(kindOf(up)) || isBars(up)) return true;
    return isBars(region.getBlock(side(pos, cw(facing)))) || isBars(region.getBlock(side(pos, ccw(facing))));
}

// ---- placement -------------------------------------------------------------------------------------

bool doorHingeRight(BlockSource& region, BlockPos const& lower, int facing, float clickX, float clickZ) {
    BlockPos up      = above(lower);
    int      left    = ccw(facing);
    int      right   = cw(facing);
    auto     full    = [&](BlockPos const& p) { return region.getBlock(p).canProvideFullSupport(1); };
    auto     door    = [&](BlockPos const& p) {
        auto const& b = region.getBlock(p);
        return isDoor(kindOf(b)) && b.getTypeName() == region.getBlock(lower).getTypeName() && !flag(b, kUpper);
    };
    int i = (full(side(lower, left)) ? -1 : 0) + (full(side(up, left)) ? -1 : 0) + (full(side(lower, right)) ? 1 : 0)
          + (full(side(up, right)) ? 1 : 0);
    bool leftDoor  = door(side(lower, left));
    bool rightDoor = door(side(lower, right));
    if ((!leftDoor || rightDoor) && i <= 0) {
        if ((!rightDoor || leftDoor) && i >= 0) {
            auto [dx, dz] = game::facingOffset(facing);
            bool leftHinge = (dx >= 0 || !(clickZ < 0.5f)) && (dx <= 0 || !(clickZ > 0.5f)) && (dz >= 0 || !(clickX > 0.5f))
                          && (dz <= 0 || !(clickX < 0.5f));
            return !leftHinge;
        }
        return false;
    }
    return true;
}

std::pair<int, bool> trapdoorPlacement(Click const& click, int playerFacing) {
    int f = faceToFacing(click.face);
    if (f >= 0) return {f, click.hitY > 0.5f};
    return {game::opposite(playerFacing), click.face != 1};
}

namespace {
int signalAt(int dim, BlockPos const& pos) {
    auto* region = regionOf(dim);
    return region ? redstoneSignal(*region, pos) : 0;
}
} // namespace

void onPlaced(Player& player, core::GlobalPos const& pos, Click const& click, ItemStack const& placedFrom) {
    auto* region = regionOf(pos.dim);
    if (!region) return;
    auto        bp     = game::blockPos(pos);
    auto const& b      = region->getBlock(bp);
    auto        k      = kindOf(b);
    int         facing = game::playerFacing(player);
    switch (k) {
    case Kind::GoldDoor:
    case Kind::NetheriteDoor: {
        BlockPos up = above(bp);
        if (flag(b, kUpper)) return;
        if (!game::replaceable(*region, up)) {
            game::clearBlock(*region, bp);
            if (!player.isCreative()) giveItem(player, makeItem(b.getTypeName()));
            return;
        }
        float cx    = click.hitX + static_cast<float>(click.pos.x - bp.x);
        float cz    = click.hitZ + static_cast<float>(click.pos.z - bp.z);
        bool  hinge = doorHingeRight(*region, bp, facing, cx, cz);
        bool  on    = k == Kind::GoldDoor && (signalAt(pos.dim, bp) > 0 || signalAt(pos.dim, up) > 0);
        auto const& lower = with(b, {{kFacing, facing}, {kHinge, hinge}, {kUpper, 0}, {kOpen, on}, {kPowered, on}});
        place(*region, bp, lower);
        place(*region, up, with(lower, {{kUpper, 1}}));
        if (k == Kind::GoldDoor) {
            track(pos);
            track(game::globalPos(pos.dim, up));
        }
        break;
    }
    case Kind::GoldTrapdoor:
    case Kind::NetheriteTrapdoor: {
        auto [f, top] = trapdoorPlacement(click, facing);
        bool on       = k == Kind::GoldTrapdoor && signalAt(pos.dim, bp) > 0;
        place(*region, bp, with(b, {{kFacing, f}, {kTop, top}, {kOpen, 0}, {kPowered, on}}));
        if (k == Kind::GoldTrapdoor) track(pos);
        break;
    }
    case Kind::IronGate:
    case Kind::GoldGate: {
        bool on = signalAt(pos.dim, bp) > 0;
        place(*region, bp, with(b, {{kFacing, facing}, {kOpen, on}, {kPowered, on}, {kInWall, gateInWall(*region, bp, facing)}}));
        track(pos);
        break;
    }
    case Kind::Safe:
        placeSafe(player, pos, placedFrom);
        break;
    default:
        break;
    }
}

// ---- removal ---------------------------------------------------------------------------------------

void onRemoved(core::GlobalPos const& pos, Block const& previous, bool byCreative) {
    auto k = kindOf(previous);
    if (readsRedstone(k)) untrack(pos);
    if (k == Kind::LockBlock || k == Kind::NetheriteTrapdoor) {
        eraseLockData(pos);
        gTimers.erase(pos);
        return;
    }
    if (isDoor(k)) {
        bool        wasUpper = flag(previous, kUpper);
        std::string id       = previous.getTypeName();
        defer([pos, wasUpper, id, byCreative] {
            auto* region = regionOf(pos.dim);
            if (!region) return;
            auto        bp      = game::blockPos(pos);
            BlockPos    partner = wasUpper ? below(bp) : above(bp);
            auto const& pb      = region->getBlock(partner);
            if (pb.getTypeName() == id && flag(pb, kUpper) != wasUpper) {
                game::clearBlock(*region, partner);
                if (wasUpper && !byCreative) {
                    dropItem(*region, Vec3{partner.x + 0.5f, partner.y + 0.5f, partner.z + 0.5f}, makeItem(id));
                }
            }
            if (!wasUpper) eraseLockData(pos);
        });
        return;
    }
    if (k == Kind::Safe) {
        removeSafe(pos, byCreative);
    }
}

bool mayBreak(Player& player, core::GlobalPos const& pos) {
    auto* region = regionOf(pos.dim);
    if (!region) return true;
    auto        bp = game::blockPos(pos);
    auto const& b  = region->getBlock(bp);
    auto        k  = kindOf(b);
    if ((k == Kind::NetheriteDoor || k == Kind::NetheriteTrapdoor) && config().netheriteUnbreakable) {
        auto lockPos = (k == Kind::NetheriteDoor && flag(b, kUpper)) ? game::globalPos(pos.dim, below(bp)) : pos;
        return playerKeyStatus(player, password(lockPos)) == KeyStatus::Correct;
    }
    if (k == Kind::Safe && config().safePreventBreaking) {
        return safeCanOpen(player, pos, true);
    }
    return true;
}

// ---- redstone ----------------------------------------------------------------------------------------

int redstoneSignal(BlockSource& region, BlockPos const& pos) {
    auto* system = region.getDimension().mCircuitSystem.get();
    if (!system) return 0;
    auto& all = system->mSceneGraph->mAllComponents;
    auto  it  = all.find(pos);
    if (it == all.end() || !it->second) return 0;
    return std::max(0, it->second->getStrength());
}

void track(core::GlobalPos const& pos) {
    if (gTracked.insert(pos).second) gTrackedDirty = true;
}
void untrack(core::GlobalPos const& pos) {
    if (gTracked.erase(pos)) gTrackedDirty = true;
}
bool tracked(core::GlobalPos const& pos) { return gTracked.contains(pos); }

void tickRedstone() {
    if (gTracked.empty()) return;
    std::vector<core::GlobalPos> gone;
    std::vector<core::GlobalPos> list(gTracked.begin(), gTracked.end());
    for (auto const& pos : list) {
        auto* region = regionOf(pos.dim);
        if (!region) continue;
        auto bp = game::blockPos(pos);
        if (!region->hasChunksAt(bp, 0, false)) continue;
        auto const& b = region->getBlock(bp);
        auto        k = kindOf(b);
        if (!readsRedstone(k)) {
            gone.push_back(pos);
            continue;
        }
        if (!enabled(k)) continue;
        switch (k) {
        case Kind::GoldDoor: {
            if (flag(b, kUpper)) continue; // handled with the lower half
            BlockPos    up  = above(bp);
            auto const& ub  = region->getBlock(up);
            bool        has = redstoneSignal(*region, bp) > 0 || (kindOf(ub) == k && redstoneSignal(*region, up) > 0);
            if (has != flag(b, kPowered)) place(*region, bp, with(b, {{kPowered, has}}));
            if (kindOf(ub) == k && flag(ub, kUpper) && has != flag(ub, kPowered)) place(*region, up, with(ub, {{kPowered, has}}));
            break;
        }
        case Kind::GoldTrapdoor: {
            bool has = redstoneSignal(*region, bp) > 0;
            if (has != flag(b, kPowered)) place(*region, bp, with(b, {{kPowered, has}}));
            break;
        }
        case Kind::IronGate:
        case Kind::GoldGate: {
            bool         has  = redstoneSignal(*region, bp) > 0;
            Block const* next = &b;
            if (has != flag(b, kPowered)) {
                next = &with(*next, {{kPowered, has}});
                if (k == Kind::IronGate || !config().doorLikeGates) {
                    if (flag(*next, kOpen) != has) {
                        next = &with(*next, {{kOpen, has}});
                        trapdoorSound(pos.dim, bp, has);
                    }
                }
            }
            bool wall = gateInWall(*region, bp, state(*next, kFacing));
            if (wall != flag(*next, kInWall)) next = &with(*next, {{kInWall, wall}});
            if (next != &b) place(*region, bp, *next);
            break;
        }
        default:
            break;
        }
    }
    for (auto const& p : gone) untrack(p);
}

void loadTracked() {
    gTracked.clear();
    gTrackedDirty = false;
    auto v        = game::db::load(kTrackedKey);
    if (!v || !v->is_array()) return;
    for (auto const& e : *v) {
        if (!e.is_array() || e.size() != 4) continue;
        gTracked.insert({e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    }
}

void saveTracked() {
    if (!gTrackedDirty) return;
    auto arr = nlohmann::json::array();
    for (auto const& p : gTracked) arr.push_back({p.dim, p.x, p.y, p.z});
    if (arr.empty()) game::db::erase(kTrackedKey);
    else game::db::save(kTrackedKey, arr);
    gTrackedDirty = false;
}

void clearTracked() {
    gTracked.clear();
    gTrackedDirty = false;
}

} // namespace cm::feature::lock
