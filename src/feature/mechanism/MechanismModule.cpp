// U7 mechanism module: tracks the ticking mechanism blocks, runs their per-tick update, and wires
// the game callbacks (player use / break, falls, neighbour changes, redstone setup).

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/components/BlockRedstoneConsumerComponent.h"
#include "mc/world/level/block/components/BlockRedstoneProducerComponent.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/mechanism/Mechanism.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("mechanism", cm::feature::mechanism::MechanismConfig);

namespace cm::feature::mechanism {

namespace {
constexpr char const* kTrackedKey = "cm:mechanism:tracked";

struct Runtime {
    std::set<core::GlobalPos>           tracked;
    bool                                dirty = false;
    std::vector<std::function<void()>>  deferred;
    std::map<std::pair<std::int64_t, core::GlobalPos>, std::uint64_t> lastUse; ///< use debounce
    std::uint64_t                       tick = 0;
};

Runtime& rt() {
    static Runtime r;
    return r;
}

bool gInstalled = false;

void loadTracked() {
    // merged into what the redstone setup hooks found already (chunks may load before this runs)
    auto& r = rt();
    if (auto j = game::db::load(kTrackedKey); j && j->is_array()) {
        for (auto const& e : *j) {
            if (e.is_array() && e.size() == 4)
                r.tracked.insert({e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
        }
    }
}

void saveTracked() {
    auto& r = rt();
    if (!r.dirty) return;
    nlohmann::json j = nlohmann::json::array();
    for (auto const& p : r.tracked) j.push_back({p.dim, p.x, p.y, p.z});
    game::db::save(kTrackedKey, j);
    r.dirty = false;
}

void resetAll() {
    auto& r = rt();
    r.tracked.clear();
    r.deferred.clear();
    r.lastUse.clear();
    r.dirty = false;
    launcher::reset();
    bellows::reset();
    turntable::reset();
}

// world lifecycle listeners are registered once (the foundation has no removal)
struct WorldHooks {
    WorldHooks() {
        game::db::onWorldReady([] { loadTracked(); });
        game::db::onWorldSave([] { saveTracked(); });
        game::db::onWorldClosed([] { resetAll(); });
    }
} const gWorldHooks;

void onTick() {
    auto& r = rt();
    ++r.tick;
    if (!r.deferred.empty()) {
        auto jobs = std::move(r.deferred);
        r.deferred.clear();
        for (auto& job : jobs) job();
    }
    std::map<int, std::vector<BlockPos>> cogs;
    for (auto it = r.tracked.begin(); it != r.tracked.end();) {
        BlockSource* region = regionOf(it->dim);
        BlockPos     pos    = game::blockPos(*it);
        if (!region || !region->hasChunksAt(pos, 0, false)) {
            ++it; // not loaded: keep it for later
            continue;
        }
        Block const& b = region->getBlock(pos);
        switch (kindOf(b)) {
        case Kind::Launcher:
            launcher::tick(*region, pos, b);
            break;
        case Kind::Bellows:
            bellows::tick(*region, pos, b);
            break;
        case Kind::TurnTable:
            turntable::tick(*region, pos, b);
            break;
        case Kind::Cog:
            cogs[it->dim].push_back(pos);
            break;
        default:
            it      = r.tracked.erase(it);
            r.dirty = true;
            continue;
        }
        ++it;
    }
    for (auto const& [dim, list] : cogs) {
        if (auto* region = regionOf(dim)) cog::tickAll(*region, list);
    }
}

/// Debounces repeated use packets of one click (and of holding the use button too fast).
bool debounced(Player const& player, core::GlobalPos const& pos) {
    auto&  r   = rt();
    auto   key = std::make_pair(player.getOrCreateUniqueID().rawID, pos);
    auto   it  = r.lastUse.find(key);
    bool   hit = it != r.lastUse.end() && r.tick - it->second < 4;
    if (r.lastUse.size() > 256) r.lastUse.clear();
    r.lastUse[key] = r.tick;
    return hit;
}

Player* playerById(std::int64_t id) {
    auto level = ll::service::getLevel();
    return level ? level->getPlayer(ActorUniqueID{id}) : nullptr;
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto block = ev.block();
    if (!block) return;
    Kind k = kindOf(*block);
    if (k != Kind::Crank && k != Kind::TurnTable && k != Kind::Bellows) return;
    Player&  player = ev.self();
    auto&    region = player.getDimensionBlockSource();
    BlockPos pos    = ev.blockPos();
    int      dim    = static_cast<int>(region.getDimensionId());
    auto     gp     = game::globalPos(dim, pos);
    bool     empty  = ev.item().isNull();
    auto     pid    = player.getOrCreateUniqueID().rawID;
    switch (k) {
    case Kind::Crank: {
        ev.cancel(); // never place items against a crank
        if (debounced(player, gp)) return;
        bool ccw = player.isSneaking();
        defer([dim, pos, ccw, pid] {
            if (auto* r = regionOf(dim)) crank::turn(*r, pos, ccw, playerById(pid));
        });
        break;
    }
    case Kind::TurnTable: {
        int face = static_cast<int>(ev.face());
        if (face == facingOf(*block) && !empty) return; // placing on the top works as usual
        ev.cancel();
        if (debounced(player, gp)) return;
        defer([dim, pos, pid] {
            auto* r = regionOf(dim);
            if (!r) return;
            bool inv = turntable::toggle(*r, pos);
            if (auto* p = playerById(pid))
                actionBar(
                    *p,
                    inv ? trFor(*p, "cm.mechanism.turn_table.counterclockwise", "Turn Table: counter-clockwise")
                        : trFor(*p, "cm.mechanism.turn_table.clockwise", "Turn Table: clockwise")
                );
        });
        break;
    }
    case Kind::Bellows:
        if (!empty || debounced(player, gp)) return;
        defer([dim, pos] {
            if (auto* r = regionOf(dim)) bellows::press(*r, pos);
        });
        break;
    default:
        break;
    }
}

void onDestroy(ll::event::PlayerDestroyBlockEvent& ev) {
    Player& player = ev.self();
    auto&   region = player.getDimensionBlockSource();
    if (!region.hasChunksAt(ev.pos(), 0, false)) return;
    Block const& b = region.getBlock(ev.pos());
    Kind         k = kindOf(b);
    if (k != Kind::Head && k != Kind::Arm) return;
    BlockPos base     = offset(ev.pos(), rotation::opposite(facingOf(b)));
    bool     creative = player.isCreative();
    int      dim      = static_cast<int>(region.getDimensionId());
    defer([dim, base, creative] {
        if (auto* r = regionOf(dim)) launcher::playerBroke(*r, base, creative);
    });
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    Kind k = kindOf(ev.newBlock());
    if (k == Kind::None) return;
    if (isTicking(k)) track(ev.blockSource(), ev.pos());
    if (k == Kind::Crank && kindOf(ev.previousBlock()) != Kind::Crank) {
        int      dim = static_cast<int>(ev.blockSource().getDimensionId());
        BlockPos pos = ev.pos();
        defer([dim, pos] {
            if (auto* r = regionOf(dim)) crank::check(*r, pos);
        });
    }
}

void onNeighborChanged(BlockType const& type, BlockSource& region, BlockPos const& pos) {
    std::string_view n = type.getTypeName();
    if (!n.starts_with("complementaries:")) return;
    Kind k = kindOf(*type.mDefaultState);
    if (k != Kind::Head && k != Kind::Arm && k != Kind::Crank) return;
    int dim = static_cast<int>(region.getDimensionId());
    defer([dim, pos, k] {
        auto* r = regionOf(dim);
        if (!r || !r->hasChunksAt(pos, 0, false)) return;
        if (k == Kind::Crank) crank::check(*r, pos);
        else launcher::check(*r, pos);
    });
}
} // namespace

// ---- hooks --------------------------------------------------------------------------------------

LL_TYPE_STATIC_HOOK(
    MechanismConsumerSetupHook,
    ll::memory::HookPriority::Normal,
    BlockRedstoneConsumerComponent,
    &BlockRedstoneConsumerComponent::setupRedstoneComponent,
    void,
    ::Block const&    block,
    ::BlockSource&    region,
    ::BlockPos const& pos
) {
    origin(block, region, pos);
    if (isTicking(kindOf(block))) track(region, pos);
}

LL_TYPE_STATIC_HOOK(
    MechanismProducerSetupHook,
    ll::memory::HookPriority::Normal,
    BlockRedstoneProducerComponent,
    &BlockRedstoneProducerComponent::setupRedstoneComponent,
    void,
    ::Block const&    block,
    ::BlockSource&    region,
    ::BlockPos const& pos
) {
    origin(block, region, pos);
    if (isTicking(kindOf(block))) track(region, pos);
}

LL_TYPE_INSTANCE_HOOK(
    MechanismNeighborChangedHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::$neighborChanged,
    void,
    ::BlockSource&    region,
    ::BlockPos const& pos,
    ::BlockPos const& neighborPos
) {
    origin(region, pos, neighborPos);
    onNeighborChanged(*this, region, pos);
}

LL_TYPE_INSTANCE_HOOK(
    MechanismFallDamageHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::handleFallDamage,
    void,
    float               fallDistance,
    float               multiplier,
    ::ActorDamageSource source
) {
    if (gInstalled && launcher::absorbFall(*this, fallDistance)) return;
    origin(fallDistance, multiplier, std::move(source));
}

// ---- runtime API --------------------------------------------------------------------------------

void defer(std::function<void()> fn) { rt().deferred.push_back(std::move(fn)); }

void track(BlockSource const& region, BlockPos const& pos) {
    if (rt().tracked.insert(game::globalPos(region, pos)).second) rt().dirty = true;
}

bool isTracked(core::GlobalPos const& pos) { return rt().tracked.contains(pos); }

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    return d ? &d->getBlockSourceFromMainChunkSource() : nullptr;
}

// ---- module -------------------------------------------------------------------------------------

class MechanismModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(onDestroy));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        MechanismConsumerSetupHook::hook();
        MechanismProducerSetupHook::hook();
        MechanismNeighborChangedHook::hook();
        MechanismFallDamageHook::hook();
        gInstalled = true;
        if (game::db::available()) loadTracked();
    }

    void uninstall() {
        gInstalled = false;
        auto& bus  = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        MechanismConsumerSetupHook::unhook();
        MechanismProducerSetupHook::unhook();
        MechanismNeighborChangedHook::unhook();
        MechanismFallDamageHook::unhook();
        if (game::db::available()) saveTracked();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace cm::feature::mechanism

CM_REGISTER_MODULE(cm::feature::mechanism::MechanismModule);
