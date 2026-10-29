// U11 `rope` module: listeners, ticking and world lifecycle.

#include <map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/mechanism/Crankable.h"
#include "feature/rope/Common.h"
#include "feature/rope/Rope.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::feature::rope {

namespace {
bool gInstalled = false;
bool gReady     = false;

/// The last use of one of our blocks by a player: clients send a burst of identical use packets
/// for one click (and repeat them every few ticks while the button is held), so a use of the same
/// block within `kUseRepeatTicks` is the same click.
struct LastUse {
    std::uint64_t   tick = 0;
    core::GlobalPos pos;
    bool            handled = false;
};
constexpr std::uint64_t         kUseRepeatTicks = 4;
std::map<std::int64_t, LastUse> gLastUse;

void worldReady() {
    if (!gInstalled || gReady) return;
    gReady = true;
    resolveTypes();
    flax::load();
    arrow::load();
    if (!types().rope) log().warn("rope: complementaries:rope is not registered (behavior pack missing?)");
}

void worldSave() {
    if (!gReady) return;
    flax::save();
    arrow::save();
}

void worldClosed() {
    gReady = false;
    detail::clearLater();
    gLastUse.clear();
    ropes::reset();
    pulley::reset();
    flax::reset();
    arrow::reset();
    clearTypes();
}

[[maybe_unused]] bool const gLifecycle = [] {
    game::db::onWorldReady(worldReady);
    game::db::onWorldSave(worldSave);
    game::db::onWorldClosed(worldClosed);
    return true;
}();

/// True when this use repeats the previous one (and was handled: cancel it too).
bool repeatedUse(Player& player, core::GlobalPos const& g, bool& handled) {
    auto& last = gLastUse[player.getOrCreateUniqueID().rawID];
    if (last.pos == g && now() - last.tick < kUseRepeatTicks) {
        handled = last.handled;
        return true;
    }
    last = {now(), g, false};
    return false;
}

void markHandled(Player& player) { gLastUse[player.getOrCreateUniqueID().rawID].handled = true; }

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (!gReady || player.getLevel().isClientSide()) return;
    auto const& pos    = ev.blockPos();
    auto&       region = player.getDimensionBlockSource();
    if (!loaded(region, pos)) return;
    Block const& b    = region.getBlock(pos);
    int          face = static_cast<int>(ev.face());
    auto const&  item = ev.item();
    auto const   id   = itemId(item);
    bool const   ours =
        isRope(b) || isPulley(b) || (isFlaxCrop(b) && id == "minecraft:bone_meal");
    if (ours) {
        bool handled = false;
        if (repeatedUse(player, gpos(region, pos), handled)) {
            if (handled) ev.cancel();
            return;
        }
    }
    if (isRope(b)) {
        if (ropes::onUse(player, pos, face, item)) {
            ev.cancel();
            markHandled(player);
        }
        return;
    }
    if (isPulley(b)) {
        if (pulley::onUse(player, pos)) {
            ev.cancel();
            markHandled(player);
        }
        return;
    }
    if (isFlaxCrop(b) && id == "minecraft:bone_meal") {
        ev.cancel();
        markHandled(player);
        auto uid = player.getOrCreateUniqueID().rawID;
        auto dim = static_cast<int>(player.getDimensionId());
        later(1, [uid, dim, pos] {
            auto* pl = playerById(uid);
            auto* r  = serverRegion(dim);
            if (!pl || !r) return;
            if (survival(*pl) && itemId(pl->getSelectedItem()) != "minecraft:bone_meal") return;
            if (flax::boneMeal(*r, pos)) {
                consumeSelected(*pl);
                log().info("rope: {} used bone meal on flax", pl->getRealName());
            }
        });
        return;
    }
    if (id == kRope && ropes::refusePlacement(player, pos, face)) ev.cancel();
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (!gReady) return;
    auto& region = ev.blockSource();
    if (!isServerRegion(region) || ev.layer() != 0) return;
    auto const& prev = ev.previousBlock();
    auto const& next = ev.newBlock();
    ropes::onBlockChanged(region, ev.pos(), prev, next);
    if (isPulley(prev)) pulley::onBlockChanged(region, ev.pos(), prev, next);
    if (isFlaxCrop(prev) || isFlaxCrop(next)) flax::onBlockChanged(region, ev.pos(), prev, next);
}

void onDestroy(ll::event::PlayerDestroyBlockEvent& ev) {
    auto& player = ev.self();
    if (!gReady || player.getLevel().isClientSide() || survival(player)) return;
    auto& region = player.getDimensionBlockSource();
    if (loaded(region, ev.pos()) && isFlaxCrop(region.getBlock(ev.pos()))) flax::noDrops(gpos(region, ev.pos()));
}

void onHurt(ll::event::ActorHurtEvent& ev) {
    if (!gReady || ev.source().mCause != SharedTypes::Legacy::ActorDamageCause::Fall) return;
    auto& actor = ev.self();
    if (actor.getLevel().isClientSide()) return;
    auto& region = actor.getDimensionBlockSource();
    auto  feet   = actor.getFeetBlockPos();
    for (auto const& p : {feet, neighbor(feet, 0)}) {
        if (loaded(region, p) && isFlaxBlock(region.getBlock(p))) {
            ev.damage() *= 0.2f; // like hay bales
            return;
        }
    }
}

void onTick(ll::event::ServerLevelTickEvent& ev) {
    if (!gReady || ev.level().isClientSide()) return;
    detail::runLater();
    ropes::tick();
    flax::tick();
    arrow::tick();
    if (now() % 20 == 0) {
        ev.level().forEachPlayer([](Player& p) {
            arrow::normalize(p);
            return true;
        });
    }
}
} // namespace

class RopeModule {
public:
    void install() {
        gInstalled = true;
        auto& bus  = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(onDestroy));
        mListeners.push_back(bus.emplaceListener<ll::event::ActorHurtEvent>(onHurt));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        arrow::install();
        flax::install();
        // a crank on a pulley turns it: clockwise winds the rope up, counter-clockwise (sneaking) lowers it
        mechanism::registerCrankable(std::string(kPulley), [](BlockSource& r, BlockPos const& p, bool ccw, Player* by) {
            turnPulley(r, p, !ccw, by);
        });
        if (game::db::available()) worldReady(); // BDS: the world exists before mods are enabled
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        arrow::uninstall();
        flax::uninstall();
        mechanism::unregisterCrankable(std::string(kPulley));
        if (gReady) {
            worldSave();
            worldClosed();
        }
        gInstalled = false;
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace cm::feature::rope

CM_REGISTER_MODULE(cm::feature::rope::RopeModule);
