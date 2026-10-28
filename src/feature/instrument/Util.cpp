// Block state, sound, message and redstone helpers of the instrument unit.

#include <array>
#include <cmath>
#include <map>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"

#include "feature/instrument/Instrument.h"

namespace cm::feature::instrument {

namespace {
/// Hashed state names are built once (the tick loop asks for the same few names all the time).
HashedString const& hashed(std::string_view name) {
    static std::map<std::string, HashedString, std::less<>> cache;
    auto it = cache.find(name);
    if (it == cache.end()) it = cache.emplace(std::string(name), HashedString(std::string(name))).first;
    return it->second;
}

BlockState const* stateOf(Block const& block, std::string_view name) { return block.getBlockType().getBlockState(hashed(name)); }

} // namespace

int state(Block const& block, std::string_view name, int fallback) {
    auto const* s = stateOf(block, name);
    if (!s) return fallback;
    return block.getState<int>(*s).value_or(fallback);
}

Block const* withStates(Block const& block, std::vector<std::pair<std::string_view, int>> const& states) {
    Block const* out = &block;
    for (auto const& [name, value] : states) {
        auto const* s = stateOf(*out, name);
        if (!s) continue;
        if (auto b = out->setState<int>(*s, value)) out = b.as_ptr();
    }
    return out;
}

std::string stringState(Block const& block, std::string_view name) {
    CompoundTag const& tag = block.mSerializationId;
    if (!tag.contains("states")) return {};
    auto const& states = tag.at("states");
    if (!states.is_object() || !states.contains(name)) return {};
    auto const& v = states.get<CompoundTag>().at(name);
    if (!v.is_string()) return {};
    return static_cast<std::string const&>(v.get<StringTag>());
}

Block const* withStringState(Block const& block, std::string_view name, std::string const& value) {
    CompoundTag tag = block.mSerializationId;
    if (!tag.contains("states")) return nullptr;
    auto& states = tag.at("states");
    if (!states.is_object() || !states.contains(name)) return nullptr;
    states.get<CompoundTag>()[name] = StringTag(value);
    auto b = Block::tryGetFromRegistry(tag);
    return b ? b.as_ptr() : nullptr;
}
bool apply(BlockSource& region, BlockPos const& pos, Block const& current, Block const* next) {
    if (!next || next == &current) return false;
    BlockChangeContext ctx{};
    return region.setBlock(pos, *next, 3, nullptr, ctx);
}

void updateComparators(BlockSource& region, BlockPos const& pos) { region.updateNeighborsAt(pos); }

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

bool isNaturalDimension(BlockSource& region) { return region.getDimension().isNaturalDimension(); }

bool playerNear(BlockSource& region, BlockPos const& pos, float range) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    int  dim   = static_cast<int>(region.getDimensionId());
    bool found = false;
    Vec3 c{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim) return true;
        auto const& pp = p.getPosition();
        float dx = pp.x - c.x, dy = pp.y - c.y, dz = pp.z - c.z;
        if (dx * dx + dy * dy + dz * dz <= range * range) {
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

void playSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& p   = static_cast<PlaySoundPacket&>(*packet);
    p.mName   = name;
    p.mPos    = BlockPos{pos.x * 8 + 4, pos.y * 8 + 4, pos.z * 8 + 4};
    p.mVolume = volume;
    p.mPitch  = pitch;
    p.sendTo(pos, region.getDimensionId());
}

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& p      = static_cast<SetTitlePacket&>(*packet);
    p.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    p.mTitleText = text;
    p.sendTo(player);
}

int redstoneInput(BlockSource& region, BlockPos const& pos) {
    auto& circuit = region.getDimension().mCircuitSystem;
    if (!circuit) return 0;
    auto& all = circuit->mSceneGraph->mAllComponents;
    auto  it  = all.find(pos);
    if (it == all.end() || !it->second) return 0;
    return std::max(0, it->second->getStrength());
}

void dropItem(BlockSource& region, BlockPos const& pos, std::string const& id, int count, int aux, std::string const& name) {
    if (id.empty() || count <= 0) return;
    ItemStack stack;
    stack.reinit(id, count, aux);
    if (stack.isNull()) return;
    if (!name.empty()) stack.setCustomName(Bedrock::Safety::RedactableString(name));
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, at, 10);
}

double random01() {
    static std::mt19937_64                 rng{std::random_device{}()};
    static std::uniform_real_distribution<> dist(0.0, 1.0);
    return dist(rng);
}

} // namespace cm::feature::instrument
