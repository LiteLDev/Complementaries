#include "feature/lighting/LightBlocks.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/states/BlockState.h"

#include <algorithm>

namespace cm::feature::lighting {

namespace {
constexpr std::string_view kNs = "complementaries:";

CompoundTag const* stateTag(Block const& block) {
    auto const& tags = block.mSerializationId->mTags;
    auto        it   = tags.find("states");
    if (it == tags.end() || !it->second.hold<CompoundTag>()) return nullptr;
    return &it->second.get<CompoundTag>();
}

std::optional<StateValue> toValue(CompoundTagVariant const& v) {
    if (v.hold<ByteTag>()) return StateValue{v.get<ByteTag>().data != 0};
    if (v.hold<IntTag>()) return StateValue{static_cast<int>(v.get<IntTag>().data)};
    if (v.hold<StringTag>()) return StateValue{std::string(static_cast<std::string const&>(v.get<StringTag>()))};
    return std::nullopt;
}

std::optional<StateValue> findState(Block const& block, std::string_view name) {
    auto const* tag = stateTag(block);
    if (!tag) return std::nullopt;
    auto it = tag->mTags.find(name);
    if (it == tag->mTags.end()) return std::nullopt;
    return toValue(it->second);
}
} // namespace

std::vector<std::string> allBlockIds() {
    std::vector<std::string> out;
    auto add = [&out](std::string_view path) { out.push_back(std::string(kNs) + std::string(path)); };
    for (auto s : kSconces) add(s);
    add("sconce_lever");
    add("candle_holder");
    for (auto c : kColors) add(std::string("candle_holder_") + std::string(c));
    for (auto l : kLamps) add(l);
    add("wall_lantern");
    return out;
}

BlockInfo classify(std::string_view id) {
    if (!id.starts_with(kNs)) return {};
    auto path = id.substr(kNs.size());
    if (path == "sconce") return {Kind::Sconce, Flame::Torch};
    if (path == "sconce_soul") return {Kind::Sconce, Flame::Soul};
    if (path == "sconce_ender") return {Kind::Sconce, Flame::Ender};
    if (path == "sconce_glow") return {Kind::Sconce, Flame::Glow};
    if (path == "sconce_green") return {Kind::Sconce, Flame::Green};
    if (path == "sconce_lever") return {Kind::SconceLever, Flame::Torch};
    if (path == "wall_lantern") return {Kind::WallLantern, Flame::Torch};
    if (path == "candle_holder" || path.starts_with("candle_holder_")) return {Kind::CandleHolder, Flame::Torch};
    for (auto l : kLamps) {
        if (path == l) return {Kind::Lamp, Flame::Torch};
    }
    return {};
}

BlockInfo classify(Block const& block) { return classify(block.getTypeName()); }

StateList states(Block const& block) {
    StateList out;
    auto const* tag = stateTag(block);
    if (!tag) return out;
    for (auto const& [name, value] : tag->mTags) {
        if (auto v = toValue(value)) out.emplace_back(name, std::move(*v));
    }
    return out;
}

bool boolState(Block const& block, std::string_view name, bool def) {
    auto v = findState(block, name);
    if (!v) return def;
    if (auto const* b = std::get_if<bool>(&*v)) return *b;
    if (auto const* i = std::get_if<int>(&*v)) return *i != 0;
    return def;
}

int intState(Block const& block, std::string_view name, int def) {
    auto v = findState(block, name);
    if (!v) return def;
    if (auto const* i = std::get_if<int>(&*v)) return *i;
    if (auto const* b = std::get_if<bool>(&*v)) return *b ? 1 : 0;
    return def;
}

std::string stringState(Block const& block, std::string_view name) {
    auto v = findState(block, name);
    if (!v) return {};
    if (auto const* s = std::get_if<std::string>(&*v)) return *s;
    return {};
}

Block const* withStates(Block const& block, StateList const& overrides) {
    // Block::tryGetFromRegistry(name, states) ignores the states, and Block::setState takes the
    // index of a value in the state's value list (custom lists such as [true, false] or [1, 2, 3, 4]
    // don't map values to themselves). So look for the index whose serialized value matches.
    Block const* current = &block;
    auto const&  type    = block.getBlockType();
    for (auto const& [name, value] : overrides) {
        auto const* state = type.getBlockState(HashedString(name));
        if (!state) return nullptr;
        int          count = static_cast<int>(std::min<uint64>(state->mVariationCount, 64));
        Block const* next  = nullptr;
        for (int idx = 0; idx < count && !next; ++idx) {
            auto r = current->setState<int>(*state, idx);
            if (r && findState(*r, name) == value) next = r.as_ptr();
        }
        if (!next) return nullptr;
        current = next;
    }
    return current;
}

Block const* makeBlock(std::string_view id, StateList const& overrides) {
    auto base = Block::tryGetFromRegistry(HashedString(std::string(id)));
    if (!base) return nullptr;
    if (overrides.empty()) return base.as_ptr();
    return withStates(*base, overrides);
}

int lanternIndex(std::string_view itemId) {
    for (int i = 0; i < static_cast<int>(std::size(kLanterns)); ++i) {
        if (kLanterns[i] == itemId) return i;
    }
    return -1;
}

std::string_view faceName(int facing) {
    switch (facing) {
    case 2:
        return "north";
    case 3:
        return "south";
    case 4:
        return "west";
    case 5:
        return "east";
    default:
        return {};
    }
}

std::pair<int, int> towardsSupport(std::string_view face) {
    if (face == "north") return {0, 1};
    if (face == "south") return {0, -1};
    if (face == "west") return {1, 0};
    if (face == "east") return {-1, 0};
    return {0, 0};
}

bool waterlogged(BlockSource& region, BlockPos const& pos) {
    auto const& extra = region.getExtraBlock(pos);
    auto const& name  = extra.getTypeName();
    return name == "minecraft:water" || name == "minecraft:flowing_water";
}

} // namespace cm::feature::lighting
