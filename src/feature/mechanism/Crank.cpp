// Crank: a hand-turned redstone source (power 0..15, wraps around), hung on a block like a lever.

#include <map>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CachedComponentData.h"

#include "feature/mechanism/Crankable.h"
#include "feature/mechanism/Mechanism.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::mechanism {

namespace {
std::map<std::string, CrankableFn, std::less<>>& crankables() {
    static std::map<std::string, CrankableFn, std::less<>> m;
    return m;
}
} // namespace

void registerCrankable(std::string const& id, CrankableFn fn) { crankables()[id] = std::move(fn); }

void unregisterCrankable(std::string const& id) { crankables().erase(id); }

namespace crank {

int turn(BlockSource& region, BlockPos const& pos, bool ccw, Player* player) {
    if (!config().crank.enabled) return -1;
    Block const& b = region.getBlock(pos);
    if (kindOf(b) != Kind::Crank) return -1;
    int power = (intStateOf(b, "complementaries:power", 0) + (ccw ? 15 : 1)) % 16;
    if (auto const* nb = withStates(b, {{"complementaries:power", rotation::Value{power}}})) place(region, pos, *nb);
    Vec3 c = centerOf(pos);
    playSound(region, c, "complementaries.block.crank", 0.6f, 0.6f + 0.04f * static_cast<float>(power));
    spawnParticle(region, c, "minecraft:redstone_wire_dust_particle");

    int      f      = facingOf(region.getBlock(pos));
    BlockPos behind = offset(pos, rotation::opposite(f < 0 ? rotation::kUp : f));
    if (region.hasChunksAt(behind, 0, false)) {
        auto it = crankables().find(region.getBlock(behind).getTypeName());
        if (it != crankables().end() && it->second) it->second(region, behind, ccw, player);
    }
    if (player) actionBar(*player, trFor(*player, "cm.mechanism.crank.power", "Power: {}", power));
    log().info("mechanism: crank at {} {} {} power {}", pos.x, pos.y, pos.z, power);
    return power;
}

void check(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    if (kindOf(b) != Kind::Crank) return;
    int f = facingOf(b);
    if (f < 0) return;
    BlockPos support = offset(pos, rotation::opposite(f));
    if (!region.hasChunksAt(support, 0, false)) return;
    Block const& s = region.getBlock(support);
    bool         ok = !s.isAir()
             && (s.mCachedComponentData->mIsSolid
                 || s.getBlockType().canProvideSupport(s, static_cast<uchar>(f), BlockSupportType::Center));
    if (ok) return;
    dropItem(region, pos, ids::kCrank);
    placeAir(region, pos);
    log().info("mechanism: crank at {} {} {} lost its support", pos.x, pos.y, pos.z);
}

} // namespace crank
} // namespace cm::feature::mechanism
