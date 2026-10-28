// U12 display: pedestal, item shelf and statue blocks (server side, both platforms).

#include "feature/display/Display.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/display/Internal.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("display", cm::feature::display::DisplayConfig);

namespace cm::feature::display {

namespace db = game::db;
using core::GlobalPos;

namespace {

constexpr int kReconcileInterval = 20; ///< ticks between helper entity checks
constexpr int kRedstoneInterval  = 4;  ///< ticks between statue power polls
constexpr int kUseCooldown       = 4;  ///< ignore repeated use packets of one click

struct Slot {
    Kind      kind = Kind::None;
    ItemStack item;
    [[nodiscard]] bool hasItem() const { return !item.isNull(); }
};

/// What was last sent to a helper entity (avoids re-sending properties every reconcile).
struct EntityState {
    int64       entity  = 0;
    int         mode    = -1;
    int         pose    = -1;
    int         facing  = -1;
    int         waving  = -1;
    std::string itemKey = "\x01"; ///< SNBT of the carried item ("" = empty)
};

struct PendingUse {
    ActorUniqueID player;
    GlobalPos     pos;
};

struct State {
    std::map<GlobalPos, Slot>        slots;
    std::map<GlobalPos, EntityState> entities;
    std::set<GlobalPos>              dirty;   ///< positions to re-evaluate next tick
    std::set<GlobalPos>              shelves; ///< shelves to check for support
    std::set<GlobalPos>              removed; ///< display blocks replaced by something else
    std::vector<PendingUse>          uses;
    std::map<int64, int>             lastUse; ///< player unique id -> tick of the last use
    BlockType const*                 types[4]{};
    int                              tick = 0;
};

State& st() {
    static State s;
    return s;
}

std::string dbKey(GlobalPos const& p) { return "cm:display:" + p.key(); }

/// Parses "<dim>:<x>:<y>:<z>" (GlobalPos::key()).
bool parseKey(std::string_view s, GlobalPos& p) {
    int* out[4] = {&p.dim, &p.x, &p.y, &p.z};
    for (int i = 0; i < 4; ++i) {
        auto end = i < 3 ? s.find(':') : s.size();
        if (end == std::string_view::npos) return false;
        auto part = s.substr(0, end);
        auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), *out[i]);
        if (ec != std::errc{} || ptr != part.data() + part.size()) return false;
        s.remove_prefix(i < 3 ? end + 1 : end);
    }
    return true;
}

void saveSlot(GlobalPos const& p, Slot const& s) {
    if (!db::available()) return;
    db::save(dbKey(p), {{"kind", static_cast<int>(s.kind)}, {"item", itemToSnbt(s.item)}});
}

void eraseSlot(GlobalPos const& p) {
    st().slots.erase(p);
    if (db::available()) db::erase(dbKey(p));
}

void resolveTypes() {
    auto& s = st();
    for (auto kind : {Kind::Pedestal, Kind::Shelf, Kind::Statue}) {
        auto type = BlockType::tryGetFromRegistry(HashedString(std::string(blockIdOf(kind))));
        s.types[static_cast<int>(kind)] = type ? &*type : nullptr;
    }
}

void loadAll() {
    auto& s = st();
    s.slots.clear();
    s.entities.clear();
    s.dirty.clear();
    s.shelves.clear();
    s.removed.clear();
    s.uses.clear();
    s.lastUse.clear();
    resolveTypes();
    if (!db::available()) return;
    std::vector<std::string> keys;
    db::forEachKey("cm:display:", [&keys](std::string_view suffix) { keys.emplace_back(suffix); });
    for (auto const& suffix : keys) {
        GlobalPos p;
        if (!parseKey(suffix, p)) continue;
        auto j = db::load("cm:display:" + suffix);
        if (!j) continue;
        Slot slot;
        slot.kind = static_cast<Kind>(j->value("kind", 0));
        if (auto item = itemFromSnbt(j->value("item", std::string{}))) slot.item = *item;
        if (slot.kind == Kind::None) continue;
        s.slots[p] = std::move(slot);
    }
    log().info("Display: {} display block(s) loaded", s.slots.size());
}

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto dimension = level->getDimension(dim).lock();
    if (!dimension) return nullptr;
    return &dimension->getBlockSourceFromMainChunkSource();
}

