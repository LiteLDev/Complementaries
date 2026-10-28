// The lock unit's module: event wiring (use, place, break, block changes, ticks) and world lifecycle.

#include "feature/lock/Lock.h"

#include <cmath>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::feature::lock {

namespace {

/// The last block click of each player (used to place doors / trapdoors / safes from it).
struct LastClick {
    Click     click;
    ItemStack item;
    long long tick = -1;
    bool      consumed = false;
};
std::unordered_map<std::string, LastClick> gLastClick;

/// Positions broken by a creative player this tick (no drops for door partners / empty safes).
std::vector<std::pair<core::GlobalPos, long long>> gCreativeBreaks;

bool brokenByCreative(core::GlobalPos const& pos) {
    auto now = detail::currentTick();
    for (auto const& [p, t] : gCreativeBreaks) {
        if (p == pos && now - t <= 1) return true;
    }
    return false;
}

Click makeClick(Player& player, int dim, BlockPos const& pos, int face, Vec3 const& clickPos) {
    Click c;
    c.pos = game::globalPos(dim, pos);
    c.face = face;
    // the click position may be absolute or relative to the block
    float x = clickPos.x, y = clickPos.y, z = clickPos.z;
    bool  relative = x > -0.01f && x < 1.01f && y > -0.01f && y < 1.01f && z > -0.01f && z < 1.01f;
    if (!relative) {
        x -= static_cast<float>(pos.x);
        y -= static_cast<float>(pos.y);
        z -= static_cast<float>(pos.z);
    }
    c.hitX     = x;
    c.hitY     = y;
    c.hitZ     = z;
    c.sneaking = player.isSneaking();
    c.yaw      = player.getRotation().y;
    return c;
}

bool dispatch(Kind k, Player& player, ItemStack const& held, Click const& click) {
    switch (k) {
    case Kind::LockBlock:
        return useLockBlock(player, held, click);
    case Kind::Safe:
        return useSafe(player, held, click);
    case Kind::IronGate:
    case Kind::GoldGate:
        return useGate(player, held, click);
    case Kind::GoldDoor:
    case Kind::NetheriteDoor:
        return useDoor(player, held, click);
    case Kind::GoldTrapdoor:
    case Kind::NetheriteTrapdoor:
        return useTrapdoor(player, held, click);
    default:
        return false;
    }
}

} // namespace

/// Shared by the event handler and the selftests: Java `useItemOn` / `useWithoutItem` order.
/// Returns true when the click is consumed (the vanilla interaction / placement is cancelled).
bool interact(Player& player, ItemStack const& held, Click const& click) {
    auto* region = regionOf(click.pos.dim);
    if (!region) return false;
    auto const& block = region->getBlock(game::blockPos(click.pos));
    auto        k     = kindOf(block);
    if (k == Kind::None || !enabled(k)) return false;
    bool hasItem = !held.isNull() || !player.getOffhandSlot().isNull();
    // secondary use (sneaking with an item) skips the block, except for keys (doesSneakBypassUse)
    if (click.sneaking && hasItem && !keyPassword(held)) return false;
    return dispatch(k, player, held, click);
}

class LockModule {
public:
    static LockModule& get() {
        static LockModule m;
        return m;
    }

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player = ev.self();
            auto  dim    = static_cast<int>(player.getDimensionId());
            auto  click  = makeClick(player, dim, ev.blockPos(), static_cast<int>(ev.face()), ev.clickPos());
            auto  uuid   = player.getUuid().asString();
            auto  now    = detail::currentTick();
            auto& last   = gLastClick[uuid];
            // the same click delivered twice in a tick
            if (last.tick == now && last.click.pos == click.pos) {
                if (last.consumed) ev.cancel();
                return;
            }
            last.click    = click;
            last.item     = ev.item();
            last.tick     = now;
            last.consumed = interact(player, ev.item(), click);
            if (last.consumed) ev.cancel();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
            auto k = kindOf(ev.placedBlock());
            if (k == Kind::None) return;
            auto& player = ev.self();
            auto  dim    = static_cast<int>(player.getDimensionId());
            auto  pos    = game::globalPos(dim, ev.pos());
            auto  uuid   = player.getUuid().asString();
            auto  it     = gLastClick.find(uuid);
            Click click;
            click.pos = pos;
            ItemStack item;
            if (it != gLastClick.end() && detail::currentTick() - it->second.tick <= 1) {
                click = it->second.click;
                item  = it->second.item;
            }
            defer([uuid, pos, click, item] {
                auto level = ll::service::getLevel();
                if (!level) return;
                auto* p = level->getPlayer(mce::UUID::fromString(uuid));
                if (!p) return;
                onPlaced(*p, pos, click, item);
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& ev) {
            auto& player = ev.self();
            auto& region = player.getDimensionBlockSource();
            auto  k      = kindOf(region.getBlock(ev.pos()));
            if (k == Kind::None) return;
            auto pos = game::globalPos(static_cast<int>(player.getDimensionId()), ev.pos());
            if (enabled(k) && !mayBreak(player, pos)) {
                ev.cancel();
                return;
            }
            if (player.isCreative()) gCreativeBreaks.emplace_back(pos, detail::currentTick());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto const& prev = ev.previousBlock();
            auto const& next = ev.newBlock();
            if (&prev.getBlockType() == &next.getBlockType()) return; // a state change
            auto pk = kindOf(prev);
            auto nk = kindOf(next);
            if (pk == Kind::None && nk == Kind::None) return;
            auto pos = game::globalPos(static_cast<int>(ev.blockSource().getDimensionId()), ev.pos());
            if (pk != Kind::None) onRemoved(pos, prev, brokenByCreative(pos));
            if (readsRedstone(nk)) track(pos);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
            detail::runScheduled();
            tickTimers();
            tickRedstone();
            tickSafes();
            auto now = detail::currentTick();
            std::erase_if(gCreativeBreaks, [now](auto const& e) { return now - e.second > 2; });
        }));
        log().info("lock: installed");
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        saveTracked();
        saveTimers();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

namespace {
struct Lifecycle {
    Lifecycle() {
        game::db::onWorldReady([] {
            loadTracked();
            loadTimers();
        });
        game::db::onWorldSave([] {
            saveTracked();
            saveTimers();
        });
        game::db::onWorldClosed([] {
            clearTracked();
            clearTimers();
            clearSafes();
            detail::clearScheduled();
            gLastClick.clear();
            gCreativeBreaks.clear();
        });
    }
} gLifecycle;
} // namespace

} // namespace cm::feature::lock

CM_REGISTER_CONFIG("lock", cm::feature::lock::LockConfig);
CM_REGISTER_MODULE(cm::feature::lock::LockModule);
