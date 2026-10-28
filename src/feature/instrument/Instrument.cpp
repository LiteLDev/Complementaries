// U6 `instrument`: module, block registry (memory + world DB), events, ticking and commands.

#include "feature/instrument/Instrument.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"
#include "mc/safety/RedactableString.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("instrument", cm::feature::instrument::Config);

namespace cm::feature::instrument {

Config const& config() { return cm::config::get<Config>(); }

// ---- kinds -----------------------------------------------------------------------------------------
Kind kindOf(std::string_view id) {
    if (id == kGlobe) return Kind::Globe;
    if (id == kGlobeSepia) return Kind::GlobeSepia;
    if (id == kClock) return Kind::Clock;
    if (id == kHourglass) return Kind::Hourglass;
    if (id == kWindVane) return Kind::WindVane;
    return Kind::None;
}

Kind kindOf(Block const& block) {
    auto const& name = block.getTypeName();
    if (name.size() < 17 || name.compare(0, 16, "complementaries:") != 0) return Kind::None;
    return kindOf(std::string_view(name));
}

std::string_view idOf(Kind kind) {
    switch (kind) {
    case Kind::Globe:
        return kGlobe;
    case Kind::GlobeSepia:
        return kGlobeSepia;
    case Kind::Clock:
        return kClock;
    case Kind::Hourglass:
        return kHourglass;
    case Kind::WindVane:
        return kWindVane;
    default:
        return {};
    }
}

bool enabled(Kind kind) {
    auto const& c = config();
    switch (kind) {
    case Kind::Globe:
        return c.globe;
    case Kind::GlobeSepia:
        return c.globeSepia;
    case Kind::Clock:
        return c.clockBlock;
    case Kind::Hourglass:
        return c.hourglass;
    case Kind::WindVane:
        return c.windVane;
    default:
        return false;
    }
}

// ---- registry ----------------------------------------------------------------------------------------
namespace {
constexpr std::string_view kKeyPrefix = "cm:instrument:b:";

std::map<core::GlobalPos, Record>       gRecords;
std::vector<core::GlobalPos>            gChanged;   ///< positions whose block changed (re-checked next tick)
std::vector<core::GlobalPos>            gErased;    ///< DB keys to delete on the next save
std::vector<std::function<void()>>      gNext;      ///< work for the next tick
std::int64_t                            gTick   = 0;
bool                                    gLoaded = false;
std::unordered_map<std::int64_t, std::pair<std::int64_t, core::GlobalPos>> gLastUse; ///< player -> (tick, pos)
std::unordered_map<std::int64_t, std::pair<std::int64_t, std::string>>     gPlacingName;
std::unordered_map<std::int64_t, std::int64_t>                             gLastClock;

std::string dbKey(core::GlobalPos const& pos) { return std::string(kKeyPrefix) + pos.key(); }

nlohmann::json toJson(Record const& r) {
    nlohmann::json j{{"kind", static_cast<int>(r.kind)}};
    switch (r.kind) {
    case Kind::Globe:
    case Kind::GlobeSepia:
        j["rotation"] = r.rotation;
        j["yaw"]      = r.yaw;
        j["powered"]  = r.powered;
        j["sheared"]  = r.sheared;
        if (!r.name.empty()) j["name"] = r.name;
        break;
    case Kind::Hourglass:
        if (!r.item.empty()) {
            j["item"] = r.item;
            j["aux"]  = r.aux;
        }
        j["progress"] = r.progress;
        break;
    case Kind::WindVane:
        j["charged"] = r.windCharged;
        break;
    case Kind::Clock:
        j["power"] = r.power;
        break;
    default:
        break;
    }
    return j;
}

bool parseKey(std::string_view suffix, core::GlobalPos& pos) {
    int  v[4]{};
    int  n = 0;
    auto s = std::string(suffix);
    std::size_t start = 0;
    while (n < 4) {
        auto end = s.find(':', start);
        try {
            v[n++] = std::stoi(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
        } catch (...) {
            return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (n != 4) return false;
    pos = {v[0], v[1], v[2], v[3]};
    return true;
}

void fromJson(Record& r, nlohmann::json const& j) {
    r.rotation    = j.value("rotation", 0) & 3;
    r.yaw         = j.value("yaw", 0.f);
    r.powered     = j.value("powered", false);
    r.sheared     = j.value("sheared", false);
    r.name        = j.value("name", std::string());
    r.item        = j.value("item", std::string());
    r.aux         = j.value("aux", 0);
    r.progress    = std::clamp(j.value("progress", 0.f), 0.f, 1.f);
    r.windCharged = j.value("charged", 0);
    r.power       = j.value("power", 0);
}

void loadWorld() {
    gRecords.clear();
    gChanged.clear();
    gErased.clear();
    gNext.clear();
    gLastUse.clear();
    gPlacingName.clear();
    gLastClock.clear();
    globe::resetSeedCache();
    if (!game::db::available()) return;
    std::vector<std::string> keys;
    game::db::forEachKey(std::string(kKeyPrefix), [&](std::string_view suffix) { keys.emplace_back(suffix); });
    for (auto const& suffix : keys) {
        core::GlobalPos pos;
        if (!parseKey(suffix, pos)) continue;
        auto j = game::db::load(std::string(kKeyPrefix) + suffix);
        if (!j) continue;
        Record r;
        r.kind = static_cast<Kind>(j->value("kind", 0));
        if (r.kind == Kind::None || idOf(r.kind).empty()) continue;
        r.pos = pos;
        fromJson(r, *j);
        r.dirty = false;
        gRecords[pos] = std::move(r);
    }
    globe::loadSeed();
    installComparators();
    gLoaded = true;
    log().info("instrument: {} blocks loaded", gRecords.size());
}

void saveWorld() {
    if (!gLoaded || !game::db::available()) return;
    for (auto const& pos : gErased) {
        if (!gRecords.contains(pos)) game::db::erase(dbKey(pos));
    }
    gErased.clear();
    for (auto& [pos, rec] : gRecords) {
        if (!rec.dirty) continue;
        game::db::save(dbKey(pos), toJson(rec));
        rec.dirty = false;
    }
}

void closeWorld() {
    gRecords.clear();
    gChanged.clear();
    gErased.clear();
    gNext.clear();
    gLoaded = false;
    globe::resetSeedCache();
}

/// The block was removed (broken, exploded, replaced): drop the contents and forget the record.
void removed(Record& rec, BlockSource& region) {
    auto pos = game::blockPos(rec.pos);
    if (rec.kind == Kind::Hourglass && !rec.item.empty()) {
        dropItem(region, pos, rec.item, 1, rec.aux);
        log().info("instrument: hourglass at {} broken, dropped {}", rec.pos.key(), rec.item);
    }
    if ((rec.kind == Kind::Globe || rec.kind == Kind::GlobeSepia) && !rec.name.empty()) {
        // The loot table dropped an unnamed globe when the block broke (the previous tick): give it back
        // its name (Java: copy_name). Only items still inside the block cell count.
        auto  id = idOf(rec.kind);
        AABB  box{
            Vec3{static_cast<float>(pos.x) - 0.25f, static_cast<float>(pos.y) - 0.25f, static_cast<float>(pos.z) - 0.25f},
            Vec3{static_cast<float>(pos.x) + 1.25f, static_cast<float>(pos.y) + 1.25f, static_cast<float>(pos.z) + 1.25f}
        };
        for (auto* a : region.getEntities(box, 0.f, ActorType::ItemEntity)) {
            auto& item = static_cast<ItemActor*>(a)->item();
            if (item.getTypeName() == id && item.getCustomName().empty()) {
                item.setCustomName(Bedrock::Safety::RedactableString(rec.name));
                break;
            }
        }
    }
    core::GlobalPos const key = rec.pos;
    gErased.push_back(key);
    gRecords.erase(key);
}

void processChanged() {
    if (gChanged.empty()) return;
    auto changed = std::move(gChanged);
    gChanged.clear();
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    for (auto const& gp : changed) {
        auto* region = regionOf(gp.dim);
        if (!region) continue;
        auto pos = game::blockPos(gp);
        if (!region->hasChunksAt(pos, 0, false)) continue;
        auto const& block = region->getBlock(pos);
        auto        kind  = kindOf(block);
        auto        it    = gRecords.find(gp);
        if (it != gRecords.end() && it->second.kind != kind) removed(it->second, *region);
        if (kind != Kind::None) (void)ensure(*region, pos);
    }
}

void dispatch(Record& rec, BlockSource& region, BlockPos const& pos, Block const& block) {
    switch (rec.kind) {
    case Kind::Globe:
    case Kind::GlobeSepia:
        globe::tick(rec, region, pos, block);
        break;
    case Kind::Clock:
        clock::tick(rec, region, pos, block);
        break;
    case Kind::Hourglass:
        hourglass::tick(rec, region, pos, block);
        break;
    case Kind::WindVane:
        windvane::tick(rec, region, pos, block);
        break;
    default:
        break;
    }
}

void onTick() {
    ++gTick;
    if (!gLoaded) {
        if (!game::db::available()) return;
        loadWorld();
    }
    if (!gNext.empty()) {
        auto work = std::move(gNext);
        gNext.clear();
        for (auto& fn : work) {
            try {
                fn();
            } catch (std::exception const& e) {
                log().error("instrument: deferred action failed: {}", e.what());
            }
        }
    }
    processChanged();
    std::map<int, BlockSource*> regions;
    for (auto& [gp, rec] : gRecords) {
        if (!enabled(rec.kind)) continue;
        auto rit = regions.find(gp.dim);
        if (rit == regions.end()) rit = regions.emplace(gp.dim, regionOf(gp.dim)).first;
        auto* region = rit->second;
        if (!region) continue;
        auto pos = game::blockPos(gp);
        if (!region->hasChunksAt(pos, 0, false)) continue;
        auto const& block = region->getBlock(pos);
        if (kindOf(block) != rec.kind) {
            gChanged.push_back(gp);
            continue;
        }
        dispatch(rec, *region, pos, block);
    }
}
} // namespace

std::int64_t playerKey(Player const& p) { return p.getOrCreateUniqueID().rawID; }

Player* playerByKey(std::int64_t key) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = key;
    return level->getPlayer(id);
}

/// Removes one item from the player's selected stack (not in creative).
void consumeSelected(Player& player) {
    if (player.isCreative()) return;
    ItemStack stack = player.getSelectedItem();
    if (stack.isNull()) return;
    stack.remove(1);
    if (stack.mCount == 0) stack = ItemStack();
    player.setSelectedItem(stack);
    player.sendInventory(false);
}

namespace {
/// A vanilla clock used on anything: show the time (once per click; the client repeats while held and
/// may send both a block and an air use for the same click).
void clockItemUsed(Player& player) {
    if (!config().clockRightClick) return;
    auto& last = gLastClock[playerKey(player)];
    if (gTick - last < 4) return;
    last = gTick;
    clock::showTime(player, player.getDimensionBlockSource());
}

int facingId(std::string const& dir) {
    if (dir == "down") return 0;
    if (dir == "up") return 1;
    if (dir == "north") return 2;
    if (dir == "south") return 3;
    if (dir == "west") return 4;
    if (dir == "east") return 5;
    return -1;
}

int oppositeFace(int f) { return f < 0 ? -1 : (f ^ 1); }

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto&        player = ev.self();
    auto&        item   = ev.item();
    auto         itemId = item.isNull() ? std::string() : item.getTypeName();
    Block const* block  = ev.block() ? ev.block().as_ptr() : &player.getDimensionBlockSource().getBlock(ev.blockPos());
    auto kind = kindOf(*block);
    if (kind == Kind::None) {
        if (itemId == "minecraft:clock") clockItemUsed(player);
        return;
    }
    if (!enabled(kind)) return;
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.blockPos();
    auto* rec    = ensure(region, pos);
    if (!rec) return;
    bool handEmpty     = item.isNull();
    bool sneakWithItem = player.isSneaking() && !handEmpty;

    // the client repeats the interaction while the button is held; Java does too (every 4 ticks)
    auto  key  = playerKey(player);
    auto& last = gLastUse[key];
    if (last.second == rec->pos && gTick - last.first < 4) {
        if (kind != Kind::WindVane && !sneakWithItem) ev.cancel();
        return;
    }

    auto gpos = rec->pos;
    switch (kind) {
    case Kind::Globe:
    case Kind::GlobeSepia: {
        if (sneakWithItem) return;
        bool shears = itemId == "minecraft:shears";
        ev.cancel();
        last = {gTick, gpos};
        nextTick([key, gpos, shears] {
            auto* p = playerByKey(key);
            auto* r = find(gpos);
            auto* region = regionOf(gpos.dim);
            if (!p || !r || !region) return;
            globe::use(*r, *p, *region, game::blockPos(gpos), shears);
        });
        return;
    }
    case Kind::Clock: {
        if (sneakWithItem) return;
        ev.cancel();
        last          = {gTick, gpos};
        int  facing   = facingId(stringState(*block, "minecraft:cardinal_direction"));
        bool twoFaced = state(*block, "complementaries:two_faced") == 1;
        if (itemId == kClock && !twoFaced && static_cast<int>(ev.face()) == oppositeFace(facing)) {
            nextTick([key, gpos] {
                auto* p      = playerByKey(key);
                auto* region = regionOf(gpos.dim);
                if (!p || !region) return;
                auto  pos   = game::blockPos(gpos);
                auto const& b = region->getBlock(pos);
                if (kindOf(b) != Kind::Clock || state(b, "complementaries:two_faced") == 1) return;
                auto const& held = p->getSelectedItem();
                if (held.isNull() || held.getTypeName() != kClock) return; // switched items meanwhile
                if (apply(*region, pos, b, withStates(b, {{"complementaries:two_faced", 1}}))) {
                    consumeSelected(*p);
                    playSound(*region, pos, "dig.wood", 1.f, 0.8f);
                    log().info("instrument: clock at {} is now two-faced", gpos.key());
                }
            });
            return;
        }
        clock::showTime(player, region);
        return;
    }
    case Kind::Hourglass:
        if (hourglass::use(*rec, player, region, pos, *block)) {
            ev.cancel();
            last = {gTick, gpos};
        }
        return;
    default:
        return;
    }
}

void onUseItem(ll::event::PlayerUseItemEvent& ev) {
    auto& item = ev.item();
    if (item.isNull()) return;
    auto  id     = item.getTypeName();
    auto& player = ev.self();
    if (id == kAltimeter) {
        if (!config().altimeter || !config().altimeterClickAction) return;
        auto y = static_cast<int>(std::floor(player.getFeetPos().y + 0.001f));
        actionBar(player, trFor(player, "message.supplementaries.altimeter", "Y: {}", y));
        log().info("instrument: altimeter used by {} at y {}", player.getRealName(), y);
    } else if (id == "minecraft:clock") {
        clockItemUsed(player);
    }
}

void onPlacing(ll::event::PlayerPlacingBlockEvent& ev) {
    auto const& item = ev.self().getSelectedItem();
    if (item.isNull()) return;
    auto id = item.getTypeName();
    if (id != kGlobe && id != kGlobeSepia) return;
    gPlacingName[playerKey(ev.self())] = {gTick, item.getCustomName()};
}

void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto kind = kindOf(ev.placedBlock());
    if (kind != Kind::Globe && kind != Kind::GlobeSepia) return;
    auto it = gPlacingName.find(playerKey(ev.self()));
    if (it == gPlacingName.end()) return;
    auto name = it->second.second;
    gPlacingName.erase(it);
    auto& region = ev.self().getDimensionBlockSource();
    if (auto* rec = ensure(region, ev.pos())) {
        rec->name  = name;
        rec->skin  = -1;
        rec->dirty = true;
        if (!name.empty()) log().info("instrument: placed globe named '{}' at {}", name, rec->pos.key());
    }
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    auto before = kindOf(ev.previousBlock());
    auto after  = kindOf(ev.newBlock());
    if (before == Kind::None && after == Kind::None) return;
    if (before == after) return; // our own state changes
    gChanged.push_back(game::globalPos(region, ev.pos()));
}

struct WorldHooks {
    WorldHooks() {
        game::db::onWorldReady([] { loadWorld(); });
        game::db::onWorldSave([] { saveWorld(); });
        game::db::onWorldClosed([] { closeWorld(); });
    }
};
WorldHooks gWorldHooks;

} // namespace

Record* find(core::GlobalPos const& pos) {
    auto it = gRecords.find(pos);
    return it == gRecords.end() ? nullptr : &it->second;
}

Record* ensure(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    auto        kind  = kindOf(block);
    if (kind == Kind::None) return nullptr;
    auto gp = game::globalPos(region, pos);
    auto it = gRecords.find(gp);
    if (it != gRecords.end()) {
        if (it->second.kind == kind) return &it->second;
        removed(it->second, region);
    }
    Record r;
    r.kind  = kind;
    r.pos   = gp;
    r.dirty = true;
    std::erase_if(gErased, [&](core::GlobalPos const& e) { return e == gp; });
    auto& out = gRecords[gp] = std::move(r);
    return &out;
}

std::size_t recordCount() { return gRecords.size(); }

void forEach(std::function<void(Record&)> const& fn) {
    for (auto& [pos, rec] : gRecords) fn(rec);
}

void nextTick(std::function<void()> fn) { gNext.push_back(std::move(fn)); }

std::int64_t gameTick() { return gTick; }

// ---- module -----------------------------------------------------------------------------------------
class InstrumentModule {
public:
    static InstrumentModule& get() {
        static InstrumentModule m;
        return m;
    }

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](auto& ev) { onInteract(ev); }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>([](auto& ev) { onUseItem(ev); }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>([](auto& ev) { onPlacing(ev); }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](auto& ev) { onPlaced(ev); }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](auto& ev) { onBlockChanged(ev); }));
        if (game::db::available() && !gLoaded) loadWorld();
        installComparators();
        windvane::installHooks();
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        windvane::uninstallHooks();
        saveWorld();
        uninstallComparators();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace cm::feature::instrument

CM_REGISTER_MODULE(cm::feature::instrument::InstrumentModule);

// ---- commands -------------------------------------------------------------------------------------------
CM_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace cm::feature::instrument;
    cmd.overload().text("globe").text("newseed").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!cm::commands::isAdmin(origin)) return output.error("You need operator permission.");
        static std::mt19937_64 rng{std::random_device{}()};
        globe::setSeed(rng(), true);
        cm::log().info("instrument: globe seed changed");
        output.success(cm::tr("commands.supplementaries.globe_changed", "Globe seed changed"));
    });
    cmd.overload().text("globe").text("resetseed").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!cm::commands::isAdmin(origin)) return output.error("You need operator permission.");
        globe::setSeed(0, false);
        cm::log().info("instrument: globe seed reset");
        output.success(cm::tr("commands.supplementaries.globe_reset", "Globe seed reset"));
    });
});
