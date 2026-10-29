#include "feature/storage/View.h"

#include <algorithm>
#include <map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/packet/BlockActorDataPacket.h"
#include "mc/network/packet/ContainerClosePacket.h"
#include "mc/network/packet/ContainerOpenPacket.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"

#include "feature/storage/Common.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::storage::view {

namespace {

using ContainerType = SharedTypes::Legacy::ContainerType;

/// Ticks between spawning the helper and opening its screen. The client ignores a container open
/// for an entity it does not know yet, and the new entity only reaches it with the next actor
/// replication, after the open packet of the same tick.
constexpr int kOpenDelay = 4;

struct Viewer {
    long long                    player = 0;
    ContainerManagerModel const* model  = nullptr;
    int                          wait   = 0; ///< ticks until the screen is opened (0 = opened)
    int                          age    = 0; ///< ticks since opening
    std::optional<BlockPos>      fake;       ///< the client-only dispenser showing the screen
};

struct Session {
    Options                  opts;
    long long                actor = 0;
    int                      grid  = 9; ///< slots of the vanilla grid
    std::vector<Viewer>      viewers;
    std::vector<std::string> snapshot; ///< SNBT per grid slot, as last written
    std::vector<ItemStack>   overflow; ///< stacks beyond the configured size (config shrank)
    int                      age = 0;  ///< ticks since the helper was spawned
};

std::map<std::string, Session> gSessions;
long long                      gSweepTick = 0;

/// The helper's inventory for `size` slots. Entity inventories only open as chest or hopper screens,
/// so the 3x3 grid is a 9-slot chest inventory shown through a dispenser screen (see `gRewrite`).
std::pair<ContainerType, int> gridFor(int size) {
    if (size <= 5) return {ContainerType::Hopper, 5};
    if (size <= 9) return {ContainerType::Container, 9};
    return {ContainerType::Container, 27};
}

/// Set while a 3x3 view opens: the container open packet the server sends to `player` is turned
/// into one of a dispenser at `pos` that only this client sees. The server keeps the screen bound
/// to the helper's inventory; dispensers and entity chests use the same container (LevelEntity), so
/// the client's item moves resolve against the helper.
struct Rewrite {
    Player const* player = nullptr;
    BlockPos      pos;
    std::string   title;
    bool          done = false;
};
std::optional<Rewrite> gRewrite;

void sendBlock(Player const& player, BlockPos const& pos, Block const& block) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::UpdateBlock);
    if (!pkt) return;
    auto& p        = static_cast<UpdateBlockPacket&>(*pkt);
    p.mPos         = pos;
    p.mLayer       = 0;
    p.mUpdateFlags = 3;
    p.mRuntimeId   = block.mNetworkId;
    p.sendTo(player);
}

/// A position near `player` for the client-only dispenser: a solid block under the one the player
/// stands on (out of sight, and no new collision), else the block above the head room. Never a
/// block with a block entity (its client data would be lost on restore) nor the player's own space.
std::optional<BlockPos> fakePosFor(Player& player) {
    auto&                   region = player.getDimensionBlockSource();
    auto                    feet   = game::blockPosOf(player.getFeetPos());
    int                     minY   = region.getMinHeight();
    int                     maxY   = region.getMaxHeight();
    std::optional<BlockPos> fallback;
    for (int dy : {-2, -3, -4, 3}) {
        BlockPos p{feet.x, feet.y + dy, feet.z};
        if (p.y < minY || p.y >= maxY || region.getBlockEntity(p)) continue;
        if (dy < 0 && region.getBlock(p).isAir()) {
            if (!fallback) fallback = p;
            continue;
        }
        return p;
    }
    return fallback;
}

