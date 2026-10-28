#include "feature/storage/View.h"

#include <map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/ContainerClosePacket.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "feature/storage/Common.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::storage::view {

namespace {

using ContainerType = SharedTypes::Legacy::ContainerType;

struct Viewer {
    long long                    player = 0;
    ContainerManagerModel const* model  = nullptr;
    int                          age    = 0; ///< ticks since opening
};

struct Session {
    Options                  opts;
    long long                actor = 0;
    int                      grid  = 9; ///< slots of the vanilla grid
    std::vector<Viewer>      viewers;
    std::vector<std::string> snapshot; ///< SNBT per grid slot, as last written
    std::vector<ItemStack>   overflow; ///< stacks beyond the configured size (config shrank)
};

std::map<std::string, Session> gSessions;

std::pair<ContainerType, int> gridFor(int size) {
    if (size <= 5) return {ContainerType::Hopper, 5};
    if (size <= 9) return {ContainerType::Dispenser, 9};
    return {ContainerType::Container, 27};
}

Actor* actorOf(long long id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->fetchEntity(uid, false);
}

ContainerComponent* containerOf(Actor& a) {
    auto c = a.getEntityContext().tryGetComponent<ContainerComponent>();
    return c ? &*c : nullptr;
}

/// Writes changed slots back to the content record; disallowed items bounce back to `owner`.
void sync(Session& s, Player* owner) {
    auto* actor = actorOf(s.actor);
    if (!actor) return;
    auto* cc = containerOf(*actor);
    if (!cc) return;
    auto& c       = *cc->mContainer;
    bool  changed = false;
    for (int i = 0; i < s.grid && i < c.getContainerSize(); ++i) {
        auto const& item = c.getItem(i);
        auto        snbt = stackToSnbt(item);
        if (snbt == s.snapshot[i]) continue;
        bool ok = !isEmpty(item) ? (i < s.opts.size && (!s.opts.allowed || s.opts.allowed(item))) : true;
        if (!ok) {
            ItemStack back = item;
            c.setItem(i, ItemStack());
            if (owner) {
                actionbar(*owner, trFor(*owner, "cm.storage.bounced", "{} can't go in there", stackName(back)));
                giveOrDrop(*owner, back);
            } else {
                spawnItem(actor->getDimensionBlockSource(), back, actor->getPosition());
            }
            snbt = {};
        }
        if (snbt != s.snapshot[i]) {
            s.snapshot[i] = snbt;
            changed       = true;
        }
    }
    if (!changed) return;
    std::vector<ItemStack> items;
    for (int i = 0; i < s.opts.size && i < c.getContainerSize(); ++i) items.push_back(c.getItem(i));
    for (auto const& o : s.overflow) items.push_back(o);
    saveContents(s.opts.uid, items);
    if (s.opts.onChange) s.opts.onChange();
}

void sendClose(Player& p, ContainerManagerModel const* model) {
    if (!model) return;
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerClose);
    if (!pkt) return;
    auto& cp                 = static_cast<ContainerClosePacket&>(*pkt);
    cp.mContainerId          = model->getContainerId();
    cp.mContainerType        = model->getContainerType();
    cp.mServerInitiatedClose = true;
    cp.sendTo(p);
}

void finish(std::string const& uid, bool closeScreens) {
    auto it = gSessions.find(uid);
    if (it == gSessions.end()) return;
    Session s = std::move(it->second);
    gSessions.erase(it);
    Player* owner = s.viewers.empty() ? nullptr : playerById(s.viewers.front().player);
    sync(s, owner);
    if (closeScreens) {
        for (auto const& v : s.viewers) {
            if (auto* p = playerById(v.player)) sendClose(*p, v.model);
        }
    }
    if (auto* a = actorOf(s.actor)) {
        if (auto* cc = containerOf(*a)) cc->mContainer->removeAllItems(); // never drop the copy
        a->despawn();
    }
    if (s.opts.onClosed) s.opts.onClosed();
}

} // namespace

bool open(Player& player, Options opts) {
    auto level = ll::service::getLevel();
    if (!level || opts.uid.empty()) return false;
    if (auto it = gSessions.find(opts.uid); it != gSessions.end()) {
        auto* actor = actorOf(it->second.actor);
        auto* cc    = actor ? containerOf(*actor) : nullptr;
        if (!cc) {
            finish(opts.uid, true);
        } else {
            for (auto const& v : it->second.viewers)
                if (v.player == playerKey(player)) return true;
            if (!cc->openContainer(*actor, player)) return false;
            it->second.viewers.push_back({playerKey(player), nullptr, 0});
            return true;
        }
    }

    auto& region = player.getDimensionBlockSource();
    Vec3  pos    = opts.block ? Vec3{static_cast<float>(opts.block->x) + 0.5f,
                                  static_cast<float>(opts.block->y) + 0.25f,
                                  static_cast<float>(opts.block->z) + 0.5f}
                              : player.getPosition();
    ActorDefinitionIdentifier id{std::string(kViewEntity)};
    Mob* mob = level->getSpawner().spawnMob(region, id, nullptr, pos, false, false, false);
    if (!mob) {
        log().warn("storage: could not spawn the container view entity");
        return false;
    }
    auto* cc = containerOf(*mob);
    if (!cc) {
        mob->despawn();
        log().warn("storage: the container view entity has no inventory");
        return false;
    }
    auto [type, grid] = gridFor(opts.size);
    cc->rebuildContainer(*mob, type, grid, false, 0, false);
    cc->setCustomName(opts.title);
    mob->setNameTag(opts.title);

    Session s;
    s.grid  = grid;
    s.actor = mob->getOrCreateUniqueID().rawID;
    s.snapshot.assign(static_cast<size_t>(grid), std::string());
    auto items = loadContents(opts.uid, opts.size);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (i < opts.size && i < grid) {
            cc->mContainer->setItem(i, items[i]);
            s.snapshot[i] = stackToSnbt(items[i]);
        } else if (!isEmpty(items[i])) {
            s.overflow.push_back(items[i]);
        }
    }
    s.opts = std::move(opts);
    if (!cc->openContainer(*mob, player)) {
        cc->mContainer->removeAllItems();
        mob->despawn();
        return false;
    }
    s.viewers.push_back({playerKey(player), nullptr, 0});
    gSessions.emplace(s.opts.uid, std::move(s));
    return true;
}

bool viewed(std::string const& uid) { return gSessions.contains(uid); }

void close(std::string const& uid) { finish(uid, true); }

void tick() {
    std::vector<std::string> done;
    for (auto& [uid, s] : gSessions) {
        if (!actorOf(s.actor)) {
            done.push_back(uid);
            continue;
        }
        Player* owner = nullptr;
        for (auto it = s.viewers.begin(); it != s.viewers.end();) {
            auto* p   = playerById(it->player);
            auto* cur = p ? p->mContainerManager.get() : nullptr;
            ++it->age;
            bool gone = !p;
            if (!gone && !it->model) {
                if (cur) it->model = cur;
                else gone = it->age > 20; // never opened
            } else if (!gone && cur != it->model) {
                gone = true;
            }
            if (gone) {
                it = s.viewers.erase(it);
            } else {
                if (!owner) owner = p;
                ++it;
            }
        }
        sync(s, owner);
        if (s.viewers.empty()) done.push_back(uid);
    }
    for (auto const& uid : done) finish(uid, false);
}

void reset() { gSessions.clear(); }

} // namespace cm::feature::storage::view