bool loaded(BlockSource& region, BlockPos const& pos) { return region.hasChunksAt(pos, 0, false); }

Block const* withStates(Block const& block, std::vector<std::pair<std::string_view, int>> const& states) {
    Block const* b = &block;
    for (auto const& [name, value] : states) {
        if (auto const* state = b->getBlockType().getBlockState(HashedString(std::string(name)))) {
            if (auto nb = b->setState(*state, value)) b = nb.as_ptr();
        }
    }
    return b;
}

// ---- redstone -------------------------------------------------------------------------------------

/// True if the block at `n` powers its neighbours (direct sources only; see the spec).
bool emitsPower(Block const& b) {
    auto const& id = b.getTypeName();
    auto        st = [&b](char const* name) { return game::intState(b, name).value_or(0); };
    if (id == "minecraft:redstone_block" || id == "minecraft:redstone_torch") return true;
    if (id == "minecraft:powered_repeater" || id == "minecraft:powered_comparator") return true;
    if (id == "minecraft:lever") return st("open_bit") != 0;
    if (id.ends_with("_button")) return st("button_pressed_bit") != 0;
    if (id == "minecraft:observer" || id == "minecraft:tripwire_hook" || id == "minecraft:lectern")
        return st("powered_bit") != 0;
    if (id == "minecraft:detector_rail") return st("rail_data_bit") != 0;
    if (id == "minecraft:redstone_wire" || id.ends_with("pressure_plate") || id == "minecraft:daylight_detector"
        || id == "minecraft:daylight_detector_inverted" || id == "minecraft:target")
        return st("redstone_signal") > 0;
    return false;
}

bool hasNeighborSignal(BlockSource& region, BlockPos const& pos) {
    static constexpr int d[6][3] = {
        {0, -1, 0},
        {0, 1,  0},
        {0, 0,  -1},
        {0, 0,  1 },
        {-1, 0, 0},
        {1, 0,  0},
    };
    for (auto const& o : d) {
        BlockPos n{pos.x + o[0], pos.y + o[1], pos.z + o[2]};
        if (emitsPower(region.getBlock(n))) return true;
    }
    return false;
}

// ---- helper entity --------------------------------------------------------------------------------

int facing16(Kind kind, Block const& block) {
    switch (kind) {
    case Kind::Pedestal: {
        auto dir = stringState(block, "minecraft:cardinal_direction");
        return (dir == "east" || dir == "west") ? 4 : 0; // AXIS X: rotated by 90 degrees
    }
    case Kind::Shelf: {
        // Java: yaw = -facing.opposite().toYRot() -> N 0, W 90, S 180, E 270 (in 22.5 degree steps)
        int f = blockFacing(block);
        return (((2 - f) * 4) % 16 + 16) % 16;
    }
    case Kind::Statue:
        return blockFacing(block) * 4; // the yaw of its facing
    default:
        return 0;
    }
}

DisplayMode modeFor(Kind kind, ItemStack const& item) {
    auto const& cfg = config::get<DisplayConfig>();
    switch (kind) {
    case Kind::Pedestal: {
        auto type = pedestalTypeOf(item);
        if (cfg.pedestalSpecial) {
            if (type == PedestalType::Sword) return DisplayMode::PedestalSword;
            if (type == PedestalType::Trident) return DisplayMode::PedestalTrident;
            if (type == PedestalType::Crystal) return DisplayMode::PedestalCrystal;
        }
        bool block = item.mBlock != nullptr;
        if (cfg.pedestalSpin) return block ? DisplayMode::PedestalBlock : DisplayMode::PedestalSpin;
        return block ? DisplayMode::PedestalBlockStill : DisplayMode::PedestalStill;
    }
    case Kind::Shelf:
        return (cfg.shelfTranslate && item.mBlock != nullptr) ? DisplayMode::ShelfBlock : DisplayMode::Shelf;
    case Kind::Statue:
        return DisplayMode::Statue;
    default:
        return DisplayMode::None;
    }
}

