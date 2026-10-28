// U1 jar: module wiring (events, hooks, ticking, world lifecycle).

#include <charconv>
#include <map>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerAttackEvent.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/jar/Jar.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("jar", cm::feature::jar::JarConfig);

namespace cm::feature::jar {

namespace {

std::vector<std::function<void()>> gNext;    ///< work for the next server tick
std::set<core::GlobalPos>          gNoDrop;  ///< jars broken in creative / by us
std::map<int64, int>               gLastUse; ///< player -> tick of the last handled click
int                                gTick = 0;

struct PendingPlace {
    std::optional<Contents> jar;
    int                     face = 2;
    int                     tick = 0;
};
std::unordered_map<int64, PendingPlace> gPending;

BlockType const* gJarType    = nullptr;
BlockType const* gFaucetType = nullptr;

void cacheTypes() {
    auto type = [](std::string_view id) -> BlockType const* {
        auto const* b = game::blockWith(id);
        return b ? &b->getBlockType() : nullptr;
    };
    gJarType    = type(kJarId);
    gFaucetType = type(kFaucetId);
}

void later(std::function<void()> fn) { gNext.push_back(std::move(fn)); }

Player* playerById(int64 id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->getPlayer(ActorUniqueID(id));
}

bool firstClickThisTick(Player const& player) {
    auto id = player.getOrCreateUniqueID().rawID;
    auto it = gLastUse.find(id);
    if (it != gLastUse.end() && it->second == gTick) return false;
    gLastUse[id] = gTick;
    return true;
}

/// Bedrock face id (0 down, 1 up, 2 N, 3 S, 4 W, 5 E) -> Java 2D facing (0 S, 1 W, 2 N, 3 E).
int faceToFacing(int face) {
    switch (face) {
    case 3:
        return 0;
    case 4:
        return 1;
    case 5:
        return 3;
    default:
        return 2;
    }
}

bool holdsJar(Player const& player) { return isItem(player.getSelectedItem(), kJarId); }

// ---- events -------------------------------------------------------------------------------------

void onInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& region = player.getDimensionBlockSource();
    auto  bp     = ev.blockPos();
    auto  pos    = game::globalPos(region, bp);
    auto const& block = region.getBlock(bp);
    auto const& conf  = cfg();
    auto        uid   = player.getOrCreateUniqueID().rawID;

    if (conf.enabled && isBlock(block, kJarId) && jarWouldHandle(player, pos)) {
        ev.cancel();
        if (!firstClickThisTick(player)) return;
        later([uid, pos] {
            if (auto* p = playerById(uid)) useJar(*p, pos);
        });
        return;
    }
    if (conf.faucetEnabled && isBlock(block, kFaucetId)) {
        if (player.isSneaking() && !player.getSelectedItem().isNull()) return; // sneak-place against it
        ev.cancel();
        if (!firstClickThisTick(player)) return;
        later([pos] { toggleFaucet(pos); });
        return;
    }
    if (conf.enabled && conf.jarCapture && holdsJar(player) && !player.isSneaking()) {
        auto c = itemContents(player.getSelectedItem());
        if (c && c->mob) {
            ev.cancel();
            if (!firstClickThisTick(player)) return;
            Vec3 click = ev.clickPos();
            later([uid, bp, click] {
                if (auto* p = playerById(uid)) releaseFromItem(*p, bp, click);
            });
        }
    }
}

void onPlacing(ll::event::PlayerPlacingBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& held = player.getSelectedItem();
    auto        uid  = player.getOrCreateUniqueID().rawID;
    if (isItem(held, kJarId)) {
        if (cfg().jarCapture && !player.isSneaking()) {
            ev.cancel(); // "[Sneak] to place": a plain click captures / releases
            return;
        }
        gPending[uid] = PendingPlace{itemContents(held), ev.face(), gTick};
    } else if (isItem(held, kFaucetId)) {
        gPending[uid] = PendingPlace{std::nullopt, ev.face(), gTick};
    }
}

void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto&       region  = player.getDimensionBlockSource();
    auto        bp      = ev.pos();
    auto        pos     = game::globalPos(region, bp);
    auto const& placed  = ev.placedBlock();
    auto        uid     = player.getOrCreateUniqueID().rawID;
    PendingPlace pending;
    if (auto it = gPending.find(uid); it != gPending.end()) {
        pending = std::move(it->second);
        gPending.erase(it);
    }
    if (isBlock(placed, kJarId)) {
        setJar(pos, pending.jar.value_or(Contents{}));
        log().info("jar: {} placed a jar at {}", player.getRealName(), pos.key());
    } else if (isBlock(placed, kFaucetId)) {
        setStates(region, bp, {{kStateFacing, faceToFacing(pending.face)}});
        faucetPlaced(pos);
        log().info("faucet: {} placed a faucet at {}", player.getRealName(), pos.key());
    }
}

void onDestroy(ll::event::PlayerDestroyBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& region = player.getDimensionBlockSource();
    if (player.isCreative() && isBlock(region.getBlock(ev.pos()), kJarId)) gNoDrop.insert(game::globalPos(region, ev.pos()));
}

