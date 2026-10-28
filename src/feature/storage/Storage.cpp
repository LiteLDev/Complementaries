// The storage unit's module: event wiring (placement, removal, interaction, item use), the tick
// (deferred actions, container views, quiver lending, overencumbered) and the lunch basket hook.

#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/player/PlayerSwingEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/util/Random.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemUseMethod.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/storage/Features.h"
#include "feature/storage/View.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Module.h"

namespace cm::feature::storage {

namespace {

constexpr char const* kSkeletonTag = "cm_quiver";

long long                                             gTick = 0;
std::vector<std::function<void()>>                    gDeferred;
std::unordered_map<long long, ItemStack>              gPendingPlace; ///< player -> item being placed
std::map<core::GlobalPos, bool>                       gBreakers;     ///< block -> broken in creative
std::unordered_map<long long, long long>              gLastAction;   ///< player -> tick (debounce)

std::multimap<long long, std::function<void()>>       gLater;        ///< tick -> action
std::set<core::GlobalPos>                             gRemoving;     ///< removals queued

void defer(std::function<void()> fn) { gDeferred.push_back(std::move(fn)); }
void deferTicks(int ticks, std::function<void()> fn) { gLater.emplace(gTick + ticks, std::move(fn)); }

/// Bedrock repeats interact / use packets while the button is held: one action per 5 ticks.
bool debounced(Player& player) {
    auto& last = gLastAction[playerKey(player)];
    if (last != 0 && gTick - last < 5) return true;
    last = gTick;
    return false;
}

void resetWorld() {
    view::reset();
    clearCaches();
    gDeferred.clear();
    gLater.clear();
    gRemoving.clear();
    gPendingPlace.clear();
    gBreakers.clear();
    gLastAction.clear();
}

void onTick() {
    ++gTick;
    if (!gDeferred.empty()) {
        auto run = std::move(gDeferred);
        gDeferred.clear();
        for (auto& fn : run) fn();
    }
    while (!gLater.empty() && gLater.begin()->first <= gTick) {
        auto fn = std::move(gLater.begin()->second);
        gLater.erase(gLater.begin());
        fn();
    }
    view::tick();
    auto level = ll::service::getLevel();
    if (!level) return;
    bool penalty = gTick % 27 == 0;
    level->forEachPlayer([penalty](Player& p) {
        quiverTick(p);
        if (penalty) overencumberedTick(p);
        return true;
    });
}

} // namespace

// The lunch basket "eats" its selected food when its eating animation ends.
LL_TYPE_INSTANCE_HOOK(
    BasketUseTimeDepletedHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$useTimeDepleted,
    ItemUseMethod,
    ItemStack& inout,
    Level*     level,
    Player*    player
) {
    if (player && !inout.isNull() && inout.getTypeName() == kLunchBasket) {
        if (!player->isClientSide()) {
            basketEat(*player, inout);
            player->sendInventory(false);
        }
        return ItemUseMethod::Eat; // the basket itself is never consumed
    }
    return origin(inout, level, player);
}

class StorageModule {
public:
    void install() {
        static bool lifecycle = false;
        if (!lifecycle) {
            lifecycle = true;
            game::db::onWorldReady(resetWorld);
            game::db::onWorldClosed(resetWorld);
        }
        BasketUseTimeDepletedHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); }));

        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>([](ll::event::PlayerPlacingBlockEvent& ev) {
            auto& p    = ev.self();
            auto const& held = p.getSelectedItem();
            if (kindOf(itemId(held)) != Kind::None) gPendingPlace[playerKey(p)] = held;
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
            auto& p    = ev.self();
            auto  kind = kindOf(ev.placedBlock());
            auto  it   = gPendingPlace.find(playerKey(p));
            if (kind == Kind::None) {
                if (it != gPendingPlace.end()) gPendingPlace.erase(it);
                return;
            }
            ItemStack item = it != gPendingPlace.end() ? it->second : makeStack(ev.placedBlock().getTypeName());
            if (it != gPendingPlace.end()) gPendingPlace.erase(it);
            auto gp = game::globalPos(p.getDimensionBlockSource(), ev.pos());
            defer([gp, item] {
                onBlockPlaced(gp, item);
                checkFall(gp);
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& ev) {
            auto& p      = ev.self();
            auto& region = p.getDimensionBlockSource();
            if (kindOf(region.getBlock(ev.pos())) != Kind::None) gBreakers[game::globalPos(region, ev.pos())] = p.isCreative();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto const& prev = ev.previousBlock();
            auto const& now  = ev.newBlock();
            if (&prev.getBlockType() == &now.getBlockType()) return;
            auto& region = ev.blockSource();
            if (kindOf(prev) != Kind::None && !movingBlocks()) {
                auto gp = game::globalPos(region, ev.pos());
                // the game reports some changes twice: one removal per block
                if (!gRemoving.insert(gp).second) return;
                bool creative = false;
                if (auto it = gBreakers.find(gp); it != gBreakers.end()) {
                    creative = it->second;
                    gBreakers.erase(it);
                }
                if (now.getTypeName() == "minecraft:moving_block") {
                    deferTicks(4, [gp, id = prev.getTypeName()] {
                        gRemoving.erase(gp);
                        onBlockPushed(gp, id);
                    });
                } else {
                    defer([gp, id = prev.getTypeName(), creative] {
                        gRemoving.erase(gp);
                        onBlockRemoved(gp, id, creative);
                    });
                }
            }
            if (now.isAir()) {
                BlockPos above{ev.pos().x, ev.pos().y + 1, ev.pos().z};
                if (above.y < region.getMaxHeight() && kindOf(region.getBlock(above)) == Kind::Sack) {
                    auto gp = game::globalPos(region, above);
                    defer([gp] { checkFall(gp); });
                }
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& p = ev.self();
            if (p.isClientSide()) return;
            auto&        region = p.getDimensionBlockSource();
            Block const& block  = ev.block() ? *ev.block() : region.getBlock(ev.blockPos());
            if (kindOf(block) != Kind::None) {
                if (p.isSneaking() && !isEmpty(ev.item())) return; // place the held block against it
                ev.cancel();
                if (debounced(p)) return;
                auto gp  = game::globalPos(region, ev.blockPos());
                auto key = playerKey(p);
                defer([gp, key] {
                    if (auto* pl = playerById(key)) interactBlock(*pl, gp);
                });
                return;
            }
            if (itemId(ev.item()) == kQuiver) {
                ev.cancel();
                if (debounced(p)) return;
                auto key   = playerKey(p);
                int  slot  = p.getSelectedItemSlot();
                bool sneak = p.isSneaking();
                defer([key, slot, sneak] {
                    if (auto* pl = playerById(key)) sneak ? cycleSelection(*pl, slot) : (void)openItemScreen(*pl, slot);
                });
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>([](ll::event::PlayerUseItemEvent& ev) {
            auto& p  = ev.self();
            auto  id = itemId(ev.item());
            if (p.isClientSide() || (id != kQuiver && id != kLunchBasket)) return;
            int  slot  = p.getSelectedItemSlot();
            bool sneak = p.isSneaking();
            if (id == kLunchBasket && !sneak && basketCanEat(p, slot)) return; // vanilla eating
            ev.cancel();
            if (debounced(p)) return;
            auto key = playerKey(p);
            defer([key, slot, sneak] {
                auto* pl = playerById(key);
                if (!pl) return;
                if (sneak) cycleSelection(*pl, slot);
                else openItemScreen(*pl, slot);
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerSwingEvent>([](ll::event::PlayerSwingEvent& ev) {
            auto& p = ev.self();
            if (p.isClientSide() || !p.isSneaking()) return;
            auto const& held = p.getSelectedItem();
            auto        id   = itemId(held);
            int         slot = -2;
            bool        open = false;
            if (id == kQuiver) slot = p.getSelectedItemSlot();
            else if (id == kLunchBasket) slot = p.getSelectedItemSlot(), open = true;
            else if (isRangedWeapon(held)) slot = findItem(p, kQuiver);
            if (slot < -1 || debounced(p)) return;
            auto key = playerKey(p);
            defer([key, slot, open] {
                auto* pl = playerById(key);
                if (!pl) return;
                if (open) openItemScreen(*pl, slot);
                else cycleSelection(*pl, slot);
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            returnProxies(ev.self());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::SpawnedMobEvent>([](ll::event::SpawnedMobEvent& ev) {
            auto mob = ev.mob();
            if (!mob) return;
            auto const& type = mob->getTypeName();
            if (type != "minecraft:skeleton" && type != "minecraft:stray" && type != "minecraft:bogged") return;
            auto level = ll::service::getLevel();
            if (level && level->getRandom().nextFloat() < config().quiverSkeletonSpawnChance) mob->addTag(kSkeletonTag);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>([](ll::event::MobDieEvent& ev) {
            auto& mob = ev.self();
            if (!mob.hasTag(kSkeletonTag)) return;
            auto& region = mob.getDimensionBlockSource();
            spawnItem(region, skeletonQuiver(), mob.getPosition());
        }));
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        BasketUseTimeDepletedHook::unhook();
        resetWorld();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace cm::feature::storage

CM_REGISTER_MODULE(cm::feature::storage::StorageModule);