std::vector<Actor*> displayEntitiesAt(BlockSource& region, BlockPos const& pos) {
    AABB box{
        Vec3{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)},
        Vec3{static_cast<float>(pos.x + 1), static_cast<float>(pos.y + 1), static_cast<float>(pos.z + 1)}
    };
    std::vector<Actor*> out;
    for (auto* a : region.getEntities(box, 0.0f)) {
        if (a && a->getTypeName() == kDisplayEntity && a->isAlive()) out.push_back(a);
    }
    return out;
}

void removeEntities(BlockSource& region, BlockPos const& pos) {
    for (auto* a : displayEntitiesAt(region, pos)) a->despawn();
}

void syncEntity(BlockSource& region, BlockPos const& pos, Kind kind, Slot const* slot) {
    auto&     s      = st();
    auto      gp     = game::globalPos(region, pos);
    bool      wanted = kind == Kind::Statue || (slot && slot->hasItem());
    auto      list   = displayEntitiesAt(region, pos);
    Actor*    entity = list.empty() ? nullptr : list.front();
    for (size_t i = 1; i < list.size(); ++i) list[i]->despawn();
    if (!wanted) {
        if (entity) entity->despawn();
        s.entities.erase(gp);
        return;
    }
    if (!entity) {
        ActorUniqueID id{};
        Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + 0.5f};
        entity = CommandUtils::spawnEntityAt(region, at, ActorDefinitionIdentifier(std::string(kDisplayEntity)), id, nullptr);
        if (!entity) return;
    }
    auto& es = s.entities[gp];
    if (es.entity != entity->getOrCreateUniqueID().rawID) es = EntityState{entity->getOrCreateUniqueID().rawID};

    ItemStack const empty;
    ItemStack const& item  = slot ? slot->item : empty;
    Block const&     block = region.getBlock(pos);
    int mode   = static_cast<int>(modeFor(kind, item));
    int pose   = static_cast<int>(statuePoseOf(item));
    int facing = facing16(kind, block);
    int waving = kind == Kind::Statue ? game::intState(block, "complementaries:powered").value_or(0) : 0;
    if (es.mode != mode) entityEvent(*entity, "mode_" + std::to_string(es.mode = mode));
    if (es.pose != pose) entityEvent(*entity, "pose_" + std::to_string(es.pose = pose));
    if (es.facing != facing) entityEvent(*entity, "facing_" + std::to_string(es.facing = facing));
    if (es.waving != waving) entityEvent(*entity, "waving_" + std::to_string(es.waving = waving));
    auto key = itemToSnbt(item);
    if (es.itemKey != key) {
        entity->setCarriedItem(item.isNull() ? ItemStack::EMPTY_ITEM() : item);
        es.itemKey = key;
    }
}

// ---- block states ---------------------------------------------------------------------------------

void updatePedestal(BlockSource& region, BlockPos const& pos, Block const& block, bool hasItem) {
    BlockPos above{pos.x, pos.y + 1, pos.z};
    BlockPos below{pos.x, pos.y - 1, pos.z};
    auto     status = pedestalStatusFor(region, pos, hasItem);
    // Java canConnectTo: up to a pedestal above unless holding an item; down to a pedestal below
    // that cannot hold an item (status none).
    bool up = kindOf(region.getBlock(above)) == Kind::Pedestal && !hasItem;
    auto const& lower = region.getBlock(below);
    bool down = kindOf(lower) == Kind::Pedestal && game::intState(lower, "complementaries:item_status").value_or(1) == 0;
    auto const* nb = withStates(
        block,
        {{"complementaries:item_status", static_cast<int>(status)},
         {"complementaries:up", up ? 1 : 0},
         {"complementaries:down", down ? 1 : 0}}
    );
    if (nb != &block) game::setBlock(region, pos, *nb);
}

void updateStatue(BlockSource& region, BlockPos const& pos, Block const& block, ItemStack const& item) {
    bool lit     = statuePoseOf(item) == StatuePose::Candle;
    bool powered = hasNeighborSignal(region, pos);
    auto const* nb = withStates(block, {{"complementaries:lit", lit ? 1 : 0}, {"complementaries:powered", powered ? 1 : 0}});
    if (nb != &block) game::setBlock(region, pos, *nb);
}