void onAttack(ll::event::PlayerAttackEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !holdsJar(player)) return;
    if (useJarItemOnMob(player, ev.target())) ev.cancel();
}

void onUseItem(ll::event::PlayerUseItemEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !cfg().drinkFromJarItem || !isItem(ev.item(), kJarId)) return;
    auto c = itemContents(ev.item());
    if (!c || c->fluid.empty()) return;
    auto const* def = fluidDef(c->fluid.fluid);
    if (!def || def->food == FoodKind::None) return;
    ev.cancel();
    auto uid = player.getOrCreateUniqueID().rawID;
    later([uid] {
        auto* p = playerById(uid);
        if (!p) return;
        auto held = p->getSelectedItem();
        auto c    = itemContents(held);
        if (!c) return;
        if (!drinkFluid(*p, c->fluid)) return;
        setItemContents(held, *c, p);
        p->setSelectedItem(held);
        p->refreshInventory();
    });
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    auto const* prevType = &ev.previousBlock().getBlockType();
    auto const* newType  = &ev.newBlock().getBlockType();
    auto const& bp       = ev.pos();
    if (prevType != newType) {
        if (gJarType && prevType == gJarType) {
            auto pos = game::globalPos(region, bp);
            bool drop = !gNoDrop.erase(pos);
            later([pos, drop] {
                auto c = jarAt(pos);
                if (drop) dropJar(pos, c);
                forgetJar(pos);
            });
        }
        if (gFaucetType && newType == gFaucetType) {
            auto pos = game::globalPos(region, bp);
            later([pos] {
                if (!faucetKnown(pos)) faucetPlaced(pos);
            });
        }
        if (gFaucetType && prevType == gFaucetType) faucetRemoved(game::globalPos(region, bp));
    }
    static constexpr int d[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    int dim = static_cast<int>(region.getDimensionId());
    for (auto const& o : d) {
        core::GlobalPos n{dim, bp.x + o[0], bp.y + o[1], bp.z + o[2]};
        if (faucetKnown(n)) faucetNeighborChanged(n);
    }
}

void onTick() {
    ++gTick;
    auto work = std::move(gNext);
    gNext.clear();
    for (auto& fn : work) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("jar: deferred work failed: {}", e.what());
        }
    }
    tickFaucets();
    if (gTick % 40 == 0) reconcileDisplays();
    if (gTick % 200 == 0) {
        std::erase_if(gPending, [](auto const& kv) { return gTick - kv.second.tick > 100; });
        std::erase_if(gLastUse, [](auto const& kv) { return gTick - kv.second > 100; });
    }
}

void onWorldReady() {
    cacheTypes();
    loadFaucets();
    // index jars holding a mob so their display comes back once their chunk loads
    game::db::forEachKey("cm:jar:j:", [](std::string_view suffix) {
        core::GlobalPos p;
        int*            out[4] = {&p.dim, &p.x, &p.y, &p.z};
        auto const*     it     = suffix.data();
        auto const*     end    = suffix.data() + suffix.size();
        for (int i = 0; i < 4; ++i) {
            auto r = std::from_chars(it, end, *out[i]);
            if (r.ec != std::errc{}) return;
            it = r.ptr;
            if (i < 3) {
                if (it == end || *it != ':') return;
                ++it;
            }
        }
        if (it != end) return;
        auto c = jarAt(p);
        if (c.mob && c.mob->display > 0) wantDisplay(p);
    });
}

void onWorldClosed() {
    clearJarCache();
    clearFaucets();
    clearDisplays();
    gNext.clear();
    gNoDrop.clear();
    gPending.clear();
    gLastUse.clear();
    gJarType = gFaucetType = nullptr;
}

} // namespace

/// Selftests: a jar removed by the test cleanup must not drop an item.
void suppressDrop(core::GlobalPos const& pos) { gNoDrop.insert(pos); }

LL_TYPE_INSTANCE_HOOK(
    JarInteractEntityHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$interact,
    bool,
    ::Actor&      entity,
    ::Vec3 const& location,
    ::HandSlot    handSlot
) {
    Player& player = mPlayer;
    if (!player.isClientSide() && isItem(player.getSelectedItem(), kJarId) && useJarItemOnMob(player, entity)) return true;
    return origin(entity, location, handSlot);
}

class JarModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteractBlock));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(onPlacing));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(onDestroy));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerAttackEvent>(onAttack));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>(onUseItem));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); }));
        JarInteractEntityHook::hook();
        if (!mLifecycle) {
            mLifecycle = true;
            game::db::onWorldReady([this] {
                if (mInstalled) onWorldReady();
            });
            game::db::onWorldSave([this] {
                if (mInstalled) saveFaucets();
            });
            game::db::onWorldClosed([this] {
                if (mInstalled) onWorldClosed();
            });
        }
        mInstalled = true;
        if (game::db::available()) onWorldReady();
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        JarInteractEntityHook::unhook();
        if (game::db::available()) saveFaucets();
        mInstalled = false;
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    bool                                mLifecycle = false;
    bool                                mInstalled = false;
};

} // namespace cm::feature::jar

CM_REGISTER_MODULE(cm::feature::jar::JarModule);
