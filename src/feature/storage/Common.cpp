#include "feature/storage/Common.h"

#include <cstdio>
#include <random>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"

namespace cm::feature::storage {

std::array<std::string_view, 16> const kColors = {
    "white", "orange", "magenta", "light_blue", "yellow", "lime",   "pink",  "gray",
    "light_gray", "cyan", "purple",  "blue",       "brown",  "green", "red",   "black",
};

namespace {
constexpr std::string_view kNs        = "complementaries:";
constexpr std::string_view kPresent   = "present";
constexpr std::string_view kTrapped   = "trapped_present";
constexpr char const*      kTagRoot   = "cm_storage";
constexpr char const*      kInvPrefix = "cm:storage:inv:";

std::string blockKey(core::GlobalPos const& pos) { return "cm:storage:block:" + pos.key(); }

/// Content records read through a cache (the quiver checks its record every tick while a bow is held).
std::unordered_map<std::string, std::optional<nlohmann::json>> gContentCache;

std::optional<nlohmann::json> const& cachedContents(std::string const& uid) {
    auto it = gContentCache.find(uid);
    if (it == gContentCache.end()) it = gContentCache.emplace(uid, game::db::load(kInvPrefix + uid)).first;
    return it->second;
}

bool isColor(std::string_view c) {
    for (auto k : kColors)
        if (k == c) return true;
    return false;
}
} // namespace

// ---- ids -------------------------------------------------------------------------------------------

std::optional<std::string> presentColor(std::string_view id) {
    if (!id.starts_with(kNs)) return std::nullopt;
    id.remove_prefix(kNs.size());
    for (auto base : {kTrapped, kPresent}) {
        if (!id.starts_with(base)) continue;
        auto rest = id.substr(base.size());
        if (rest.empty()) return std::string();
        if (rest.front() == '_' && isColor(rest.substr(1))) return std::string(rest.substr(1));
        return std::nullopt;
    }
    return std::nullopt;
}

Kind kindOf(std::string_view id) {
    if (id == kSack) return Kind::Sack;
    if (presentColor(id)) {
        return id.substr(kNs.size()).starts_with(kTrapped) ? Kind::TrappedPresent : Kind::Present;
    }
    return Kind::None;
}

std::string presentId(bool trapped, std::string_view color) {
    std::string id = std::string(kNs) + std::string(trapped ? kTrapped : kPresent);
    if (!color.empty()) id += "_" + std::string(color);
    return id;
}

// ---- items -----------------------------------------------------------------------------------------

ItemStack makeStack(std::string_view id, int count, int aux) {
    ItemStack s;
    s.reinit(id, count, aux);
    return s;
}

std::string itemId(ItemStack const& stack) { return isEmpty(stack) ? std::string() : stack.getTypeName(); }

bool isEmpty(ItemStack const& stack) { return stack.isNull() || stack.mCount == 0; }

int countOf(ItemStack const& stack) { return isEmpty(stack) ? 0 : static_cast<int>(stack.mCount); }

std::string stackToSnbt(ItemStack const& stack) {
    if (isEmpty(stack)) return {};
    auto tag = stack.save(*SaveContextFactory::createCloneSaveContext());
    return tag ? tag->toSnbt(SnbtFormat::Minimize, 0) : std::string();
}

ItemStack stackFromSnbt(std::string const& snbt) {
    if (snbt.empty()) return ItemStack();
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) return ItemStack();
    return ItemStack::fromTag(*tag);
}

CompoundTag const* storageTag(ItemStack const& stack) {
    if (!stack.mUserData || !stack.mUserData->contains(kTagRoot, Tag::Type::Compound)) return nullptr;
    return &stack.mUserData->at(kTagRoot).get<CompoundTag>();
}

CompoundTag& storageTagMut(ItemStack& stack) {
    if (!stack.mUserData) stack.mUserData = std::make_unique<CompoundTag>();
    if (!stack.mUserData->contains(kTagRoot, Tag::Type::Compound)) (*stack.mUserData)[kTagRoot] = CompoundTag{};
    return (*stack.mUserData)[kTagRoot].get<CompoundTag>();
}

