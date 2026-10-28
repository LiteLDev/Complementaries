#include <algorithm>
#include <cmath>
#include <memory>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/FloatTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "game/Blocks.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/tool/Tool.h"
#include "mod/Config.h"
#include "mod/I18n.h"

CM_REGISTER_CONFIG("tool", cm::feature::tool::ToolConfig);

namespace cm::feature::tool {

ToolConfig const& config() { return cm::config::get<ToolConfig>(); }

Level* level() {
    auto l = ll::service::getLevel();
    return l ? l.as_ptr() : nullptr;
}

std::uint64_t currentTick() {
    auto* l = level();
    return l ? l->getCurrentServerTick().tickID : 0;
}

ItemStack makeItem(std::string_view id, int count, int aux) {
    ItemStack stack;
    stack.reinit(id, count, aux);
    return stack;
}

std::string itemId(ItemStackBase const& item) {
    if (item.isNull()) return {};
    return item.getTypeName();
}

bool isItem(ItemStackBase const& item, std::string_view id) { return !item.isNull() && item.getTypeName() == id; }

bool isCreative(Player const& player) { return player.getPlayerGameType() == GameType::Creative; }

Player* playerById(std::int64_t uniqueId) {
    auto* l = level();
    if (!l) return nullptr;
    ActorUniqueID id;
    id.rawID = uniqueId;
    return l->getPlayer(id);
}

Actor* actorById(std::int64_t uniqueId) {
    auto* l = level();
    if (!l) return nullptr;
    ActorUniqueID id;
    id.rawID = uniqueId;
    return l->fetchEntity(id, false);
}

Vec3 lookVector(Actor const& actor) { return actor.getViewVector(1.0f); }

Vec3 eyePos(Actor const& actor) { return actor.getEyePos(); }

Vec3 center(BlockPos const& pos) {
    return Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
}

float length(Vec3 const& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec3 normalized(Vec3 const& v) {
    float l = length(v);
    if (l < 1e-6f) return Vec3{0, 0, 0};
    return Vec3{v.x / l, v.y / l, v.z / l};
}

BlockPos faceOffset(int face) {
    switch (face) {
    case 0:
        return {0, -1, 0};
    case 1:
        return {0, 1, 0};
    case 2:
        return {0, 0, -1};
    case 3:
        return {0, 0, 1};
    case 4:
        return {-1, 0, 0};
    case 5:
        return {1, 0, 0};
    default:
        return {0, 0, 0};
    }
}

BlockSource* regionOf(int dim) {
    auto* l = level();
    if (!l) return nullptr;
    auto d = l->getDimension(DimensionType(dim)).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

void playSound(int dim, Vec3 const& pos, std::string const& name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName   = name;
    // the protocol carries the position in 1/8 blocks
    pkt.mPos    = BlockPos{
        static_cast<int>(std::floor(pos.x * 8.0f)),
        static_cast<int>(std::floor(pos.y * 8.0f)),
        static_cast<int>(std::floor(pos.z * 8.0f)),
    };
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    auto* l     = level();
    if (!l) return;
    l->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == dim && p.getPosition().distanceToSqr(pos) < 48.0f * 48.0f) {
            pkt.sendTo(p);
        }
        return true;
    });
}

void particle(int dim, Vec3 const& pos, std::string const& effect) {
    auto* l = level();
    if (!l) return;
    auto d = l->getDimension(DimensionType(dim)).lock();
    if (!d) return;
    l->spawnParticleEffect(effect, pos, d.get());
}

void syncMotion(Actor& actor) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
    if (!packet) return;
    auto& pkt      = static_cast<SetActorMotionPacket&>(*packet);
    pkt.mRuntimeId = actor.getRuntimeID();
    pkt.mMotion    = actor.getPosDelta();
    pkt.sendTo(actor);
}

void setVelocity(Actor& actor, Vec3 const& v) {
    const_cast<Vec3&>(actor.getPosDelta()) = v;
    syncMotion(actor);
}

namespace {
std::vector<std::function<void()>>& queue() {
    static std::vector<std::function<void()>> q;
    return q;
}
} // namespace

void nextTick(std::function<void()> fn) { queue().push_back(std::move(fn)); }

void runQueued() {
    if (queue().empty()) return;
    auto tasks = std::move(queue());
    queue().clear();
    for (auto& t : tasks) t();
}

void setMainhand(Player& player, ItemStack const& item) {
    player.setSelectedItem(item);
    player.sendInventory(false);
}

void setOffhand(Player& player, ItemStack const& item) {
    player.setOffhandSlot(item);
    player.sendInventory(false);
}

void consumeMainhand(Player& player, int count) {
    if (isCreative(player)) return;
    ItemStack item = player.getSelectedItem();
    if (item.isNull()) return;
    if (item.mCount <= count) item = ItemStack::EMPTY_ITEM();
    else item.remove(count);
    setMainhand(player, item);
}

void consumeOffhand(Player& player, int count) {
    if (isCreative(player)) return;
    ItemStack item = player.getOffhandSlot();
    if (item.isNull()) return;
    if (item.mCount <= count) item = ItemStack::EMPTY_ITEM();
    else item.remove(count);
    setOffhand(player, item);
}

