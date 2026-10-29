// The tool unit's module: event listeners and hooks that dispatch to the tools.

#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPickUpItemEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerItemInUse.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"

#include "feature/tool/Tool.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::feature::tool {

namespace {

/// Tick of the last block interaction we handled per player: a use-on-block may be followed by a
/// use-in-air of the same click, which must not act twice.
std::unordered_map<std::int64_t, std::uint64_t>& lastBlockUse() {
    static std::unordered_map<std::int64_t, std::uint64_t> m;
    return m;
}

bool recentlyUsedOnBlock(Player& player) {
    auto it = lastBlockUse().find(player.getOrCreateUniqueID().rawID);
    return it != lastBlockUse().end() && currentTick() <= it->second + 2;
}

/// Runs `fn(player)` next tick if the player is still there and still holds `id`.
void deferForPlayer(Player& player, std::string_view id, std::function<void(Player&)> fn) {
    std::int64_t uid    = player.getOrCreateUniqueID().rawID;
    lastBlockUse()[uid] = currentTick();
    nextTick([uid, id = std::string(id), fn = std::move(fn)] {
        auto* p = playerById(uid);
        if (p && isItem(p->getSelectedItem(), id)) fn(*p);
    });
}

void onUseItem(ll::event::PlayerUseItemEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& item = ev.item();
    if (item.isNull()) return;
    auto id = itemId(item);
    if (!id.starts_with("complementaries:")) return;
    if (recentlyUsedOnBlock(player)) return;
    if (id == ids::BubbleBlower) {
        if (bubble::tryRefill(player)) ev.cancel();
    } else if (id == ids::Soap) {
        soap::useInAir(player);
    } else if (id == ids::Flute) {
        flute::useInAir(player);
    } else if (id == ids::ConfettiPopper) {
        confetti::useInAir(player);
    } else if (id == ids::AntiqueInk) {
        antique::useInAir(player);
    } else if (id == ids::EmptySliceMap) {
        slicemap::useInAir(player);
    }
}

void onInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& item = ev.item();
    if (item.isNull()) return;
    auto id = itemId(item);
    if (!id.starts_with("complementaries:")) return;
    BlockPos    pos    = ev.blockPos();
    int         face   = static_cast<int>(ev.face());
    auto&       region = player.getDimensionBlockSource();
    auto const& block  = region.getBlock(pos);
    if (id == ids::Wrench) {
        if (!config().wrench.enabled) return;
        if (config().wrench.bypassWhenSneaking && player.isSneaking()) return;
        ev.cancel();
        deferForPlayer(player, id, [pos, face](Player& p) { wrench::useOnBlock(p, pos, face); });
    } else if (id == ids::BubbleBlower) {
        if (!config().bubbleBlower.enabled) return;
        ev.cancel();
        deferForPlayer(player, id, [pos, face](Player& p) { bubble::useOnBlock(p, pos, face); });
    } else if (id == ids::Soap) {
        if (!config().soap.enabled) return;
        if (!soap::cleanBlock(block) && !antique::isSign(block)) return;
        ev.cancel();
        deferForPlayer(player, id, [pos](Player& p) { soap::useOnBlock(p, pos); });
    } else if (id == ids::AntiqueInk) {
        if (!config().antiqueInk.enabled || !antique::isSign(block)) return;
        ev.cancel();
        deferForPlayer(player, id, [pos](Player& p) { antique::useOnBlock(p, pos); });
    }
}

void onTick() {
    runQueued();
    slingshot::tick();
    bubble::tick();
    flute::tick();
    auto* l = level();
    if (!l) return;
    // bubble blowing: every player keeping the use button down on a blower
    l->forEachPlayer([](Player& p) {
        auto const& inUse = *p.mItemInUse;
        if (isItem(inUse.mItem, ids::BubbleBlower)) bubble::blowTick(p, inUse.getUsedDuration(p.getEntityContext()));
        return true;
    });
    if (currentTick() % 5 == 0) slicemap::tick();
    if (currentTick() % 200 == 0) {
        auto t = currentTick();
        std::erase_if(lastBlockUse(), [t](auto const& kv) { return kv.second + 20 < t; });
    }
}

} // namespace