std::string tagString(ItemStack const& stack, std::string_view key) {
    auto const* t = storageTag(stack);
    if (!t || !t->contains(key, Tag::Type::String)) return {};
    return t->at(key).get<StringTag>();
}

int tagInt(ItemStack const& stack, std::string_view key, int fallback) {
    auto const* t = storageTag(stack);
    if (!t || !t->contains(key, Tag::Type::Int)) return fallback;
    return t->at(key).get<IntTag>();
}

void setTagString(ItemStack& stack, std::string_view key, std::string const& value) {
    storageTagMut(stack)[key] = value;
}

void setTagInt(ItemStack& stack, std::string_view key, int value) { storageTagMut(stack)[key] = value; }

std::string itemUid(ItemStack const& stack) { return tagString(stack, "uid"); }

std::string ensureUid(ItemStack& stack) {
    auto uid = itemUid(stack);
    if (uid.empty()) {
        uid = newUid();
        setTagString(stack, "uid", uid);
    }
    return uid;
}

std::string newUid() {
    static std::mt19937_64 rng{std::random_device{}()};
    char                   buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)rng(), (unsigned long long)rng());
    return buf;
}

// ---- content records -------------------------------------------------------------------------------

std::vector<ItemStack> loadContents(std::string const& uid, int size) {
    std::vector<ItemStack> items(static_cast<size_t>(std::max(size, 0)));
    if (uid.empty()) return items;
    auto const& j = cachedContents(uid);
    if (!j || !j->contains("items") || !j->at("items").is_array()) return items;
    std::vector<ItemStack> overflow;
    for (auto const& e : j->at("items")) {
        int  slot = e.value("slot", -1);
        auto s    = stackFromSnbt(e.value("nbt", std::string()));
        if (isEmpty(s)) continue;
        if (slot >= 0 && slot < size && isEmpty(items[slot])) items[slot] = std::move(s);
        else overflow.push_back(std::move(s));
    }
    // a config change that shrank the container: keep the extra stacks after the visible slots
    for (auto& s : overflow) items.push_back(std::move(s));
    return items;
}

void saveContents(std::string const& uid, std::vector<ItemStack> const& items) {
    if (uid.empty()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < items.size(); ++i) {
        if (isEmpty(items[i])) continue;
        arr.push_back({{"slot", static_cast<int>(i)}, {"nbt", stackToSnbt(items[i])}});
    }
    if (arr.empty()) {
        game::db::erase(kInvPrefix + uid);
        gContentCache[uid] = std::nullopt;
    } else {
        nlohmann::json doc = {{"items", arr}};
        game::db::save(kInvPrefix + uid, doc);
        gContentCache[uid] = std::move(doc);
    }
}

void eraseContents(std::string const& uid) {
    if (uid.empty()) return;
    game::db::erase(kInvPrefix + uid);
    gContentCache[uid] = std::nullopt;
}

void clearCaches() { gContentCache.clear(); }

bool contentsEmpty(std::vector<ItemStack> const& items) {
    for (auto const& s : items)
        if (!isEmpty(s)) return false;
    return true;
}

std::string stackName(ItemStack const& stack) {
    if (isEmpty(stack)) return {};
    auto name = stack.getName();
    if (name.empty()) name = stack.getTypeName();
    return name;
}

std::vector<std::string> contentLore(std::vector<ItemStack> const& items, int selected) {
    std::vector<std::string> lore;
    int                      shown = 0, hidden = 0;
    auto line = [&](size_t i) {
        auto const& s = items[i];
        return std::string(static_cast<int>(i) == selected ? "§r§f> " : "§r§7") + stackName(s)
             + " x" + std::to_string(countOf(s));
    };
    if (selected >= 0 && selected < static_cast<int>(items.size()) && !isEmpty(items[selected])) {
        lore.push_back(line(static_cast<size_t>(selected)));
        ++shown;
    }
    for (size_t i = 0; i < items.size(); ++i) {
        if (isEmpty(items[i]) || static_cast<int>(i) == selected) continue;
        if (shown < 5) {
            lore.push_back(line(i));
            ++shown;
        } else {
            ++hidden;
        }
    }
    if (hidden > 0) lore.push_back("§r§7§o" + tr("cm.storage.more", "and {} more...", hidden));
    return lore;
}