/// Puts the real block back on the client that saw a fake dispenser.
void restoreFake(Viewer& v) {
    if (!v.fake) return;
    if (auto* p = playerById(v.player)) sendBlock(*p, *v.fake, p->getDimensionBlockSource().getBlock(*v.fake));
    v.fake.reset();
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    ViewOpenPacketHook,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$sendNetworkPacket,
    void,
    Packet& packet
) {
    if (gRewrite && !gRewrite->done && gRewrite->player == this
        && packet.getId() == MinecraftPacketIds::ContainerOpen) {
        auto const* dispenser = game::blockWith("minecraft:dispenser", {{"facing_direction", 1}});
        if (dispenser) {
            gRewrite->done = true;
            sendBlock(*this, gRewrite->pos, *dispenser);
            auto tag = CompoundTag::fromSnbt(
                "{\"id\":\"Dispenser\",\"x\":" + std::to_string(gRewrite->pos.x) + ",\"y\":"
                + std::to_string(gRewrite->pos.y) + ",\"z\":" + std::to_string(gRewrite->pos.z) + "}"
            );
            if (tag) {
                (*tag)["CustomName"] = gRewrite->title;
                BlockActorDataPacketPayload payload;
                payload.mPos  = gRewrite->pos;
                payload.mData = std::move(*tag);
                BlockActorDataPacket(std::move(payload)).sendTo(*this);
            }
            auto& open           = static_cast<ContainerOpenPacket&>(packet);
            open.mType           = ContainerType::Dispenser;
            open.mPos            = gRewrite->pos;
            open.mEntityUniqueID = ActorUniqueID{-1};
        }
    }
    origin(packet);
}

namespace {

/// Opens the helper's screen for `player`; true when the server opened it.
bool openFor(Session& s, Actor& actor, ContainerComponent& cc, Player& player, Viewer& v) {
    std::optional<BlockPos> fake;
    if (s.grid == 9) fake = fakePosFor(player);
    if (fake) gRewrite = Rewrite{&player, *fake, s.opts.title};
    bool ok = cc.openContainer(actor, player);
    if (fake && gRewrite && gRewrite->done) v.fake = fake;
    gRewrite.reset();
    return ok;
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
    Player* owner = s.viewers.size() == 1 ? playerById(s.viewers.front().player) : nullptr;
    sync(s, owner);
    for (auto& v : s.viewers) {
        if (auto* p = playerById(v.player); p && closeScreens) sendClose(*p, v.model);
        restoreFake(v);
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
            for (auto& v : it->second.viewers) {
                if (v.player != playerKey(player)) continue;
                // Opening it again while the server has the screen open means the client never
                // showed it (the click could not happen otherwise): open it once more. Repeats
                // within a few ticks are the same click delivered twice.
                if (v.wait == 0 && v.age > kOpenDelay * 2) {
                    restoreFake(v);
                    v.model = nullptr;
                    v.wait  = 1;
                    v.age   = 0;
                }
                return true;
            }
            int wait = std::max(1, kOpenDelay - it->second.age);
            it->second.viewers.push_back({playerKey(player), nullptr, wait, 0});
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
    s.viewers.push_back({playerKey(player), nullptr, kOpenDelay, 0});
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
        ++s.age;
        for (auto it = s.viewers.begin(); it != s.viewers.end();) {
            auto* p = playerById(it->player);
            if (p && it->wait > 0 && --it->wait == 0) {
                auto* actor = actorOf(s.actor);
                auto* cc    = actor ? containerOf(*actor) : nullptr;
                if (!cc || !openFor(s, *actor, *cc, *p, *it)) p = nullptr;
            }
            if (p && it->wait > 0) {
                ++it;
                continue;
            }
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
                restoreFake(*it);
                it = s.viewers.erase(it);
            } else {
                ++it;
            }
        }
        // with several viewers nobody knows who put a refused item in: it drops at the container
        sync(s, s.viewers.size() == 1 ? playerById(s.viewers.front().player) : nullptr);
        if (s.viewers.empty()) done.push_back(uid);
    }
    for (auto const& uid : done) finish(uid, false);

    // Views left over from a crash or a world closed with a screen open (saved with the world):
    // the content record is authoritative, so they are removed without dropping their copy.
    if (++gSweepTick % 100 == 0) {
        auto level = ll::service::getLevel();
        if (!level) return;
        for (auto* a : level->getRuntimeActorList()) {
            if (!a || a->getTypeName() != kViewEntity) continue;
            long long id    = a->getOrCreateUniqueID().rawID;
            bool      owned = false;
            for (auto const& [uid, s] : gSessions) owned = owned || s.actor == id;
            if (owned) continue;
            if (auto* cc = containerOf(*a)) cc->mContainer->removeAllItems();
            a->despawn();
        }
    }
}

void install() { ViewOpenPacketHook::hook(); }

void uninstall() { ViewOpenPacketHook::unhook(); }

void reset() {
    std::vector<std::string> uids;
    for (auto const& [uid, s] : gSessions) uids.push_back(uid);
    for (auto const& uid : uids) finish(uid, true); // no-op on entities of a world that is gone
    gSessions.clear();
}

} // namespace cm::feature::storage::view
