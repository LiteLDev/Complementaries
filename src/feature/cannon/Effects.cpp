// Small game helpers of the cannon unit: sounds, particles, action bar, spawning, items.

#include <cstdio>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"

#include "feature/cannon/Cannon.h"
#include "mod/Log.h"

namespace cm::feature::cannon {

namespace {
std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.5f", v);
    return buf;
}
std::string quoted(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}
void sendMotion(Actor& actor, Vec3 const& motion) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
    if (!packet) return;
    auto& pkt      = static_cast<SetActorMotionPacket&>(*packet);
    pkt.mRuntimeId = actor.getRuntimeID();
    pkt.mMotion    = motion;
    pkt.sendTo(actor);
}
} // namespace

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

void playSound(int dim, Vec3 const& pos, std::string const& name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt     = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName     = name;
    pkt.mPos      = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    pkt.mVolume   = volume;
    pkt.mPitch    = pitch;
    auto level    = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == dim && p.getPosition().distanceTo(pos) < 48.0f) pkt.sendTo(p);
        return true;
    });
}

void particle(int dim, Vec3 const& pos, std::string const& name) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return;
    level->spawnParticleEffect(name, pos, d.get());
}

void particleFor(Player& player, Vec3 const& pos, std::string const& name) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SpawnParticleEffect);
    if (!packet) return;
    auto& pkt               = static_cast<SpawnParticleEffectPacket&>(*packet);
    pkt.mVanillaDimensionId = static_cast<uchar>(static_cast<int>(player.getDimensionId()));
    pkt.mActorId            = ActorUniqueID{-1};
    pkt.mPos                = pos;
    pkt.mEffectName         = name;
    pkt.sendTo(player);
}

void actionBar(Player& player, std::string const& text) {
    SetTitlePacket pkt(SetTitlePacketPayload::TitleType::Actionbar, text, std::nullopt);
    pkt.sendTo(player);
}

Actor* spawnActor(BlockSource& region, std::string_view id, Vec3 const& pos, Vec3 const& motion, int64_t owner, std::string const& extra) {
    std::string snbt = "{identifier:" + quoted(id) + ",Pos:[" + num(pos.x) + "f," + num(pos.y) + "f," + num(pos.z) + "f],Motion:["
                     + num(motion.x) + "f," + num(motion.y) + "f," + num(motion.z) + "f]";
    if (owner != 0) snbt += ",OwnerNew:" + std::to_string(owner) + "L";
    if (!extra.empty()) snbt += "," + extra;
    snbt += "}";
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) {
        log().warn("cannon: bad actor tag {}", snbt);
        return nullptr;
    }
    auto actor = region.spawnActor(*tag);
    if (!actor) return nullptr;
    Actor& a = *actor;
    // make sure the motion is applied even if the definition resets it
    a.mBuiltInComponents->mStateVectorComponent->mPosDelta = motion;
    sendMotion(a, motion);
    return &a;
}

Actor* shootProjectile(BlockSource& region, std::string_view id, Vec3 const& pos, Vec3 const& velocity, int64_t owner) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Actor* shooter = owner ? level->fetchEntity(ActorUniqueID{owner}, false) : nullptr;
    Vec3   dir     = velocity;
    if (double len = dir.length(); len > 1e-6) dir = dir * static_cast<float>(1.0 / len);
    Actor* actor = level->getSpawner().spawnProjectile(region, ActorDefinitionIdentifier(std::string(id)), shooter, pos, dir);
    if (!actor) return spawnActor(region, id, pos, velocity, owner);
    actor->mBuiltInComponents->mStateVectorComponent->mPosDelta = velocity;
    sendMotion(*actor, velocity);
    return actor;
}

Actor* dropItem(BlockSource& region, ItemStack const& stack, Vec3 const& pos, Vec3 const& motion) {
    if (stack.isNull()) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Actor* item = level->getSpawner().spawnItem(region, stack, nullptr, pos, 0);
    if (!item) return nullptr;
    item->mBuiltInComponents->mStateVectorComponent->mPosDelta = motion;
    sendMotion(*item, motion);
    return item;
}

ItemStack makeItem(std::string_view id, int count, int aux) {
    ItemStack s;
    s.reinit(id, count, aux);
    return s;
}

std::string itemToSnbt(ItemStack const& stack) {
    if (stack.isNull()) return {};
    auto ctx = SaveContextFactory::createCloneSaveContext();
    auto tag = stack.save(*ctx);
    return tag ? tag->toSnbt(SnbtFormat::Minimize, 0) : std::string{};
}

ItemStack itemFromSnbt(std::string const& snbt) {
    if (snbt.empty()) return {};
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) return {};
    return ItemStack::fromTag(*tag);
}

void runCommand(std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return;
    ServerCommandOrigin origin("complementaries-cannon", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
}

} // namespace cm::feature::cannon