// ---- block records ---------------------------------------------------------------------------------

std::optional<BlockRecord> loadBlockRecord(core::GlobalPos const& pos) {
    auto j = game::db::load(blockKey(pos));
    if (!j || !j->is_object()) return std::nullopt;
    BlockRecord r;
    r.uid         = j->value("uid", std::string());
    r.packed      = j->value("packed", false);
    r.recipient   = j->value("recipient", std::string());
    r.sender      = j->value("sender", std::string());
    r.description = j->value("description", std::string());
    return r;
}

void saveBlockRecord(core::GlobalPos const& pos, BlockRecord const& r) {
    game::db::save(
        blockKey(pos),
        {{"uid", r.uid}, {"packed", r.packed}, {"recipient", r.recipient}, {"sender", r.sender}, {"description", r.description}}
    );
}

void eraseBlockRecord(core::GlobalPos const& pos) { game::db::erase(blockKey(pos)); }

BlockRecord blockRecordOrNew(core::GlobalPos const& pos) {
    auto r = loadBlockRecord(pos);
    if (r && !r->uid.empty()) return *r;
    BlockRecord fresh = r.value_or(BlockRecord{});
    fresh.uid         = newUid();
    saveBlockRecord(pos, fresh);
    return fresh;
}

// ---- blocks ----------------------------------------------------------------------------------------

Kind kindOf(Block const& block) { return kindOf(std::string_view(block.getTypeName())); }

bool boolState(Block const& block, std::string_view state) { return game::intState(block, state).value_or(0) != 0; }

void setBoolState(BlockSource& region, BlockPos const& pos, std::string_view state, bool value) {
    auto const& cur = region.getBlock(pos);
    if (boolState(cur, state) == value) return;
    std::vector<std::pair<std::string_view, int>> states;
    // keep the other bool states of our blocks
    for (auto s : {kStateOpen, kStatePacked}) {
        if (s == state) continue;
        if (auto v = game::intState(cur, s)) states.emplace_back(s, *v);
    }
    states.emplace_back(state, value ? 1 : 0);
    if (auto const* b = game::blockWith(cur.getTypeName(), states)) game::setBlock(region, pos, *b);
}

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

// ---- players ---------------------------------------------------------------------------------------

void actionbar(Player& player, std::string const& text) {
    SetTitlePacket pkt(SetTitlePacketPayload::TitleType::Actionbar, text, std::nullopt);
    pkt.sendTo(player);
}

void spawnItem(BlockSource& region, ItemStack const& stack, Vec3 const& pos) {
    if (isEmpty(stack)) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    level->getSpawner().spawnItem(region, stack, nullptr, pos, 0);
}

void giveOrDrop(Player& player, ItemStack stack) {
    if (isEmpty(stack)) return;
    // add() takes what fits and leaves the rest in `stack`; it returns false when something is left
    if (!player.add(stack) && !isEmpty(stack)) player.drop(stack, false);
    player.sendInventory(false);
}

void playSound(BlockSource& region, Vec3 const& pos, std::string const& sound) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!pkt) return;
    auto& p   = static_cast<PlaySoundPacket&>(*pkt);
    p.mName   = sound;
    p.mPos    = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    p.mVolume = 1.0f;
    p.mPitch  = 1.0f;
    p.sendTo(game::blockPosOf(pos), region.getDimensionId());
}

long long playerKey(Player const& player) { return player.getOrCreateUniqueID().rawID; }

Player* playerById(long long uniqueId) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uniqueId;
    return level->getPlayer(id);
}

ItemStack const& slotItem(Player& player, int slot) {
    if (slot < 0) return player.getOffhandSlot();
    return player.getInventory().getItem(slot);
}

void setSlotItem(Player& player, int slot, ItemStack const& stack) {
    if (slot < 0) player.setOffhandSlot(stack);
    else player.getInventory().setItem(slot, stack);
}

void forEachSlot(Player& player, std::function<bool(int, ItemStack const&)> const& fn) {
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (fn(i, inv.getItem(i))) return;
    }
    fn(-1, player.getOffhandSlot());
}

} // namespace cm::feature::storage
