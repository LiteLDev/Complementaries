// U18 tweak: wires the vanilla tweaks to the game (events, hooks, world lifecycle).

#include <exception>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("tweak", cm::feature::tweak::TweakConfig);

namespace cm::feature::tweak {

TweakConfig const& config() { return cm::config::get<TweakConfig>(); }

namespace {
/// Runs a handler, logging (instead of propagating) unexpected exceptions out of game callbacks.
template <class F>
auto guarded(char const* what, F&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (std::exception const& e) {
        log().error("tweak: {} failed: {}", what, e.what());
    } catch (...) {
        log().error("tweak: {} failed", what);
    }
    return decltype(fn())();
}
} // namespace

bool dispatchUseOn(Player& player, ItemStack const& item, BlockPos const& pos, int face, Vec3 const& clickPos) {
    auto&        region = player.getDimensionBlockSource();
    Block const& block  = region.getBlock(pos);
    std::string  id     = itemId(item);
    UseOn        use{player, item, id, pos, face, block, clickPos};
    return guarded("block interaction", [&] {
        return bellChainUseOn(use) || hangingPotUseOn(use) || gunpowderUseOn(use) || sticksUseOn(use) || bottleXpUseOn(use)
            || cauldronUseOn(use) || throwUseOn(use);
    });
}

bool dispatchUse(Player& player, ItemStack const& item) {
    std::string id = itemId(item);
    if (id.empty()) return false;
    return guarded("item use", [&] { return throwUse(player, id) || infoUse(player, id); });
}

LL_TYPE_INSTANCE_HOOK(
    TweakProjectileHitHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    ::Actor&           owner,
    ::HitResult const& result
) {
    if (!owner.getLevel().isClientSide() && guarded("projectile hit", [&] { return projectileHit(owner, result); })) return;
    origin(owner, result);
}

LL_TYPE_INSTANCE_HOOK(
    TweakPlayerInteractHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    ::InteractionResult,
    ::Actor&      actor,
    ::Vec3 const& location
) {
    if (!getLevel().isClientSide() && guarded("entity interaction", [&] { return zombieHorseInteract(*this, actor); })) {
        InteractionResult r{};
        r.mSuccess = true;
        r.mSwing   = true;
        return r;
    }
    return origin(actor, location);
}

class TweakModule {
public:
    static TweakModule& get() {
        static TweakModule m;
        return m;
    }

    void install() {
        static bool lifecycle = false;
        if (!lifecycle) {
            lifecycle = true;
            game::db::onWorldReady([] { gunpowderLoad(); });
            game::db::onWorldSave([] { gunpowderSave(); });
            game::db::onWorldClosed([] {
                gunpowderClear();
                supportClear();
                throwablesClear();
                clearDeferred();
                clearDropSuppression();
            });
        }
        if (game::db::available()) gunpowderLoad(); // BDS: the world already exists

        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player = ev.self();
            if (player.getLevel().isClientSide()) return;
            if (dispatchUseOn(player, ev.item(), ev.blockPos(), static_cast<int>(ev.face()), ev.clickPos())) ev.cancel();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>([](ll::event::PlayerUseItemEvent& ev) {
            auto& player = ev.self();
            if (player.getLevel().isClientSide()) return;
            dispatchUse(player, ev.item());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& ev) {
            auto& player = ev.self();
            if (player.getLevel().isClientSide() || !player.isCreative()) return;
            auto& region = player.getDimensionBlockSource();
            if (blockName(region.getBlock(ev.pos())).starts_with("complementaries:")) {
                suppressDrops(game::globalPos(region, ev.pos()));
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            guarded("block change", [&] {
                onBlockChanged(region, ev.pos(), ev.previousBlock(), ev.newBlock());
                gunpowderBlockChanged(region, ev.pos(), ev.previousBlock(), ev.newBlock());
                supportBlockChanged(region, ev.pos(), ev.previousBlock(), ev.newBlock());
                return true;
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
            guarded("tick", [] {
                advanceTick();
                runDeferred();
                supportTick();
                gunpowderTick();
                clearDropSuppression();
                return true;
            });
        }));
        TweakProjectileHitHook::hook();
        TweakPlayerInteractHook::hook();
        log().info("tweak: vanilla tweaks installed");
    }

    void uninstall() {
        TweakPlayerInteractHook::unhook();
        TweakProjectileHitHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        if (game::db::available()) gunpowderSave();
        gunpowderClear();
        clearDeferred();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace cm::feature::tweak

CM_REGISTER_MODULE(cm::feature::tweak::TweakModule);