void dropContents(BlockSource& region, GlobalPos const& gp) {
    auto it = st().slots.find(gp);
    if (it == st().slots.end()) return;
    if (it->second.hasItem()) {
        dropItem(region, static_cast<float>(gp.x) + 0.5f, static_cast<float>(gp.y) + 0.5f, static_cast<float>(gp.z) + 0.5f, it->second.item);
    }
    eraseSlot(gp);
}

void handleRemoved(GlobalPos const& gp) {
    auto* region = regionOf(gp.dim);
    if (!region) return;
    auto pos = game::blockPos(gp);
    if (!loaded(*region, pos)) return;
    auto it = st().slots.find(gp);
    Kind now = kindOf(region->getBlock(pos));
    if (it != st().slots.end() && it->second.kind == now) return; // put back in the meantime
    dropContents(*region, gp);
    removeEntities(*region, pos);
    st().entities.erase(gp);
}

void checkShelf(GlobalPos const& gp) {
    auto* region = regionOf(gp.dim);
    if (!region) return;
    auto pos = game::blockPos(gp);
    if (!loaded(*region, pos)) return;
    auto const& block = region->getBlock(pos);
    if (kindOf(block) != Kind::Shelf) return;
    // Java canSurvive: the block behind (opposite of facing) must not be air.
    auto [dx, dz] = game::facingOffset(blockFacing(block));
    BlockPos behind{pos.x - dx, pos.y, pos.z - dz};
    if (!region->getBlock(behind).isAir()) return;
    game::clearBlock(*region, pos); // fires BlockChangedEvent -> contents dropped
    dropItem(*region, static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f, makeItem(kItemShelf));
}

void processUse(PendingUse const& u) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto* player = level->getPlayer(u.player);
    if (!player) return;
    auto& region = player->getDimensionBlockSource();
    if (static_cast<int>(region.getDimensionId()) != u.pos.dim) return;
    use(*player, region, game::blockPos(u.pos));
}

// ---- module -----------------------------------------------------------------------------------------

class DisplayModule {
public:
    static DisplayModule& get() {
        static DisplayModule m;
        return m;
    }

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mInteract = bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [](ll::event::PlayerInteractBlockEvent& ev) { onInteract(ev); },
            ll::event::EventPriority::High
        );
        mChanged = bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) { onChanged(ev); });
        mTick    = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) { onTick(); });
        db::onWorldReady([] {
            loadAll();
            detail::onWorldReady();
        });
        db::onWorldClosed([] {
            detail::onWorldClosed();
            st().slots.clear();
            st().entities.clear();
        });
        if (db::available()) {
            loadAll();
            detail::onWorldReady();
        }
        detail::installHooks();
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        if (mInteract) bus.removeListener(mInteract);
        if (mChanged) bus.removeListener(mChanged);
        if (mTick) bus.removeListener(mTick);
        mInteract = mChanged = mTick = nullptr;
        detail::uninstallHooks();
    }

