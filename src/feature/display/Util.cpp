// Small game helpers used by the display unit (items, sounds, entity events, giving items).

#include "feature/display/Display.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

namespace cm::feature::display {

ItemStack makeItem(std::string_view id, int count, int aux) {
    ItemStack item;
    item.reinit(id, count, aux);
    return item;
}

std::string itemId(ItemStack const& item) {
    if (item.isNull()) return {};
    return item.getTypeName();
}

std::string itemToSnbt(ItemStack const& item) {
    if (item.isNull()) return {};
    auto ctx = SaveContextFactory::createCloneSaveContext();
    auto tag = item.save(*ctx);
    if (!tag) return {};
    return tag->toSnbt(SnbtFormat::Minimize, 0);
}

std::optional<ItemStack> itemFromSnbt(std::string const& snbt) {
    if (snbt.empty()) return std::nullopt;
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) return std::nullopt;
    ItemStack item = ItemStack::fromTag(*tag);
    if (item.isNull()) return std::nullopt;
    return item;
}

void playSound(BlockSource& region, float x, float y, float z, std::string const& name, float volume, float pitch) {
    PlaySoundPacket pkt;
    pkt.mName   = name;
    pkt.mPos    = BlockPos{static_cast<int>(x * 8.0f), static_cast<int>(y * 8.0f), static_cast<int>(z * 8.0f)};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    pkt.sendTo(BlockPos{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)}, region.getDimensionId());
}

void entityEvent(Actor& actor, std::string const& name) {
    VariantParameterList params{};
    params.mSelf = &actor;
    actor.executeEvent("complementaries:" + name, params);
}

void dropItem(BlockSource& region, float x, float y, float z, ItemStack const& item) {
    if (item.isNull()) return;
    region.getLevel().getSpawner().spawnItem(region, item, nullptr, Vec3{x, y, z}, 0);
}

void giveToPlayer(Player& player, ItemStack const& item) {
    if (item.isNull()) return;
    if (player.getSelectedItem().isNull()) {
        player.setSelectedItem(item);
    } else {
        ItemStack copy = item;
        if (!player.add(copy) && !copy.isNull()) {
            auto const& p = player.getPosition();
            dropItem(player.getDimensionBlockSource(), p.x, p.y, p.z, copy);
        }
    }
    player.sendInventory(false);
}

bool isCreative(Player const& player) { return player.getPlayerGameType() == GameType::Creative; }

} // namespace cm::feature::display