void damageMainhand(Player& player, int amount) {
    if (isCreative(player)) return;
    ItemStack item = player.getSelectedItem();
    if (item.isNull() || !item.isDamageableItem()) return;
    item.hurtAndBreak(amount, &player);
    if (item.isNull() || item.mCount == 0) item = ItemStack::EMPTY_ITEM();
    setMainhand(player, item);
}

int intData(ItemStackBase const& item, std::string const& key, int fallback) {
    auto const& tag = item.mUserData;
    if (!tag || !tag->contains(key)) return fallback;
    auto const& v = (*tag)[key];
    if (v.hold<IntTag>()) return v.get<IntTag>().data;
    if (v.hold<ByteTag>()) return v.get<ByteTag>().data;
    return fallback;
}

void setIntData(ItemStackBase& item, std::string const& key, int value) {
    if (!item.mUserData) item.mUserData = std::make_unique<CompoundTag>();
    item.mUserData->putInt(key, value);
}

void removeData(ItemStackBase& item, std::string const& key) {
    if (!item.mUserData) return;
    item.mUserData->mTags.erase(key);
}

std::string stringData(ItemStackBase const& item, std::string const& key) {
    auto const& tag = item.mUserData;
    if (!tag || !tag->contains(key)) return {};
    auto const& v = (*tag)[key];
    if (v.hold<StringTag>()) return static_cast<std::string const&>(v.get<StringTag>());
    return {};
}

void setStringData(ItemStackBase& item, std::string const& key, std::string const& value) {
    if (!item.mUserData) item.mUserData = std::make_unique<CompoundTag>();
    item.mUserData->putString(key, value);
}

void setLoreLine(ItemStackBase& item, std::string const& prefix, std::string const& line) {
    auto lore = item.getCustomLore();
    std::erase_if(lore, [&](std::string const& l) { return l.starts_with(prefix); });
    if (!line.empty()) lore.push_back(line);
    if (lore.empty()) item.clearCustomLore();
    else item.setCustomLore(lore);
}

States blockStates(Block const& block) {
    States       out;
    auto const& ser = *block.mSerializationId;
    if (!ser.contains("states")) return out;
    auto const& st = ser["states"];
    if (!st.is_object()) return out;
    for (auto const& [name, v] : st.items()) {
        if (v.hold<IntTag>()) out.emplace_back(name, static_cast<int>(v.get<IntTag>().data));
        else if (v.hold<ByteTag>()) out.emplace_back(name, v.get<ByteTag>().data != 0);
        else if (v.hold<StringTag>()) out.emplace_back(name, std::string(static_cast<std::string const&>(v.get<StringTag>())));
        else if (v.hold<FloatTag>()) out.emplace_back(name, v.get<FloatTag>().data);
    }
    return out;
}

Block const* blockWithStates(std::string const& id, States const& states) {
    auto type = BlockType::tryGetFromRegistry(HashedString(id));
    if (!type) return nullptr;
    // the lookup by serialization NBT (the name + states lookup ignores the states)
    CompoundTag nbt;
    nbt.putString("name", id);
    CompoundTag st;
    for (auto const& [name, value] : states) {
        if (!type->getBlockState(HashedString(name))) continue;
        if (std::holds_alternative<int>(value)) st.putInt(name, std::get<int>(value));
        else if (std::holds_alternative<bool>(value)) st.putByte(name, std::get<bool>(value) ? 1 : 0);
        else if (std::holds_alternative<std::string>(value)) st.putString(name, std::get<std::string>(value));
        else st.putFloat(name, std::get<float>(value));
    }
    nbt.putCompound("states", std::move(st));
    if (auto b = Block::tryGetFromRegistry(nbt)) return b.as_ptr();
    return nullptr;
}

bool replaceKeepingData(BlockSource& region, BlockPos const& pos, Block const& block) {
    std::unique_ptr<CompoundTag> saved;
    if (auto* ba = region.getBlockEntity(pos)) {
        saved = std::make_unique<CompoundTag>();
        if (!ba->save(*saved, *SaveContextFactory::createCloneSaveContext())) saved.reset();
    }
    if (!game::setBlock(region, pos, block)) return false;
    if (saved) {
        if (auto* nb = region.getBlockEntity(pos)) {
            DefaultDataLoadHelper helper;
            nb->load(region.getLevel(), *saved, helper);
            nb->refresh(region);
        }
    }
    return true;
}

int stasisLevel(ItemStackBase const& item) { return intData(item, "cm_stasis", 0); }

void setStasisLevel(ItemStackBase& item, int lvl) {
    // the lore line carries a colour code so it reads like an enchantment line
    std::string const prefix = fmtCode("r7") + tr("enchantment.supplementaries.stasis", "Stasis");
    if (lvl <= 0) {
        removeData(item, "cm_stasis");
        setLoreLine(item, prefix, "");
        return;
    }
    static char const* roman[] = {"", "I", "II", "III", "IV", "V"};
    setIntData(item, "cm_stasis", lvl);
    setLoreLine(item, prefix, prefix + " " + (lvl <= 5 ? roman[lvl] : std::to_string(lvl)));
}

int enchantLevel(ItemStackBase const& item, int enchantType) {
    if (item.isNull()) return 0;
    return EnchantUtils::getEnchantLevel(static_cast<Enchant::Type>(enchantType), item);
}

} // namespace cm::feature::tool