private:
    static void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.getLevel().isClientSide()) return;
        // ev.block() is not always filled in: read the block from the world.
        auto& region = player.getDimensionBlockSource();
        Block const* block = &region.getBlock(ev.blockPos());
        auto& s      = st();
        Kind  kind   = kindOf(*block);
        auto const& hand = ev.item().isNull() ? player.getSelectedItem() : ev.item();
        if (kind == Kind::None) {
            if (itemId(hand) == kHatStand) {
                ev.cancel();
                detail::queueHatStandPlacement(player, ev.blockPos(), static_cast<int>(ev.face()));
            }
            return;
        }
        if (kind == Kind::Pedestal && game::intState(*block, "complementaries:item_status").value_or(1) == 0) return;
        // Secondary use (sneaking) with an item uses the item (e.g. places a block) as in Java.
        if (player.isSneaking() && !hand.isNull()) return;
        auto gp  = game::globalPos(region, ev.blockPos());
        auto it  = s.slots.find(gp);
        bool has = it != s.slots.end() && it->second.hasItem();
        if (!has && hand.isNull()) return;
        ev.cancel();
        auto  id   = player.getOrCreateUniqueID();
        auto& last = s.lastUse[id.rawID];
        if (s.tick - last < kUseCooldown && last != 0) return;
        last = s.tick;
        s.uses.push_back({id, gp});
    }

    static void onChanged(ll::event::BlockChangedEvent& ev) {
        if (ev.layer() != 0) return;
        auto& region = ev.blockSource();
        if (region.getLevel().isClientSide()) return;
        auto& s       = st();
        Kind  oldKind = kindOf(ev.previousBlock());
        Kind  newKind = kindOf(ev.newBlock());
        auto const& pos = ev.pos();
        auto  gp      = game::globalPos(region, pos);
        if (oldKind != Kind::None && oldKind != newKind) s.removed.insert(gp);
        if (newKind != Kind::None) s.dirty.insert(gp);
        // A pedestal above / below has to re-evaluate its connections and status.
        for (int dy : {-1, 1}) {
            BlockPos n{pos.x, pos.y + dy, pos.z};
            if (kindOf(region.getBlock(n)) == Kind::Pedestal) s.dirty.insert(game::globalPos(region, n));
        }
        // An item shelf whose support became air pops off.
        if (ev.newBlock().isAir()) {
            for (int f = 0; f < 4; ++f) {
                auto [dx, dz] = game::facingOffset(f);
                BlockPos n{pos.x + dx, pos.y, pos.z + dz};
                if (kindOf(region.getBlock(n)) == Kind::Shelf) s.shelves.insert(game::globalPos(region, n));
            }
        }
    }

    static void onTick() {
        auto& s = st();
        ++s.tick;
        if (!s.uses.empty()) {
            auto uses = std::move(s.uses);
            s.uses.clear();
            for (auto const& u : uses) processUse(u);
        }
        if (!s.removed.empty()) {
            auto removed = std::move(s.removed);
            s.removed.clear();
            for (auto const& gp : removed) handleRemoved(gp);
        }
        if (!s.dirty.empty()) {
            auto dirty = std::move(s.dirty);
            s.dirty.clear();
            for (auto const& gp : dirty) {
                if (auto* region = regionOf(gp.dim)) {
                    auto pos = game::blockPos(gp);
                    if (loaded(*region, pos)) refresh(*region, pos);
                }
            }
        }
        if (!s.shelves.empty()) {
            auto shelves = std::move(s.shelves);
            s.shelves.clear();
            for (auto const& gp : shelves) checkShelf(gp);
        }
        if (s.tick % kRedstoneInterval == 0) pollStatues();
        if (s.tick % kReconcileInterval == 0) reconcile();
        detail::tickHatStands(s.tick);
    }

    static void pollStatues() {
        for (auto const& [gp, slot] : st().slots) {
            if (slot.kind != Kind::Statue) continue;
            auto* region = regionOf(gp.dim);
            if (!region) continue;
            auto pos = game::blockPos(gp);
            if (!loaded(*region, pos)) continue;
            auto const& block = region->getBlock(pos);
            if (kindOf(block) != Kind::Statue) continue;
            bool powered = hasNeighborSignal(*region, pos);
            if (powered != (game::intState(block, "complementaries:powered").value_or(0) != 0)) refresh(*region, pos);
        }
    }

    static void reconcile() {
        std::vector<GlobalPos> gone;
        for (auto const& [gp, slot] : st().slots) {
            auto* region = regionOf(gp.dim);
            if (!region) continue;
            auto pos = game::blockPos(gp);
            if (!loaded(*region, pos)) continue;
            Kind now = kindOf(region->getBlock(pos));
            if (now != slot.kind) {
                gone.push_back(gp);
                continue;
            }
            syncEntity(*region, pos, slot.kind, &slot);
        }
        for (auto const& gp : gone) handleRemoved(gp);
    }

    ll::event::ListenerPtr mInteract;
    ll::event::ListenerPtr mChanged;
    ll::event::ListenerPtr mTick;
};

} // namespace

// ---- public API -------------------------------------------------------------------------------------

int currentTick() { return st().tick; }

