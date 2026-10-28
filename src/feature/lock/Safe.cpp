// The safe: owner / key lock, a 27-slot storage entity opened with the vanilla container screen,
// item data that keeps owner, password and contents.

#include "feature/lock/Lock.h"

#include <map>
#include <set>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"
#include "mc/world/level/block/components/BlockRedstoneComponent.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::lock {

namespace {

constexpr int kSlots = 27;

std::string safeKey(core::GlobalPos const& pos) { return "cm:lock:safe:" + pos.key(); }

/// Players (UUID strings) that have a safe open, per safe position.
std::map<core::GlobalPos, std::set<std::string>> gOpeners;

std::string uuidOf(Player const& p) { return p.getUuid().asString(); }

Player* playerByUuid(std::string const& uuid) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->getPlayer(mce::UUID::fromString(uuid));
}

void runCommand(int dim, std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return;
    ServerCommandOrigin origin("complementaries-lock", level->asServer(), CommandPermissionLevel::Owner, DimensionType(dim));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
}

Actor* findStorageNear(BlockSource& region, BlockPos const& bp) {
    AABB box{Vec3{static_cast<float>(bp.x), static_cast<float>(bp.y), static_cast<float>(bp.z)},
             Vec3{bp.x + 1.0f, bp.y + 1.0f, bp.z + 1.0f}};
    for (auto* a : region.getEntities(box, 0.25f)) {
        if (a && a->getTypeName() == kSafeStorageId) return a;
    }
    return nullptr;
}

Actor* storageOf(core::GlobalPos const& pos, SafeData const& data) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    if (data.storage != 0) {
        if (auto* a = level->fetchEntity(ActorUniqueID{data.storage}, false)) {
            if (a->getTypeName() == kSafeStorageId) return a;
        }
    }
    auto* region = regionOf(pos.dim);
    return region ? findStorageNear(*region, game::blockPos(pos)) : nullptr;
}

ContainerComponent* containerOf(Actor& a) {
    auto c = a.getEntityContext().tryGetComponent<ContainerComponent>();
    return c ? c.as_ptr() : nullptr;
}

/// The storage entity of the safe at `pos`, spawned when missing.
Actor* ensureStorage(core::GlobalPos const& pos, SafeData& data) {
    if (auto* a = storageOf(pos, data)) {
        if (a->getOrCreateUniqueID().rawID != data.storage) {
            data.storage = a->getOrCreateUniqueID().rawID;
            saveSafeData(pos, data);
        }
        return a;
    }
    auto* region = regionOf(pos.dim);
    if (!region) return nullptr;
    Actor* a   = nullptr;
    auto   tag = CompoundTag::fromSnbt(
        "{\"identifier\":\"" + std::string(kSafeStorageId) + "\",\"Pos\":[" + std::to_string(pos.x + 0.5f) + "f,"
        + std::to_string(pos.y + 0.25f) + "f," + std::to_string(pos.z + 0.5f) + "f]}"
    );
    if (tag) {
        if (auto spawned = region->spawnActor(*tag)) a = spawned.as_ptr();
    }
    if (!a) {
        runCommand(
            pos.dim,
            "summon " + std::string(kSafeStorageId) + " " + std::to_string(pos.x) + ".5 " + std::to_string(pos.y) + ".25 "
                + std::to_string(pos.z) + ".5"
        );
        a = findStorageNear(*region, game::blockPos(pos));
    }
    if (!a) {
        log().error("lock: could not create the storage of the safe at {}", pos.key());
        return nullptr;
    }
    data.storage = a->getOrCreateUniqueID().rawID;
    saveSafeData(pos, data);
    return a;
}

Vec3 frontSoundPos(core::GlobalPos const& pos, int facing) {
    auto [dx, dz] = game::facingOffset(facing);
    return Vec3{pos.x + 0.5f + dx / 2.0f, pos.y + 0.5f, pos.z + 0.5f + dz / 2.0f};
}

void setSafeOpenState(core::GlobalPos const& pos, bool open) {
    auto* region = regionOf(pos.dim);
    if (!region) return;
    auto        bp = game::blockPos(pos);
    auto const& b  = region->getBlock(bp);
    if (kindOf(b) != Kind::Safe || flag(b, kOpen) == open) return;
    place(*region, bp, with(b, {{kOpen, open}}));
    playSound(
        pos.dim,
        frontSoundPos(pos, state(b, kFacing)),
        open ? "complementaries.lock.iron_trapdoor.open" : "complementaries.lock.iron_trapdoor.close",
        0.5f,
        randomFloat() * 0.1f + 0.65f
    );
}

