#include "feature/rope/Common.h"

#include <algorithm>
#include <map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"
#include "mod/Config.h"

CM_REGISTER_CONFIG("rope", cm::feature::rope::RopeConfig);

namespace cm::feature::rope {

RopeConfig const& config() { return cm::config::get<RopeConfig>(); }

// ---- types ---------------------------------------------------------------------------------------
namespace {
Types       gTypes;
std::string gChainId = "minecraft:iron_chain";

BlockType* lookup(std::string_view id) {
    auto type = BlockType::tryGetFromRegistry(HashedString(std::string(id)));
    return const_cast<BlockType*>(type.as_ptr());
}
} // namespace

Types const& types() { return gTypes; }

void resolveTypes() {
    auto* rope       = lookup(kRope);
    gTypes.rope      = rope;
    gTypes.pulley    = lookup(kPulley);
    gTypes.flaxCrop  = lookup(kFlaxCrop);
    gTypes.flaxBlock = lookup(kFlaxBlock);
    gTypes.wildFlax  = lookup(kWildFlax);
    markRopeClimbable();
    gChainId = lookup("minecraft:iron_chain") ? "minecraft:iron_chain" : "minecraft:chain";
}

void clearTypes() { gTypes = {}; }

bool markRopeClimbable() {
    auto* rope = lookup(kRope);
    if (!rope) return false;
    // Ladders and scaffolding climb through this flag; the movement code reads it both on the
    // server (authoritative movement) and on the client (prediction, client mod).
    rope->mProperties = static_cast<BlockProperty>(
        static_cast<std::uint64_t>(rope->mProperties) | static_cast<std::uint64_t>(BlockProperty::Climbable)
    );
    return true;
}

bool isRope(Block const& b) { return gTypes.rope && &b.getBlockType() == gTypes.rope; }
bool isPulley(Block const& b) { return gTypes.pulley && &b.getBlockType() == gTypes.pulley; }
bool isFlaxCrop(Block const& b) { return gTypes.flaxCrop && &b.getBlockType() == gTypes.flaxCrop; }
bool isFlaxBlock(Block const& b) { return gTypes.flaxBlock && &b.getBlockType() == gTypes.flaxBlock; }

bool isChain(Block const& b) {
    auto const& id = b.getTypeName();
    return id.starts_with("minecraft:") && id.ends_with("chain") && id != "minecraft:chain_command_block";
}

std::string const& chainId() { return gChainId; }

int stateOf(Block const& b, std::string_view name) { return game::intState(b, name).value_or(0); }

Block const& withStates(Block const& b, std::vector<std::pair<std::string_view, int>> const& states) {
    Block const* out = &b;
    auto const&  type = b.getBlockType();
    for (auto const& [name, value] : states) {
        if (auto const* state = type.getBlockState(HashedString(std::string(name)))) {
            if (auto r = out->setState(*state, value)) out = r.as_ptr();
        }
    }
    return *out;
}

// ---- positions -----------------------------------------------------------------------------------
BlockPos neighbor(BlockPos const& p, int face) {
    switch (face) {
    case 0:
        return {p.x, p.y - 1, p.z};
    case 1:
        return {p.x, p.y + 1, p.z};
    case 2:
        return {p.x, p.y, p.z - 1};
    case 3:
        return {p.x, p.y, p.z + 1};
    case 4:
        return {p.x - 1, p.y, p.z};
    default:
        return {p.x + 1, p.y, p.z};
    }
}

int oppositeFace(int face) { return face ^ 1; }

BlockSource* serverRegion(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

bool isServerRegion(BlockSource const& region) {
    return !const_cast<BlockSource&>(region).getLevel().isClientSide();
}

bool loaded(BlockSource& region, BlockPos const& pos) {
    return pos.y >= region.getMinHeight() && pos.y < region.getMaxHeight() && region.hasChunksAt(pos, 0, false);
}

core::GlobalPos gpos(BlockSource const& region, BlockPos const& pos) { return game::globalPos(region, pos); }

// ---- world edits ---------------------------------------------------------------------------------
bool set(BlockSource& region, BlockPos const& pos, Block const& block) { return game::setBlock(region, pos, block); }

bool setAir(BlockSource& region, BlockPos const& pos) { return game::clearBlock(region, pos); }

bool destroy(BlockSource& region, BlockPos const& pos, bool drops) {
    BlockChangeContext ctx{};
    return region.getLevel().destroyBlock(region, pos, drops, ctx);
}

bool replaceable(BlockSource& region, BlockPos const& pos) { return game::replaceable(region, pos); }

bool movable(BlockSource& region, BlockPos const& pos) {
    if (!loaded(region, pos)) return false;
    auto const& b = region.getBlock(pos);
    if (b.isAir() || b.hasProperty(BlockProperty::Liquid)) return false;
    if (b.getBlockType().getDestroySpeed() < 0.0f) return false;
    if (region.getBlockEntity(pos) != nullptr) return false;
    return true;
}

void blockSound(BlockSource& region, BlockPos const& pos, Block const& block, bool place, Actor const& by) {
    Vec3 center{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    region.getLevel().broadcastSoundEvent(
        region,
        place ? SharedTypes::Legacy::LevelSoundEvent::Place : SharedTypes::Legacy::LevelSoundEvent::Break,
        center,
        static_cast<int>(block.mNetworkId),
        ActorSoundIdentifier(by),
        false,
        std::nullopt
    );
}

// ---- items ---------------------------------------------------------------------------------------
ItemStack makeStack(std::string_view id, int count, int aux) {
    ItemStack s;
    s.reinit(id, count, aux);
    return s;
}

std::string itemId(ItemStackBase const& item) { return item.isNull() ? std::string{} : item.getTypeName(); }

void giveOrDrop(Player& player, ItemStack const& item) {
    ItemStack copy = item;
    if (!player.add(copy)) player.drop(copy, false);
    player.refreshInventory();
}

void dropAt(BlockSource& region, Vec3 const& pos, ItemStack const& item) {
    region.getLevel().getSpawner().spawnItem(region, item, nullptr, pos, 0);
}

bool survival(Player const& player) { return !player.isCreative(); }

Player* playerById(std::int64_t uid) {
    auto level = ll::service::getLevel();
    return level ? level->getPlayer(ActorUniqueID{uid}) : nullptr;
}

void consumeSelected(Player& player, int n) {
    if (!survival(player)) return;
    ItemStack s = player.getSelectedItem();
    if (s.isNull()) return;
    int left = static_cast<int>(s.mCount) - n;
    if (left <= 0) s = ItemStack::EMPTY_ITEM();
    else s.set(left);
    player.setSelectedItem(s);
    player.refreshInventory();
}

// ---- deferred work -------------------------------------------------------------------------------
namespace {
std::uint64_t                                      gNow = 0;
std::multimap<std::uint64_t, std::function<void()>> gLater;
} // namespace

void later(int ticks, std::function<void()> fn) { gLater.emplace(gNow + static_cast<std::uint64_t>(std::max(ticks, 1)), std::move(fn)); }

std::uint64_t now() { return gNow; }

namespace detail {
void runLater() {
    ++gNow;
    while (!gLater.empty() && gLater.begin()->first <= gNow) {
        auto fn = std::move(gLater.begin()->second);
        gLater.erase(gLater.begin());
        fn();
    }
}
void clearLater() { gLater.clear(); }
} // namespace detail

} // namespace cm::feature::rope