Kind kindOf(Block const& block) {
    auto const& s    = st();
    auto const* type = &block.getBlockType();
    if (!s.types[1] && !s.types[2] && !s.types[3]) {
        // Not resolved yet (no world ready): compare by name.
        auto const& id = block.getTypeName();
        if (id == kPedestal) return Kind::Pedestal;
        if (id == kItemShelf) return Kind::Shelf;
        if (id == kStatue) return Kind::Statue;
        return Kind::None;
    }
    if (type == s.types[1]) return Kind::Pedestal;
    if (type == s.types[2]) return Kind::Shelf;
    if (type == s.types[3]) return Kind::Statue;
    return Kind::None;
}

std::string_view blockIdOf(Kind kind) {
    switch (kind) {
    case Kind::Pedestal:
        return kPedestal;
    case Kind::Shelf:
        return kItemShelf;
    case Kind::Statue:
        return kStatue;
    default:
        return {};
    }
}

bool isSwordId(std::string_view id) { return id.ends_with("_sword"); }

bool isToolId(std::string_view id) {
    return id.ends_with("_pickaxe") || id.ends_with("_axe") || id.ends_with("_shovel") || id.ends_with("_hoe")
        || id == "minecraft:mace" || id == "complementaries:wrench";
}

bool isCandleId(std::string_view id) { return id == "minecraft:candle" || id.ends_with("_candle"); }

PedestalType pedestalTypeOf(ItemStack const& item) {
    auto id = itemId(item);
    if (isSwordId(id)) return PedestalType::Sword;
    if (id == "minecraft:trident") return PedestalType::Trident;
    if (id == "minecraft:end_crystal") return PedestalType::Crystal;
    if (!item.isNull() && item.mBlock != nullptr) return PedestalType::Block;
    return PedestalType::Item;
}

StatuePose statuePoseOf(ItemStack const& item) {
    if (item.isNull()) return StatuePose::Standing;
    auto id = itemId(item);
    if (isSwordId(id)) return StatuePose::Sword;
    if (isToolId(id)) return StatuePose::Tool;
    if (isCandleId(id)) return StatuePose::Candle;
    return StatuePose::Holding;
}

std::string stringState(Block const& block, std::string_view name) {
    CompoundTag const& ser = block.mSerializationId;
    if (!ser.contains("states", Tag::Type::Compound)) return {};
    auto const& states = ser.at("states").get<CompoundTag>();
    if (!states.contains(name, Tag::Type::String)) return {};
    return std::string(states.at(name).get<StringTag>());
}

int blockFacing(Block const& block) {
    auto toJava = [](std::string const& d) {
        if (d == "south") return 0;
        if (d == "west") return 1;
        if (d == "east") return 3;
        return 2; // north (also up / down for shelves)
    };
    switch (kindOf(block)) {
    case Kind::Shelf:
        return toJava(stringState(block, "minecraft:block_face"));
    case Kind::Statue:
    case Kind::Pedestal:
        // cardinal_direction is the direction the placer looked at; the block faces the placer.
        return game::opposite(toJava(stringState(block, "minecraft:cardinal_direction")));
    default:
        return 0;
    }
}

PedestalStatus pedestalStatusFor(BlockSource& region, BlockPos const& pos, bool hasItem) {
    if (hasItem) return PedestalStatus::Full;
    BlockPos     above{pos.x, pos.y + 1, pos.z};
    Block const& b = region.getBlock(above);
    // Java canHaveItemAbove: no pedestal and no redstone conductor (full solid block) on top.
    if (kindOf(b) == Kind::Pedestal) return PedestalStatus::None;
    if (!b.isAir() && b._isSolid()) return PedestalStatus::None;
    return PedestalStatus::Empty;
}

std::optional<ItemStack> displayedItem(GlobalPos const& pos) {
    auto it = st().slots.find(pos);
    if (it == st().slots.end() || !it->second.hasItem()) return std::nullopt;
    return it->second.item;
}

int comparatorSignal(GlobalPos const& pos) { return displayedItem(pos) ? 15 : 0; }

Actor* displayEntityAt(BlockSource& region, BlockPos const& pos) {
    auto list = displayEntitiesAt(region, pos);
    return list.empty() ? nullptr : list.front();
}