bool isRedstoneConductor(Block const& b) {
    auto const* rc = static_cast<BlockRedstoneComponent const*>(b.mDirectData->mBlockRedstoneComponent);
    if (rc) return rc->mRedstoneConductor;
    return b.mCachedComponentData->mIsSolid;
}

bool isOwner(SafeData const& d, Player const& p) { return d.owner && *d.owner == uuidOf(p); }
bool isNotOwner(SafeData const& d, Player const& p) { return d.owner && *d.owner != uuidOf(p); }

std::string itemSnbt(ItemStack const& item) {
    auto ctx = SaveContextFactory::createCloneSaveContext();
    auto tag = item.save(*ctx);
    return tag ? tag->toSnbt(SnbtFormat::Minimize, 0) : std::string{};
}

ItemStack itemFromSnbt(std::string const& snbt) {
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) return ItemStack{};
    return ItemStack::fromTag(*tag);
}

bool disallowedInSafe(ItemStack const& item, SafeData const& data) {
    if (item.isNull()) return false;
    auto id = item.getTypeName();
    if (id == kSafeId || id.ends_with("shulker_box")) return true;
    return data.password && keyStatus(item, data.password) == KeyStatus::Correct;
}

nlohmann::json toJson(SafeData const& d) {
    nlohmann::json j = nlohmann::json::object();
    if (d.owner) j["owner"] = *d.owner;
    if (d.ownerName) j["ownerName"] = *d.ownerName;
    if (d.password) j["password"] = *d.password;
    if (d.customName) j["name"] = *d.customName;
    if (d.storage) j["storage"] = d.storage;
    return j;
}

SafeData fromJson(nlohmann::json const& j) {
    SafeData d;
    if (!j.is_object()) return d;
    auto str = [&](char const* k) -> std::optional<std::string> {
        if (j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
        return std::nullopt;
    };
    d.owner      = str("owner");
    d.ownerName  = str("ownerName");
    d.password   = str("password");
    d.customName = str("name");
    if (j.contains("storage") && j["storage"].is_number_integer()) d.storage = j["storage"].get<long long>();
    return d;
}

void openSafe(Player& player, core::GlobalPos const& pos, SafeData data) {
    auto* actor = ensureStorage(pos, data);
    if (!actor) return;
    auto* cc = containerOf(*actor);
    if (!cc) {
        log().error("lock: the storage of the safe at {} has no container", pos.key());
        return;
    }
    // the screen title (Java getDisplayName), in the player's language
    std::string base = data.customName ? *data.customName : trFor(player, "gui.supplementaries.safe", "Safe");
    std::string title = base;
    if (config().simpleSafes) {
        if (data.ownerName && (!data.password || data.password->size() <= kMaxShownPassword)) {
            title = trFor(player, "gui.supplementaries.safe.name", "{0}'s {1}", *data.ownerName, base);
        }
    } else if (data.password && data.password->size() <= kMaxShownPassword) {
        title = trFor(player, "gui.supplementaries.safe.password", "{1} key: {0}", *data.password, base);
    }
    cc->setCustomName(title);
    if (!cc->openContainer(*actor, player)) {
        log().warn("lock: the safe at {} did not open for {}", pos.key(), player.getRealName());
        return;
    }
    gOpeners[pos].insert(uuidOf(player));
    setSafeOpenState(pos, true);
    log().info("lock: {} opened the safe at {}", player.getRealName(), pos.key());
}

} // namespace

// ---- data ------------------------------------------------------------------------------------------

SafeData safeData(core::GlobalPos const& pos) {
    auto v = game::db::load(safeKey(pos));
    return v ? fromJson(*v) : SafeData{};
}

void saveSafeData(core::GlobalPos const& pos, SafeData const& data) { game::db::save(safeKey(pos), toJson(data)); }

bool safeLocked(SafeData const& data) { return config().simpleSafes ? data.owner.has_value() : data.password.has_value(); }