// ---- hooks ---------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    ToolReleaseUsingHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$releaseUsing,
    void,
    ItemStack& item,
    Player*    player,
    int        durationLeft
) {
    origin(item, player, durationLeft);
    if (!player || player->isClientSide() || !isItem(item, ids::Slingshot)) return;
    int used = getMaxUseDuration(&item) - durationLeft;
    slingshot::release(*player, item, used);
}

LL_TYPE_INSTANCE_HOOK(
    ToolDispenseHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$dispense,
    bool,
    BlockSource& region,
    Container&   container,
    int          slot,
    Vec3 const&  pos,
    uchar        face
) {
    if (config().confettiPopper.enabled && isItem(container.getItem(slot), ids::ConfettiPopper)) {
        auto off = faceOffset(face);
        confetti::burst(
            static_cast<int>(region.getDimensionId()),
            pos,
            Vec3{static_cast<float>(off.x), static_cast<float>(off.y) + 0.1f, static_cast<float>(off.z)}
        );
        container.removeItem(slot, 1);
        log().info("confetti popper fired by a dispenser");
        return true;
    }
    return origin(region, container, slot, pos, face);
}

LL_TYPE_INSTANCE_HOOK(
    ToolInteractEntityHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$interact,
    bool,
    Actor&      entity,
    Vec3 const& location,
    HandSlot    handSlot
) {
    Player& player = mPlayer;
    if (!player.isClientSide() && handSlot == HandSlot::Mainhand) {
        auto const& item = player.getSelectedItem();
        auto        id   = itemId(item);
        if (id.starts_with("complementaries:")) {
            bool handled = false;
            if (id == ids::Flute) handled = flute::useOnEntity(player, entity);
            else if (id == ids::Soap) handled = soap::useOnEntity(player, entity);
            else if (id == ids::Wrench) handled = wrench::useOnEntity(player, entity);
            if (handled) {
                lastBlockUse()[player.getOrCreateUniqueID().rawID] = currentTick();
                return true;
            }
        }
    }
    return origin(entity, location, handSlot);
}

// ---- module --------------------------------------------------------------------------------------

class ToolModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>(onUseItem));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteractBlock));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); }));
        // a projectile left over from a closed world: pick it up as the plain item
        mListeners.push_back(
            bus.emplaceListener<ll::event::PlayerPickUpItemEvent>([](ll::event::PlayerPickUpItemEvent& ev) {
                auto& stack = *ev.itemActor().mItem;
                if (stack.mUserData && stack.mUserData->contains("cm_sling")) removeData(stack, "cm_sling");
            })
        );
        ToolReleaseUsingHook::hook();
        ToolDispenseHook::hook();
        ToolInteractEntityHook::hook();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        ToolReleaseUsingHook::unhook();
        ToolDispenseHook::unhook();
        ToolInteractEntityHook::unhook();
        if (game::db::available()) bubble::save();
        clearAll();
    }

    static void clearAll() {
        slingshot::clear();
        bubble::clear();
        flute::clear();
        slicemap::clear();
        lastBlockUse().clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

namespace {
// World lifecycle (registered at static init so BDS, whose world is ready before features
// install, gets the ready call too).
struct Lifecycle {
    Lifecycle() {
        game::db::onWorldReady([] {
            ToolModule::clearAll();
            bubble::load();
        });
        game::db::onWorldSave([] { bubble::save(); });
        game::db::onWorldClosed([] { ToolModule::clearAll(); });
    }
} gLifecycle;
} // namespace

} // namespace cm::feature::tool

CM_REGISTER_MODULE(cm::feature::tool::ToolModule);
