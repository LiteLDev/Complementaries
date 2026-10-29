// Ringing bells through the chain or rope they hang from (docs/spec/tweak.md §3.1).

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/Direction.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BellBlockActor.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "mod/Log.h"

namespace cm::feature::tweak {

namespace {
bool isLink(Block const& b) { return blockName(b) == kRopeBlock || isChain(blockName(b)); }
bool isBell(Block const& b) { return blockName(b) == "minecraft:bell"; }

bool ring(BlockSource& region, BlockPos const& pos, Actor* source) {
    auto* actor = region.getBlockEntity(pos);
    if (!actor) return false;
    auto* bell = static_cast<BellBlockActor*>(actor);
    bell->ring(Direction::Type::South, region, source, true);
    playSound(region, Sound::Bell, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f});
    log().info("tweak: rang the bell at {} {} {} through a chain", pos.x, pos.y, pos.z);
    return true;
}
} // namespace

bool isChain(std::string const& id) {
    if (id == "minecraft:chain") return true;
    return id.starts_with("minecraft:") && id.ends_with("_chain") && id.find("command") == std::string::npos;
}

bool ringThroughChain(BlockSource& region, BlockPos const& pos, Actor* source) {
    int const len = config().bellChainLength;
    for (int dir : {1, -1}) {
        for (int i = 1; i <= len; ++i) {
            BlockPos     p{pos.x, pos.y + dir * i, pos.z};
            Block const& b = region.getBlock(p);
            if (isBell(b)) return ring(region, p, source);
            if (!isLink(b)) break;
        }
    }
    return false;
}

bool bellChainUseOn(UseOn const& use) {
    if (!config().bellChainRinging || !use.itemId.empty() || !isLink(use.block)) return false;
    auto& region = use.player.getDimensionBlockSource();
    // only when a bell is really at the end of the column, so other interactions stay untouched
    int const len   = config().bellChainLength;
    bool      found = false;
    for (int dir : {1, -1}) {
        for (int i = 1; i <= len && !found; ++i) {
            Block const& b = region.getBlock(BlockPos{use.pos.x, use.pos.y + dir * i, use.pos.z});
            if (isBell(b)) found = true;
            else if (!isLink(b)) break;
        }
    }
    if (!found) return false;
    auto playerId = use.player.getOrCreateUniqueID();
    int  dim      = static_cast<int>(region.getDimensionId());
    auto pos      = use.pos;
    defer([=] {
        auto* r = regionOf(dim);
        if (!r) return;
        ringThroughChain(*r, pos, findPlayer(playerId));
    });
    return true;
}

} // namespace cm::feature::tweak