std::string safeTitle(SafeData const& data) {
    std::string base = data.customName ? *data.customName : tr("gui.supplementaries.safe", "Safe");
    if (config().simpleSafes) {
        if (data.ownerName) return tr("gui.supplementaries.safe.name", "{0}'s {1}", *data.ownerName, base);
    } else if (data.password && data.password->size() <= kMaxShownPassword) {
        return tr("gui.supplementaries.safe.password", "{1} key: {0}", *data.password, base);
    }
    return base;
}

bool safeCanOpen(Player& player, core::GlobalPos const& pos, bool feedback) {
    if (player.isCreative()) return true;
    auto data = safeData(pos);
    if (config().simpleSafes) {
        if (isNotOwner(data, player)) {
            if (feedback) {
                actionBar(player, trFor(player, "message.supplementaries.safe.owner", "This safe is owned by {}", data.ownerName.value_or("?")));
            }
            return false;
        }
        return true;
    }
    auto status = inventoryKeyStatus(player, data.password);
    if (feedback) sendKeyMessage(player, status, "safe");
    return status == KeyStatus::Correct;
}

// ---- use -------------------------------------------------------------------------------------------

bool useSafe(Player& player, ItemStack const& held, Click const& click) {
    if (player.isSpectator()) return true;
    auto const& pos  = click.pos;
    auto        data = safeData(pos);
    auto*       region = regionOf(pos.dim);
    if (!region) return true;
    auto        bp = game::blockPos(pos);
    auto const& b  = region->getBlock(bp);

    bool cleared = false;
    if (config().simpleSafes) {
        bool keyLike = held.getTypeName() == "minecraft:tripwire_hook" || keyPassword(held).has_value();
        if (keyLike && (isOwner(data, player) || (isNotOwner(data, player) && player.isCreative()))) cleared = true;
    } else if (click.sneaking && (player.isCreative() || keyStatus(held, data.password) == KeyStatus::Correct)) {
        cleared = true;
    }
    if (cleared) {
        data.owner.reset();
        data.ownerName.reset();
        data.password.reset();
        saveSafeData(pos, data);
        actionBar(player, trFor(player, "message.supplementaries.safe.cleared", "Cleared ownership"));
        playSound(pos.dim, bp, "complementaries.lock.iron_trapdoor.open", 0.5f, 1.5f);
        log().info("lock: {} cleared the safe at {}", player.getRealName(), pos.key());
        return true;
    }

    // a solid block in front of the door keeps it shut
    auto [dx, dz] = game::facingOffset(state(b, kFacing));
    if (isRedstoneConductor(region->getBlock(BlockPos{bp.x + dx, bp.y, bp.z + dz}))) return true;

    if (config().simpleSafes) {
        if (!data.owner) {
            data.owner     = uuidOf(player);
            data.ownerName = player.getRealName();
            saveSafeData(pos, data);
        }
        if (*data.owner != uuidOf(player)) {
            actionBar(player, trFor(player, "message.supplementaries.safe.owner", "This safe is owned by {}", data.ownerName.value_or("?")));
            if (!player.isCreative()) return true;
        }
    } else {
        if (!data.password) {
            if (auto newKey = keyPassword(held)) {
                data.password = newKey;
                saveSafeData(pos, data);
                if (newKey->size() <= kMaxShownPassword) {
                    actionBar(player, trFor(player, "message.supplementaries.safe.assigned_key", "Assigned to '{}' key", *newKey));
                } else {
                    actionBar(player, trFor(player, "message.supplementaries.safe.assigned_key_generic", "Assigned to key"));
                }
                playSound(pos.dim, bp, "complementaries.lock.iron_trapdoor.open", 0.5f, 1.5f);
                log().info("lock: {} assigned key '{}' to the safe at {}", player.getRealName(), *newKey, pos.key());
                return true;
            }
        } else if (!safeCanOpen(player, pos, true) && !player.isCreative()) {
            return true;
        }
    }
    auto uuid = uuidOf(player);
    defer([uuid, pos] {
        auto* p = playerByUuid(uuid);
        if (!p) return;
        auto* region = regionOf(pos.dim);
        if (!region || kindOf(region->getBlock(game::blockPos(pos))) != Kind::Safe) return;
        openSafe(*p, pos, safeData(pos));
    });
    return true;
}

bool safeOpenBy(Player& player, core::GlobalPos const& pos) {
    auto it = gOpeners.find(pos);
    return it != gOpeners.end() && it->second.contains(uuidOf(player));
}

