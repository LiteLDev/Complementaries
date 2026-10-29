#include "game/Blocks.h"

#include <algorithm>
#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/states/BlockState.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

namespace cm::game {

namespace {
constexpr int kUpdateAll = 3; ///< neighbours + clients

// Block::getState / Block::setState work on the index of a value in the state's value list, not
// on the value itself. For vanilla integer states the two are the same, but a custom state such as
// [1, 2, 3, 4] or [true, false] maps index 0 to 1 / true. The serialized states tag holds the real
// values, so read and match those instead.
std::optional<int> serializedState(Block const& block, std::string_view name) {
    auto const& tags   = block.mSerializationId->mTags;
    auto        states = tags.find("states");
    if (states == tags.end() || !states->second.hold<CompoundTag>()) return std::nullopt;
    auto const& values = states->second.get<CompoundTag>().mTags;
    auto        it     = values.find(name);
    if (it == values.end()) return std::nullopt;
    if (it->second.hold<ByteTag>()) return it->second.get<ByteTag>().data != 0 ? 1 : 0;
    if (it->second.hold<IntTag>()) return static_cast<int>(it->second.get<IntTag>().data);
    return std::nullopt; // string state: only its index is meaningful as an int
}
} // namespace

std::string blockId(Block const& block) { return block.getTypeName(); }

std::string blockIdAt(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) return {};
    return region.getBlock(pos).getTypeName();
}

std::optional<int> intState(Block const& block, std::string_view state) {
    auto const* s = block.getBlockType().getBlockState(HashedString(std::string(state)));
    if (!s) return std::nullopt;
    if (auto value = serializedState(block, state)) return value;
    return block.getState<int>(*s);
}

Block const* withStates(Block const& block, std::vector<std::pair<std::string_view, int>> const& states) {
    Block const* current = &block;
    for (auto const& [name, value] : states) {
        auto const* state = current->getBlockType().getBlockState(HashedString(std::string(name)));
        if (!state) continue;
        // Find the index whose serialized value is `value`; string states take `value` as the index.
        Block const* next  = nullptr;
        int          count = static_cast<int>(std::min<uint64>(state->mVariationCount, 64));
        for (int idx = 0; idx < count && !next; ++idx) {
            auto b = current->setState<int>(*state, idx);
            if (!b) continue;
            auto v = serializedState(*b, name);
            if (v ? *v == value : idx == value) next = b.as_ptr();
        }
        if (next) current = next;
    }
    return current;
}

Block const* blockWith(std::string_view id, std::vector<std::pair<std::string_view, int>> const& states) {
    auto type = BlockType::tryGetFromRegistry(HashedString(std::string(id)));
    if (!type) return nullptr;
    Block const* block = type->mDefaultState;
    if (!block) return nullptr;
    return withStates(*block, states);
}

bool setBlock(BlockSource& region, BlockPos const& pos, Block const& block) {
    BlockChangeContext ctx{};
    return region.setBlock(pos, block, kUpdateAll, nullptr, ctx);
}

bool clearBlock(BlockSource& region, BlockPos const& pos) {
    auto const* air = blockWith("minecraft:air");
    return air && setBlock(region, pos, *air);
}

bool replaceable(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    if (!region.hasChunksAt(pos, 0, false)) return false;
    auto const& b = region.getBlock(pos);
    return b.isAir() || b.getBlockType().canBeBuiltOver(b, region, pos);
}

int playerFacing(Actor const& actor) {
    // Bedrock yaw: 0 = south (+z), 90 = west (-x), 180/-180 = north, -90 = east; same as Java.
    float yaw = actor.getRotation().y;
    int   idx = static_cast<int>(std::floor(yaw / 90.0f + 0.5f)) & 3;
    return idx; // Java 2D data value: 0 S, 1 W, 2 N, 3 E
}

std::pair<int, int> facingOffset(int facing) {
    switch (facing & 3) {
    case 0:
        return {0, 1}; // south
    case 1:
        return {-1, 0}; // west
    case 2:
        return {0, -1}; // north
    default:
        return {1, 0}; // east
    }
}

core::GlobalPos globalPos(BlockSource const& region, BlockPos const& pos) {
    return {static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z};
}

core::GlobalPos globalPos(int dim, BlockPos const& pos) { return {dim, pos.x, pos.y, pos.z}; }

BlockPos blockPos(core::GlobalPos const& pos) { return BlockPos{pos.x, pos.y, pos.z}; }

BlockPos blockPosOf(Vec3 const& pos) {
    // the same small nudge as the game's own conversion, so 63.9999 from float maths is still 64
    return BlockPos{
        static_cast<int>(std::floor(pos.x + 0.001f)),
        static_cast<int>(std::floor(pos.y + 0.001f)),
        static_cast<int>(std::floor(pos.z + 0.001f)),
    };
}

} // namespace cm::game
