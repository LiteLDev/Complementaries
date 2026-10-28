#include "feature/sign/SignGame.h"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/actor/Mob.h"
#include "mc/deps/vanilla_components/ActorDataDirtyFlagsComponent.h"
#include "mc/entity/components/SynchedActorDataComponent.h"
#include "mc/world/actor/DataItem.h"

#include "mod/Log.h"

namespace cm::feature::sign {

ItemStack makeItem(std::string_view id, int count) {
    CompoundTag tag;
    tag["Name"]   = std::string(id);
    tag["Count"]  = static_cast<uchar>(count);
    tag["Damage"] = static_cast<short>(0);
    return ItemStack::fromTag(tag);
}

std::string itemId(ItemStack const& item) {
    if (item.isNull()) return {};
    return item.getTypeName();
}

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto dimension = level->getDimension(static_cast<DimensionType>(dim)).lock();
    if (!dimension) return nullptr;
    return &dimension->getBlockSourceFromMainChunkSource();
}

bool loadedAt(BlockSource& region, BlockPos const& pos) { return region.hasChunksAt(pos, 0, false); }

void dropItem(BlockSource& region, Vec3 const& pos, ItemStack const& item) {
    if (item.isNull()) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    level->getSpawner().spawnItem(region, item, nullptr, pos, 10);
}

void playSound(int dim, Vec3 const& pos, std::string const& sound, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName   = sound;
    pkt.mPos    = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    pkt.sendTo(BlockPos{static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(pos.z)}, static_cast<DimensionType>(dim));
}

void consumeSelected(Player& player) {
    if (player.isCreative()) return;
    ItemStack item = player.getSelectedItem();
    if (item.isNull()) return;
    item.remove(1);
    if (item.mCount == 0) item = ItemStack{};
    player.setSelectedItem(item);
    player.refreshInventory();
}

void damageSelected(Player& player) {
    if (player.isCreative()) return;
    ItemStack item = player.getSelectedItem();
    if (item.isNull() || !item.isDamageableItem()) return;
    item.setDamageValue(static_cast<short>(item.getDamageValue() + 1));
    player.setSelectedItem(item);
    player.refreshInventory();
}

std::int64_t spawnDisplay(BlockSource& region, std::string_view type, Vec3 const& pos) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    ActorDefinitionIdentifier id{std::string(type)};
    auto* actor = level->getSpawner().spawnMob(region, id, nullptr, pos, false, false, false);
    if (!actor) {
        log().warn("sign: could not spawn {} (behavior pack missing?)", type);
        return 0;
    }
    return actor->getOrCreateUniqueID().rawID;
}

Actor* findActor(std::int64_t id) {
    if (!id) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID   = id;
    auto* actor = level->fetchEntity(uid, false);
    if (!actor || !actor->isAlive()) return nullptr;
    return actor;
}

namespace {
/// Writes an int synched data item and marks it dirty (the templated setters aren't exported).
void setSynchedInt(Actor& actor, ActorDataIDs which, int value) {
    auto&                      wrapper = *actor.mEntityData;
    SynchedActorDataComponent* comp    = wrapper.mData.get();
    auto&                      data    = comp->mData.get();
    auto&                      items   = data.mItemsArray.get();
    auto                       id      = static_cast<ushort>(which);
    if (id >= items.size()) items.resize(id + 1);
    if (!items[id] || !items[id]->setData(int{value})) items[id] = DataItem::create(id, int{value});
    data.mDirtyFlags->set(id);
    ActorDataDirtyFlagsComponent* dirty = wrapper.mDirtyFlags.get();
    dirty->mDirtyFlags->set(id);
}
} // namespace

void applyDisplay(Actor& actor, Vec3 const& pos, int variant, int markVariant, std::string const& name) {
    if (actor.getVariant() != variant) setSynchedInt(actor, ActorDataIDs::Variant, variant);
    if (actor.getMarkVariant() != markVariant) setSynchedInt(actor, ActorDataIDs::MarkVariant, markVariant);
    if (actor.getNameTag() != name) actor.setNameTag(name);
    if (actor.getPosition().distanceToSqr(pos) > 1.0e-4f) actor.teleportTo(pos, true, 0, 0, false);
}

void removeActor(Actor& actor) { actor.remove(); }

} // namespace cm::feature::sign