// ---- storage access --------------------------------------------------------------------------------

int safeItemCount(core::GlobalPos const& pos) {
    auto  data  = safeData(pos);
    auto* actor = storageOf(pos, data);
    auto* cc    = actor ? containerOf(*actor) : nullptr;
    if (!cc) return -1;
    int n = 0;
    for (int i = 0; i < cc->mContainer->getContainerSize(); ++i) {
        if (!cc->mContainer->getItem(i).isNull()) ++n;
    }
    return n;
}

bool safeSetItem(core::GlobalPos const& pos, int slot, ItemStack const& item) {
    auto  data  = safeData(pos);
    auto* actor = ensureStorage(pos, data);
    auto* cc    = actor ? containerOf(*actor) : nullptr;
    if (!cc || slot < 0 || slot >= cc->mContainer->getContainerSize()) return false;
    cc->mContainer->setItem(slot, item);
    return true;
}

// ---- item data -------------------------------------------------------------------------------------

SafeData safeItemData(ItemStack const& item) {
    SafeData d;
    if (item.isNull() || !item.mUserData) return d;
    auto const& tag = *item.mUserData;
    if (tag.contains("cm_safe", Tag::Type::String)) {
        auto j = nlohmann::json::parse(std::string(tag.at("cm_safe").get<StringTag>()), nullptr, false);
        d      = fromJson(j);
        d.storage = 0;
    }
    auto name = item.getCustomName();
    if (!name.empty()) d.customName = name;
    return d;
}

namespace {
std::vector<std::pair<int, std::string>> itemListOf(ItemStack const& item) {
    std::vector<std::pair<int, std::string>> out;
    if (item.isNull() || !item.mUserData) return out;
    auto const& tag = *item.mUserData;
    if (!tag.contains("cm_safe", Tag::Type::String)) return out;
    auto j = nlohmann::json::parse(std::string(tag.at("cm_safe").get<StringTag>()), nullptr, false);
    if (!j.is_object() || !j.contains("items") || !j["items"].is_array()) return out;
    for (auto const& e : j["items"]) {
        if (e.is_object() && e.contains("slot") && e.contains("item") && e["item"].is_string()) {
            out.emplace_back(e["slot"].get<int>(), e["item"].get<std::string>());
        }
    }
    return out;
}

ItemStack buildSafeItem(SafeData const& data, std::vector<ItemStack> const& items) {
    ItemStack stack = makeItem(kSafeId);
    if (stack.isNull()) return stack;
    nlohmann::json j       = toJson(data);
    j.erase("storage");
    j.erase("name");
    auto list              = nlohmann::json::array();
    int  count             = 0;
    std::vector<std::string> lore;
    if (config().simpleSafes) {
        if (data.ownerName) lore.push_back("§7" + tr("message.supplementaries.safe.owner", "This safe is owned by {}", *data.ownerName));
    } else {
        lore.push_back("§7" + (data.password ? tr("message.supplementaries.safe.bound", "Locked") : tr("message.supplementaries.safe.unbound", "Unbound")));
    }
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        auto const& it = items[i];
        if (it.isNull()) continue;
        list.push_back({{"slot", i}, {"item", itemSnbt(it)}});
        if (count < 5) lore.push_back("§f" + tr("cm.lock.safe.item_count", "{} x{}", it.getName(), static_cast<int>(it.mCount)));
        ++count;
    }
    if (count > 5) lore.push_back("§f§o" + tr("cm.lock.safe.more", "and {} more...", count - 5));
    bool hasData = data.owner || data.password || count > 0;
    if (!hasData && !data.customName) return stack;
    auto tag = std::make_unique<CompoundTag>();
    if (!list.empty()) j["items"] = list;
    (*tag)["cm_safe"] = j.dump();
    if (data.customName) {
        CompoundTag display;
        display["Name"] = *data.customName;
        (*tag)["display"] = std::move(display);
    }
    stack.setUserData(std::move(tag));
    if (!lore.empty()) stack.setCustomLore(lore);
    return stack;
}

std::vector<ItemStack> storageItems(Actor* actor) {
    std::vector<ItemStack> items;
    auto* cc = actor ? containerOf(*actor) : nullptr;
    if (!cc) return items;
    for (int i = 0; i < cc->mContainer->getContainerSize(); ++i) items.push_back(cc->mContainer->getItem(i));
    return items;
}
} // namespace

