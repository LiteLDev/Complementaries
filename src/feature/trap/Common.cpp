// Shared helpers of the trap unit: items, blocks, sounds, particles, data keys.

#include "feature/trap/Trap.h"

#include <charconv>
#include <cmath>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"

namespace cm::feature::trap {

ItemStack makeItem(std::string_view id, int count, int aux) {
    ItemStack item;
    item.reinit(id, count, aux);
    return item;
}

std::string itemId(ItemStack const& item) { return item.isNull() ? std::string{} : item.getTypeName(); }

bool isBlock(Block const& block, std::string_view id) { return block.getTypeName() == id; }

int stateOf(Block const& block, std::string_view state, int fallback) {
    return game::intState(block, state).value_or(fallback);
}

bool setState(BlockSource& region, BlockPos const& pos, std::string_view state, int value) {
    auto const& block = region.getBlock(pos);
    auto const* s     = block.getBlockType().getBlockState(HashedString(std::string(state)));
    if (!s) return false;
    auto next = block.setState(*s, value);
    if (!next) return false;
    return game::setBlock(region, pos, *next.as_ptr());
}

void spawnItem(BlockSource& region, Vec3 const& pos, ItemStack const& item) {
    if (item.isNull()) return;
    region.getLevel().getSpawner().spawnItem(region, item, nullptr, pos, 10);
}

void playSound(BlockSource& region, Vec3 const& pos, std::string const& name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& p   = static_cast<PlaySoundPacket&>(*packet);
    p.mName   = name;
    p.mPos    = BlockPos{
        static_cast<int>(std::floor(pos.x * 8.f)),
        static_cast<int>(std::floor(pos.y * 8.f)),
        static_cast<int>(std::floor(pos.z * 8.f)),
    };
    p.mVolume = volume;
    p.mPitch  = pitch;
    region.getDimension().sendPacketForPosition(game::blockPosOf(pos), p, nullptr);
}

void particle(BlockSource& region, Vec3 const& pos, std::string const& effect) {
    region.getLevel().spawnParticleEffect(effect, pos, &region.getDimension());
}

void blockParticles(BlockSource& region, BlockPos const& pos, Block const& block) {
    Vec3 center{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    region.getLevel().broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlockNoSound,
        center,
        static_cast<int>(block.mNetworkId),
        nullptr
    );
}

void runCommand(std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return;
    ServerCommandOrigin origin("complementaries-trap", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
}

uint64_t currentTick() {
    auto level = ll::service::getLevel();
    return level ? level->getCurrentTick().tickID : 0;
}

bool isCreative(Player const& player) { return player.getPlayerGameType() == GameType::Creative; }

void swapHeld(Player& player, ItemStack const& result) {
    ItemStack held = player.getSelectedItem();
    if (held.mCount <= 1) {
        player.setSelectedItem(result);
    } else {
        held.remove(1);
        player.setSelectedItem(held);
        ItemStack copy = result;
        if (!player.add(copy)) player.drop(result, false);
    }
    player.sendInventory(false);
}

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& p      = static_cast<SetTitlePacket&>(*packet);
    p.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    p.mTitleText = text;
    p.sendTo(player);
}

CompoundTag const* userData(ItemStack const& item) { return item.mUserData.get(); }

CompoundTag& userDataMut(ItemStack& item) {
    if (!item.mUserData) item.mUserData = std::make_unique<CompoundTag>();
    return *item.mUserData;
}

std::string dataKey(std::string_view kind, core::GlobalPos const& pos) {
    return "cm:trap:" + std::string(kind) + ":" + pos.key();
}

std::optional<core::GlobalPos> parseKeySuffix(std::string_view suffix) {
    int  values[4]{};
    auto rest = suffix;
    for (int i = 0; i < 4; ++i) {
        auto colon = rest.find(':');
        auto part  = rest.substr(0, colon);
        auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), values[i]);
        if (ec != std::errc{} || ptr != part.data() + part.size()) return std::nullopt;
        if (i < 3) {
            if (colon == std::string_view::npos) return std::nullopt;
            rest = rest.substr(colon + 1);
        } else if (colon != std::string_view::npos) {
            return std::nullopt;
        }
    }
    return core::GlobalPos{values[0], values[1], values[2], values[3]};
}

int64_t uid(Actor const& actor) { return actor.getOrCreateUniqueID().rawID; }

Actor* actorByUid(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level || id == 0) return nullptr;
    ActorUniqueID u;
    u.rawID = id;
    return level->fetchEntity(u, false);
}

} // namespace cm::feature::trap
