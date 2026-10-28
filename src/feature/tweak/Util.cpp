#include "feature/tweak/Util.h"

#include <utility>
#include <vector>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutputType.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/LevelSoundEventPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"

namespace cm::feature::tweak {

namespace {
std::vector<std::function<void()>>& deferred() {
    static std::vector<std::function<void()>> q;
    return q;
}
} // namespace

ItemStack makeItem(std::string_view id, int count, int aux) {
    ItemStack stack;
    stack.reinit(id, count, aux);
    return stack;
}

std::string itemId(ItemStack const& item) {
    if (item.isNull()) return {};
    return item.getTypeName();
}

std::string const& blockName(Block const& block) { return block.getTypeName(); }

void setSelected(Player& player, ItemStack const& item) {
    player.setSelectedItem(item);
    player.sendInventory(false);
}

void consumeSelected(Player& player, int n) {
    if (player.isCreative()) return;
    ItemStack item = player.getSelectedItem();
    if (item.isNull()) return;
    if (item.mCount <= n) {
        item = ItemStack();
    } else {
        item.remove(n);
    }
    setSelected(player, item);
}

void giveOrDrop(Player& player, ItemStack item) {
    if (item.isNull()) return;
    ItemStack copy = item;
    if (!player.addAndRefresh(copy) && !copy.isNull() && copy.mCount > 0) {
        player.getLevel().getSpawner().spawnItem(
            player.getDimensionBlockSource(),
            copy,
            nullptr,
            player.getPosition(),
            0
        );
    }
}

void dropItem(BlockSource& region, BlockPos const& pos, ItemStack const& item) {
    if (item.isNull()) return;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.3f, static_cast<float>(pos.z) + 0.5f};
    region.getLevel().getSpawner().spawnItem(region, item, nullptr, at, 10);
}

void playSound(BlockSource& region, Sound sound, Vec3 const& pos, int data) {
    // a LevelSoundEvent packet to the players around (the Level helpers need an ActorSoundIdentifier,
    // whose default constructor the game does not export)
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::LevelSoundEvent);
    if (!packet) return;
    auto& p             = static_cast<LevelSoundEventPacket&>(*packet);
    p.mSoundEvent->mSound = sound;
    p.mPos              = pos;
    p.mData             = data;
    p.mActorIdentifier  = std::string{};
    p.mActor            = ActorUniqueID{-1};
    p.mIsGlobal         = false;
    p.mIsBaby           = false;
    p.mFireAtPosition   = std::nullopt;
    p.sendTo(BlockPos{static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(pos.z)}, region.getDimensionId());
}

void playBlockSound(BlockSource& region, Sound sound, BlockPos const& pos, Block const& block) {
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    playSound(region, sound, at, static_cast<int>(block.mNetworkId));
}

void showTip(Player const& player, std::string const& text) {
    auto        packet = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(text);
    packet.mBody = message;
    packet.sendTo(player);
}

void runCommand(std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return;
    ServerCommandOrigin origin("complementaries-tweak", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin, CommandOutputType::None);
}

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

Player* findPlayer(ActorUniqueID id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->getPlayer(id);
}

void defer(std::function<void()> fn) { deferred().push_back(std::move(fn)); }

void runDeferred() {
    if (deferred().empty()) return;
    auto work = std::move(deferred());
    deferred().clear();
    for (auto& fn : work) fn();
}

void clearDeferred() { deferred().clear(); }

namespace {
long long gTick = 0;
}
long long currentTick() { return gTick; }
void      advanceTick() { ++gTick; }

BlockPos offset(BlockPos const& pos, int face) {
    switch (face) {
    case 0:
        return {pos.x, pos.y - 1, pos.z};
    case 1:
        return {pos.x, pos.y + 1, pos.z};
    case 2:
        return {pos.x, pos.y, pos.z - 1};
    case 3:
        return {pos.x, pos.y, pos.z + 1};
    case 4:
        return {pos.x - 1, pos.y, pos.z};
    case 5:
        return {pos.x + 1, pos.y, pos.z};
    default:
        return pos;
    }
}

int faceAxis(int face) {
    if (face <= 1) return 1;
    if (face <= 3) return 2;
    return 0;
}

Block const* withState(Block const& block, std::string_view state, int value) {
    auto const* s = block.getBlockType().getBlockState(HashedString(std::string(state)));
    if (!s) return nullptr;
    auto b = block.setState(*s, value);
    return b ? b.as_ptr() : nullptr;
}

bool isSolid(Block const& block) { return !block.isAir() && block._isSolid(); }

bool isInteractive(Block const& block) { return block.getBlockType().isInteractiveBlock(); }

} // namespace cm::feature::tweak