ItemStack safeItem(core::GlobalPos const& pos) {
    auto data = safeData(pos);
    return buildSafeItem(data, storageItems(storageOf(pos, data)));
}

// ---- placement / removal ---------------------------------------------------------------------------

void placeSafe(Player& player, core::GlobalPos const& pos, ItemStack const& item) {
    auto* region = regionOf(pos.dim);
    if (!region) return;
    auto        bp = game::blockPos(pos);
    auto const& b  = region->getBlock(bp);
    if (kindOf(b) != Kind::Safe) return;
    place(*region, bp, with(b, {{kFacing, game::opposite(game::playerFacing(player))}, {kOpen, 0}}));
    SafeData data = safeItemData(item);
    if (!data.owner) {
        data.owner     = uuidOf(player);
        data.ownerName = player.getRealName();
    }
    data.storage = 0;
    saveSafeData(pos, data);
    auto* actor = ensureStorage(pos, data);
    auto* cc    = actor ? containerOf(*actor) : nullptr;
    if (cc) {
        for (auto const& [slot, snbt] : itemListOf(item)) {
            if (slot >= 0 && slot < cc->mContainer->getContainerSize()) cc->mContainer->setItem(slot, itemFromSnbt(snbt));
        }
    }
}

void removeSafe(core::GlobalPos const& pos, bool byCreative) {
    defer([pos, byCreative] {
        auto* region = regionOf(pos.dim);
        if (!region) return;
        if (kindOf(region->getBlock(game::blockPos(pos))) == Kind::Safe) return; // replaced by another safe state
        auto  data  = safeData(pos);
        auto* actor = storageOf(pos, data);
        auto  items = storageItems(actor);
        bool  empty = std::all_of(items.begin(), items.end(), [](ItemStack const& i) { return i.isNull(); });
        if (!byCreative || !empty) {
            dropItem(*region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, buildSafeItem(data, items));
        }
        if (actor) {
            if (auto* cc = containerOf(*actor)) {
                for (int i = 0; i < cc->mContainer->getContainerSize(); ++i) cc->mContainer->setItem(i, ItemStack{});
            }
            actor->despawn();
        }
        game::db::erase(safeKey(pos));
        gOpeners.erase(pos);
    });
}

// ---- tick --------------------------------------------------------------------------------------------

void tickSafes() {
    if (gOpeners.empty()) return;
    std::vector<core::GlobalPos> closed;
    for (auto& [pos, uuids] : gOpeners) {
        auto data   = safeData(pos);
        auto* actor = storageOf(pos, data);
        long long id = actor ? actor->getOrCreateUniqueID().rawID : 0;
        Player*   anyOpener = nullptr;
        for (auto it = uuids.begin(); it != uuids.end();) {
            auto* p     = playerByUuid(*it);
            bool  still = false;
            if (p && id != 0) {
                std::shared_ptr<ContainerManagerModel> const& sp = p->mContainerManager;
                auto* model = sp.get();
                if (model) {
                    auto const& owner = *model->mScreenContext->mOwner;
                    if (auto const* aid = std::get_if<ActorUniqueID>(&owner)) still = aid->rawID == id;
                }
            }
            if (still) {
                anyOpener = p;
                ++it;
            } else {
                it = uuids.erase(it);
            }
        }
        // shulker-box rules: no shulker boxes, no safes and not the safe's own key
        if (auto* cc = actor ? containerOf(*actor) : nullptr) {
            for (int i = 0; i < cc->mContainer->getContainerSize(); ++i) {
                auto const& item = cc->mContainer->getItem(i);
                if (!disallowedInSafe(item, data)) continue;
                ItemStack copy = item;
                cc->mContainer->setItem(i, ItemStack{});
                if (anyOpener) giveItem(*anyOpener, copy);
                else if (auto* region = regionOf(pos.dim)) dropItem(*region, Vec3{pos.x + 0.5f, pos.y + 1.1f, pos.z + 0.5f}, copy);
            }
        }
        if (uuids.empty()) closed.push_back(pos);
    }
    for (auto const& pos : closed) {
        gOpeners.erase(pos);
        setSafeOpenState(pos, false);
    }
}

void clearSafes() { gOpeners.clear(); }

} // namespace cm::feature::lock