void refresh(BlockSource& region, BlockPos const& pos) {
    auto& s     = st();
    auto  gp    = game::globalPos(region, pos);
    auto const& block = region.getBlock(pos);
    Kind  kind  = kindOf(block);
    if (kind == Kind::None) {
        removeEntities(region, pos);
        s.entities.erase(gp);
        return;
    }
    auto it = s.slots.find(gp);
    if (it != s.slots.end() && it->second.kind != kind) {
        handleRemoved(gp);
        it = s.slots.end();
    }
    if (kind == Kind::Statue && it == s.slots.end()) {
        // A new statue: register it so its figure is drawn and its power polled.
        it = s.slots.emplace(gp, Slot{Kind::Statue, {}}).first;
        saveSlot(gp, it->second);
    }
    Slot const* slot = it == s.slots.end() ? nullptr : &it->second;
    bool has = slot && slot->hasItem();
    if (kind == Kind::Pedestal) updatePedestal(region, pos, block, has);
    if (kind == Kind::Statue) updateStatue(region, pos, block, slot ? slot->item : ItemStack::EMPTY_ITEM());
    syncEntity(region, pos, kind, slot);
}

bool putItem(BlockSource& region, BlockPos const& pos, ItemStack const& item) {
    if (item.isNull()) return false;
    auto const& block = region.getBlock(pos);
    Kind kind = kindOf(block);
    if (kind == Kind::None) return false;
    if (kind == Kind::Pedestal && pedestalStatusFor(region, pos, false) == PedestalStatus::None
        && game::intState(block, "complementaries:item_status").value_or(1) == 0)
        return false;
    auto gp = game::globalPos(region, pos);
    auto& slot = st().slots[gp];
    if (slot.hasItem()) return false;
    slot.kind = kind;
    slot.item = item;
    slot.item.set(1);
    saveSlot(gp, slot);
    refresh(region, pos);
    region.updateNeighborsAt(pos); // comparators
    return true;
}

std::optional<ItemStack> takeItem(BlockSource& region, BlockPos const& pos) {
    auto gp = game::globalPos(region, pos);
    auto it = st().slots.find(gp);
    if (it == st().slots.end() || !it->second.hasItem()) return std::nullopt;
    ItemStack item = it->second.item;
    if (it->second.kind == Kind::Statue) {
        it->second.item = ItemStack();
        saveSlot(gp, it->second);
    } else {
        eraseSlot(gp);
    }
    refresh(region, pos);
    region.updateNeighborsAt(pos);
    return item;
}

UseResult use(Player& player, BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    Kind kind = kindOf(block);
    if (kind == Kind::None) return UseResult::Pass;
    float cx = static_cast<float>(pos.x) + 0.5f, cy = static_cast<float>(pos.y) + 0.5f, cz = static_cast<float>(pos.z) + 0.5f;
    auto  name = tr(std::string("block.supplementaries.") + std::string(blockIdOf(kind).substr(16)), std::string(blockIdOf(kind)));
    auto  where = std::format("{} {} {}", pos.x, pos.y, pos.z);
    if (auto taken = takeItem(region, pos)) {
        auto id = itemId(*taken);
        giveToPlayer(player, *taken);
        playSound(region, cx, cy, cz, "block.itemframe.remove_item");
        log().info("{}", tr("cm.display.taken", "Took {} from the {} at {}", id, name, where));
        return UseResult::Taken;
    }
    ItemStack hand = player.getSelectedItem();
    if (hand.isNull()) return UseResult::Pass;
    auto id = itemId(hand);
    if (!putItem(region, pos, hand)) return UseResult::Pass;
    if (!isCreative(player)) {
        hand.remove(1);
        player.setSelectedItem(hand.isNull() || hand.mCount == 0 ? ItemStack::EMPTY_ITEM() : hand);
        player.sendInventory(false);
    }
    playSound(region, cx, cy, cz, kind == Kind::Shelf ? "insert.chiseled_bookshelf" : "block.itemframe.add_item");
    log().info("{}", tr("cm.display.placed", "Placed {} on the {} at {}", id, name, where));
    return UseResult::Placed;
}

} // namespace cm::feature::display

CM_REGISTER_MODULE(cm::feature::display::DisplayModule);
