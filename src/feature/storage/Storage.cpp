// The storage unit's module: event wiring (placement, removal, pistons, interaction, item use), the
// tick (deferred actions, container views, quiver lending, overencumbered) and the lunch basket hook.

#include <functional>
#include <map>
#include <optional>
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

#include "mc/deps/core/math/Vec3.h"
#include "mc/util/Random.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorType.h"
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
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::feature::storage {

namespace {

constexpr char const*      kSkeletonTag  = "cm_quiver";
constexpr std::string_view kMovingBlock  = "minecraft:moving_block";
constexpr int              kStaleTicks   = 5;  ///< place / break bookkeeping older than this is ignored
constexpr int              kLandTimeout  = 6;  ///< ticks a removed block may take to land (piston)

struct Pending {
    ItemStack item;
    long long tick = 0;
};
struct Removed {
    std::string                id;
    std::optional<BlockRecord> rec;
    long long                  tick     = 0;
    bool                       creative = false;
};

long long                                             gTick = 0;
std::vector<std::function<void()>>                    gDeferred;
std::unordered_map<long long, Pending>                gPendingPlace; ///< player -> item being placed
std::map<core::GlobalPos, std::pair<bool, long long>> gBreakers;     ///< block -> (creative, tick)
std::unordered_map<long long, long long>              gLastAction;   ///< player -> tick (debounce)
std::map<core::GlobalPos, Removed>                    gRemoved;      ///< removed blocks waiting to drop

void defer(std::function<void()> fn) { gDeferred.push_back(std::move(fn)); }

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
    gRemoved.clear();
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
    for (auto it = gRemoved.begin(); it != gRemoved.end();) {
        if (gTick - it->second.tick < kLandTimeout) {
            ++it;
            continue;
        }
        onBlockGone(it->first, it->second.id, it->second.rec, it->second.creative);
        it = gRemoved.erase(it);
    }
    view::tick();
    auto level = ll::service::getLevel();
    if (!level) return;
    bool penalty  = gTick % 27 == 0;
    bool fullScan = gTick % 20 == 0;
    level->forEachPlayer([penalty, fullScan](Player& p) {
        quiverTick(p, fullScan);
        if (penalty) overencumberedTick(p);
        return true;
    });
}

/// Use of a quiver / lunch basket (in the air or on a block that is not ours). Returns true when
/// the vanilla use must be cancelled.
bool useStorageItem(Player& p, std::string const& id) {
    int  slot  = p.getSelectedItemSlot();
    bool sneak = p.isSneaking();
    if (id == kLunchBasket && !sneak && basketCanEat(p, slot)) return false; // vanilla eating
    if (debounced(p)) return true;
    auto key = playerKey(p);
    defer([key, slot, sneak] {
        auto* pl = playerById(key);
        if (!pl) return;
        if (sneak) cycleSelection(*pl, slot);
        else openItemScreen(*pl, slot);
    });
    return true;
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto const& prev = ev.previousBlock();
    auto const& now  = ev.newBlock();
    if (&prev.getBlockType() == &now.getBlockType()) return;
    auto& region = ev.blockSource();
    Kind  was    = kindOf(prev);
    if (was != Kind::None && !movingBlocks()) {
        // Removed (broken, exploded, replaced) or taken by a piston: Bedrock pistons turn the block
        // into air and later place it again from a moving block next to it. The record waits in
        // memory for a few ticks: it follows a block that lands next to it, else the block drops.
        auto gp = game::globalPos(region, ev.pos());
        if (!gRemoved.contains(gp)) { // the game reports some changes twice
            bool creative = false;
            if (auto it = gBreakers.find(gp); it != gBreakers.end()) {
                creative = it->second.first && gTick - it->second.second <= kStaleTicks;
                gBreakers.erase(it);
            }
            auto rec = loadBlockRecord(gp);
            if (rec) {
                eraseBlockRecord(gp);
                defer([uid = rec->uid] { view::close(uid); });
            }
            gRemoved[gp] = Removed{prev.getTypeName(), rec, gTick, creative};
        }
    }
    if (prev.getTypeName() == kMovingBlock && kindOf(now) != Kind::None) {
        // a pushed block landed: pair it with the removal next to it
        static constexpr int kDirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        auto                 gp          = game::globalPos(region, ev.pos());
        for (auto const& d : kDirs) {
            core::GlobalPos src{gp.dim, gp.x - d[0], gp.y - d[1], gp.z - d[2]};
            auto            it = gRemoved.find(src);
            if (it == gRemoved.end() || it->second.id != now.getTypeName()) continue;
            defer([gp, rec = it->second.rec] { onBlockLanded(gp, rec); });
            gRemoved.erase(it);
            break;
        }
    }
    if (now.isAir()) {
        BlockPos above{ev.pos().x, ev.pos().y + 1, ev.pos().z};
        if (above.y < region.getMaxHeight() && kindOf(region.getBlock(above)) == Kind::Sack) {
            auto gp = game::globalPos(region, above);
            defer([gp] { checkFall(gp); });
        }
    }
}

bool killedByPlayer(ActorDamageSource const& source) {
    return source.isEntitySource()
        && (source.getEntityType() == ActorType::Player || source.getDamagingEntityType() == ActorType::Player);
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
        view::install();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));

        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>([](ll::event::PlayerPlacingBlockEvent& ev) {
            auto&       p    = ev.self();
            auto const& held = p.getSelectedItem();
            if (kindOf(itemId(held)) != Kind::None) gPendingPlace[playerKey(p)] = Pending{held, gTick};
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
            auto&                    p    = ev.self();
            auto                     kind = kindOf(ev.placedBlock());
            std::optional<ItemStack> held;
            if (auto it = gPendingPlace.find(playerKey(p)); it != gPendingPlace.end()) {
                if (gTick - it->second.tick <= kStaleTicks) held = it->second.item;
                gPendingPlace.erase(it);
            }
            if (kind == Kind::None) return;
            ItemStack item = held && itemId(*held) == ev.placedBlock().getTypeName() ? *held
                                                                                     : makeStack(ev.placedBlock().getTypeName());
            auto      gp   = game::globalPos(p.getDimensionBlockSource(), ev.pos());
            defer([gp, item] {
                onBlockPlaced(gp, item);
                checkFall(gp);
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& ev) {
            auto& p      = ev.self();
            auto& region = p.getDimensionBlockSource();
            if (kindOf(region.getBlock(ev.pos())) != Kind::None) {
                gBreakers[game::globalPos(region, ev.pos())] = {p.isCreative(), gTick};
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
            auto id = itemId(ev.item());
            if ((id == kQuiver || id == kLunchBasket) && useStorageItem(p, id)) ev.cancel();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>([](ll::event::PlayerUseItemEvent& ev) {
            auto& p  = ev.self();
            auto  id = itemId(ev.item());
            if (p.isClientSide() || (id != kQuiver && id != kLunchBasket)) return;
            if (useStorageItem(p, id)) ev.cancel();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerSwingEvent>([](ll::event::PlayerSwingEvent& ev) {
            auto& p = ev.self();
            if (p.isClientSide() || !p.isSneaking()) return;
            auto const& held = p.getSelectedItem();
            auto        id   = itemId(held);
            int         slot = -2;
            bool        open = false;
            if (id == kQuiver) {
                slot = p.getSelectedItemSlot();
            } else if (id == kLunchBasket) {
                slot = p.getSelectedItemSlot();
                open = true;
            } else if (isRangedWeapon(held)) {
                slot = findItem(p, kQuiver);
            }
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
            if (!mob.hasTag(kSkeletonTag) || !killedByPlayer(ev.source())) return;
            spawnItem(mob.getDimensionBlockSource(), skeletonQuiver(), mob.getPosition());
        }));
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        BasketUseTimeDepletedHook::unhook();
        view::uninstall();
        resetWorld();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace cm::feature::storage

CM_REGISTER_MODULE(cm::feature::storage::StorageModule);
