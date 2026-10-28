// Placeable sticks, blaze rods and breeze rods (docs/spec/tweak.md §1.1).

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "game/Blocks.h"

namespace cm::feature::tweak {

namespace {
struct StickKind {
    char const* item;
    char const* block;
    bool TweakConfig::*enabled;
};

constexpr StickKind kKinds[] = {
    {"minecraft:stick", kStickBlock, &TweakConfig::placeableSticks},
    {"minecraft:blaze_rod", kBlazeRodBlock, &TweakConfig::placeableBlazeRods},
    {"minecraft:breeze_rod", kBreezeRodBlock, &TweakConfig::placeableBreezeRods},
};

constexpr char const* kAxis[3] = {"complementaries:axis_x", "complementaries:axis_y", "complementaries:axis_z"};

/// The stick block with `axis` added at `target` (nullptr when not possible).
Block const* withStick(BlockSource& region, BlockPos const& target, std::string const& blockId, int axis) {
    Block const& there = region.getBlock(target);
    if (blockName(there) == blockId) {
        if (game::intState(there, kAxis[axis]).value_or(1) != 0) return nullptr;
        return withState(there, kAxis[axis], 1);
    }
    if (!game::replaceable(region, target)) return nullptr;
    return game::blockWith(blockId, {{kAxis[axis], 1}});
}
} // namespace

std::string stickBlockFor(std::string const& itemId) {
    for (auto const& k : kKinds) {
        if (itemId == k.item) return config().*k.enabled ? k.block : "";
    }
    return {};
}

std::string stickItemFor(std::string const& blockId) {
    for (auto const& k : kKinds) {
        if (blockId == k.block) return k.item;
    }
    return {};
}

bool sticksUseOn(UseOn const& use) {
    auto blockId = stickBlockFor(use.itemId);
    if (blockId.empty()) return false;
    if (isInteractive(use.block) && !use.player.isSneaking()) return false;
    auto& region = use.player.getDimensionBlockSource();
    int   axis   = faceAxis(use.face);

    // first the clicked stick block itself (adds a stick across it), then the block in front of the face
    BlockPos target = use.pos;
    if (blockName(use.block) != blockId || game::intState(use.block, kAxis[axis]).value_or(1) != 0) {
        target = offset(use.pos, use.face);
    }
    if (!withStick(region, target, blockId, axis)) return false;

    auto playerId = use.player.getOrCreateUniqueID();
    int  dim      = static_cast<int>(region.getDimensionId());
    defer([=] {
        auto* player = findPlayer(playerId);
        auto* r      = regionOf(dim);
        if (!player || !r) return;
        if (itemId(player->getSelectedItem()) != stickItemFor(blockId)) return;
        auto const* block = withStick(*r, target, blockId, axis);
        if (!block || !game::setBlock(*r, target, *block)) return;
        playBlockSound(*r, Sound::Place, target, *block);
        consumeSelected(*player);
    });
    return true;
}

} // namespace cm::feature::tweak
