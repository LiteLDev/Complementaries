#include "feature/tableware/Tableware.h"

#include <cmath>
#include <random>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::feature::tableware {

CM_REGISTER_CONFIG("tableware", TablewareConfig);

TablewareConfig const& config() { return cm::config::get<TablewareConfig>(); }

// ---- block helpers --------------------------------------------------------------------------------

bool is(Block const& block, std::string_view id) { return block.getTypeName() == id; }

int state(Block const& block, std::string_view name, int def) { return cm::game::intState(block, name).value_or(def); }

namespace {
StateList statesOf(Block const& block) {
    StateList   out;
    auto const& tag = *block.mSerializationId;
    if (!tag.contains("states")) return out;
    auto const& states = tag.at("states");
    if (!states.is_object()) return out;
    for (auto const& [name, value] : states.get<CompoundTag>()) {
        if (value.is_string()) {
            out.emplace_back(name, std::string(value.get<StringTag>()));
        } else if (value.hold(Tag::Byte)) {
            out.emplace_back(name, static_cast<bool>(value.get<ByteTag>().data));
        } else if (value.hold(Tag::Int)) {
            out.emplace_back(name, value.get<IntTag>().data);
        } else if (value.hold(Tag::Float)) {
            out.emplace_back(name, value.get<FloatTag>().data);
        }
    }
    return out;
}
} // namespace

std::string direction(Block const& block) {
    for (auto const& [name, value] : statesOf(block)) {
        if (name == "minecraft:cardinal_direction" && std::holds_alternative<std::string>(value)) {
            return std::get<std::string>(value);
        }
    }
    return "south";
}

Block const* with(Block const& block, StateList const& changes) {
    // Rebuild the block's serialization tag with the changed states and look it up again (works
    // for integer and string states alike).
    CompoundTag tag = *block.mSerializationId;
    if (!tag.contains("states")) return nullptr;
    auto& states = tag["states"].get<CompoundTag>();
    for (auto const& [name, value] : changes) {
        if (auto const* i = std::get_if<int>(&value)) {
            states[name] = IntTag(*i);
        } else if (auto const* s = std::get_if<std::string>(&value)) {
            states[name] = StringTag(*s);
        } else if (auto const* b = std::get_if<bool>(&value)) {
            states[name] = ByteTag(*b);
        }
    }
    auto b = Block::tryGetFromRegistry(tag);
    return b ? b.as_ptr() : nullptr;
}

Block const* make(std::string_view id, StateList const& states) {
    auto base = Block::tryGetFromRegistry(HashedString(std::string(id)));
    if (!base) return nullptr;
    if (states.empty()) return base.as_ptr();
    return with(*base, states);
}

bool place(BlockSource& region, BlockPos const& pos, Block const& block) {
    return cm::game::setBlock(region, pos, block);
}


std::string directionOfFace(FacingID face) {
    switch (face) {
    case FacingID::North:
        return "north";
    case FacingID::South:
        return "south";
    case FacingID::West:
        return "west";
    case FacingID::East:
        return "east";
    default:
        return {};
    }
}

std::string directionTowards(Player const& player) {
    // Java 2D facing the player looks along (0 S, 1 W, 2 N, 3 E); the block faces back at him.
    static constexpr char const* kNames[] = {"north", "east", "south", "west"};
    return kNames[cm::game::playerFacing(player) & 3];
}

// ---- item helpers ---------------------------------------------------------------------------------

ItemStack makeItem(std::string_view id, int count, int aux) {
    ItemStack item;
    item.reinit(id, count, aux);
    return item;
}

bool isEmpty(ItemStack const& item) { return item.isNull() || item.mCount == 0; }

std::string itemId(ItemStack const& item) { return isEmpty(item) ? std::string{} : item.getTypeName(); }

bool creative(Player const& player) { return player.isCreative(); }

void consumeHeld(Player& player, int n) {
    if (creative(player)) return;
    ItemStack held = player.getSelectedItem();
    if (isEmpty(held)) return;
    if (held.mCount <= n) {
        held = ItemStack::EMPTY_ITEM();
    } else {
        held.remove(n);
    }
    player.setSelectedItem(held);
    player.sendInventory(false);
}

namespace {
bool hasItem(Player& player, ItemStack const& like) {
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& it = inv.getItem(i);
        if (!isEmpty(it) && it.getTypeName() == like.getTypeName() && it.getAuxValue() == like.getAuxValue())
            return true;
    }
    return false;
}
} // namespace

void exchangeHeld(Player& player, ItemStack const& result) {
    if (creative(player)) {
        if (!hasItem(player, result)) giveOrDrop(player, result);
        return;
    }
    ItemStack held = player.getSelectedItem();
    if (!isEmpty(held) && held.mCount == 1) {
        player.setSelectedItem(result);
        player.sendInventory(false);
        return;
    }
    consumeHeld(player, 1);
    giveOrDrop(player, result);
}

void giveOrDrop(Player& player, ItemStack item) {
    if (isEmpty(item)) return;
    if (!player.addAndRefresh(item) && !isEmpty(item)) player.drop(item, false);
}

std::string itemToSnbt(ItemStack const& item) {
    SaveContext ctx(SaveContext::SaveUseCase::SaveToDisk);
    auto        tag = item.save(ctx);
    return tag ? tag->toSnbt(SnbtFormat::Minimize, 0) : std::string{};
}

std::optional<ItemStack> itemFromSnbt(std::string const& snbt) {
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) return std::nullopt;
    auto item = ItemStack::fromTag(*tag);
    if (isEmpty(item)) return std::nullopt;
    return item;
}

// ---- world helpers --------------------------------------------------------------------------------

std::string posKey(std::string_view kind, int dim, BlockPos const& pos) {
    return "cm:tableware:" + std::string(kind) + ":" + std::to_string(dim) + ":" + std::to_string(pos.x) + ":"
         + std::to_string(pos.y) + ":" + std::to_string(pos.z);
}

namespace {
std::vector<std::function<void()>>& queue() {
    static std::vector<std::function<void()>> q;
    return q;
}

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
} // namespace

void defer(std::function<void()> fn) { queue().push_back(std::move(fn)); }

void dropLater(int dim, BlockPos const& pos, std::vector<ItemStack> items) {
    if (items.empty()) return;
    defer([dim, pos, items = std::move(items)] {
        auto* region = regionOf(dim);
        auto  level  = ll::service::getLevel();
        if (!region || !level) return;
        Vec3 at{pos.x + 0.5f, pos.y + 0.3f, pos.z + 0.5f};
        for (auto const& item : items) level->getSpawner().spawnItem(*region, item, nullptr, at, 0);
    });
}

void spawnMobLater(int dim, BlockPos const& pos, std::string const& id) {
    defer([dim, pos, id] {
        auto* region = regionOf(dim);
        auto  level  = ll::service::getLevel();
        if (!region || !level) return;
        Vec3 at{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f};
        if (level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, at, false, false, false)) {
            log().info("tableware: urn released {}", id);
        }
    });
}

void playSound(Player& player, int levelSoundEvent, BlockPos const& pos, int data) {
    Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    player.getLevel().playSound(
        player.getDimensionBlockSource(),
        static_cast<SharedTypes::Legacy::LevelSoundEvent>(levelSoundEvent),
        at,
        data,
        ActorSoundIdentifier(player),
        false
    );
}

float randomFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }
int   randomInt(int bound) { return bound <= 1 ? 0 : std::uniform_int_distribution<int>(0, bound - 1)(rng()); }

// ---- interactions ---------------------------------------------------------------------------------

namespace {
constexpr UseHandler kHandlers[] = {
    &goblet::onUse,
    &flower_box::onUse,
    &pancake::onUse,
    &book_pile::onUse,
    &book_pile::onPlaceUse,
};

Vec3 hitInBlock(Vec3 const& click, BlockPos const& pos) {
    auto inUnit = [](float v) { return v >= -0.001f && v <= 1.001f; };
    // The protocol sends the click relative to the block; accept absolute positions too.
    if (inUnit(click.x) && inUnit(click.y) && inUnit(click.z)) return click;
    return Vec3{click.x - pos.x, click.y - pos.y, click.z - pos.z};
}

/// Index of the handler that takes the click (-1: none); `apply` performs it. `only` restricts the
/// search to one handler (a deferred click re-checks the handler chosen at click time).
int runHandlers(Player& player, BlockPos const& pos, FacingID face, Vec3 const& hit, bool apply, int only) {
    auto& region = player.getDimensionBlockSource();
    if (!region.hasChunksAt(pos, 0, false)) return -1;
    auto const& block = region.getBlock(pos);
    ItemStack   held  = player.getSelectedItem();
    UseContext  ctx{player, region, pos, block, held, face, hit};
    for (int i = 0; i < static_cast<int>(std::size(kHandlers)); ++i) {
        if (only >= 0 && i != only) continue;
        if (kHandlers[i](ctx, false)) {
            if (apply) kHandlers[i](ctx, true);
            return i;
        }
    }
    return -1;
}
} // namespace

bool handleUse(Player& player, BlockPos const& pos, FacingID face, Vec3 const& click, bool immediate) {
    Vec3 hit = hitInBlock(click, pos);
    if (immediate) return runHandlers(player, pos, face, hit, true, -1) >= 0;
    int handler = runHandlers(player, pos, face, hit, false, -1);
    if (handler < 0) return false;
    auto id  = player.getOrCreateUniqueID();
    int  dim = static_cast<int>(player.getDimensionId());
    defer([id, dim, pos, face, hit, handler] {
        auto level = ll::service::getLevel();
        if (!level) return;
        auto* p = level->getPlayer(id);
        if (!p || static_cast<int>(p->getDimensionId()) != dim) return;
        runHandlers(*p, pos, face, hit, true, handler);
    });
    return true;
}

// ---- module ---------------------------------------------------------------------------------------

class TablewareModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [this](ll::event::PlayerInteractBlockEvent& ev) { onInteract(ev); }
        ));
        mListeners.push_back(
            bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>([this](ll::event::PlayerPlacingBlockEvent& ev) {
                if (ev.self().isClientSide()) return;
                mPlacingFace[ev.self().getOrCreateUniqueID().rawID] = static_cast<FacingID>(ev.face());
            })
        );
        mListeners.push_back(
            bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([this](ll::event::PlayerPlacedBlockEvent& ev) {
                auto& player = ev.self();
                if (player.isClientSide() || !is(ev.placedBlock(), kFlowerBox)) return;
                auto it   = mPlacingFace.find(player.getOrCreateUniqueID().rawID);
                auto face = it == mPlacingFace.end() ? FacingID::Up : it->second;
                // the block is only in the world after the event
                defer([uuid = player.getUuid(), pos = ev.pos(), face] {
                    auto level = ll::service::getLevel();
                    if (auto* p = level ? level->getPlayer(uuid) : nullptr) flower_box::onPlaced(*p, pos, face);
                });
            })
        );
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            onBlockChanged(ev);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            ++mTick;
            if (queue().empty()) return;
            auto work = std::move(queue());
            queue().clear();
            for (auto& fn : work) {
                try {
                    fn();
                } catch (std::exception const& e) {
                    log().error("tableware: deferred action failed: {}", e.what());
                }
            }
        }));
        if (!mDbHooked) {
            // Pending drops / spawns belong to the world that is closing (the client opens many).
            game::db::onWorldClosed([] { queue().clear(); });
            mDbHooked = true;
        }
        planter::installHooks();
        book_pile::installHooks();
        urn::installHooks();
        jar_boat::installHooks();
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        planter::uninstallHooks();
        book_pile::uninstallHooks();
        urn::uninstallHooks();
        jar_boat::uninstallHooks();
        queue().clear();
        mPlacingFace.clear();
        mLastUse.clear();
    }

private:
    void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide()) return;
        auto key = player.getOrCreateUniqueID().rawID;
        auto pos = ev.blockPos();
        // The same click can be reported more than once (a tick or two apart): act once per Java's
        // 4-tick right-click delay, cancel every time.
        if (auto it = mLastUse.find(key); it != mLastUse.end() && mTick - it->second.tick < 4 && it->second.pos == pos) {
            if (it->second.handled) ev.cancel();
            return;
        }
        bool handled  = handleUse(player, pos, ev.face(), ev.clickPos(), false);
        mLastUse[key] = {mTick, pos, handled};
        if (handled) ev.cancel();
    }

    static void onBlockChanged(ll::event::BlockChangedEvent& ev) {
        if (ev.layer() != 0) return;
        auto const& prev = ev.previousBlock();
        auto const& now  = ev.newBlock();
        // LeviLamina reports one change twice in a row: handle it once.
        static struct {
            Block const* prev = nullptr;
            Block const* now  = nullptr;
            BlockPos     pos;
        } last;
        bool repeat = last.prev == &prev && last.now == &now && last.pos == ev.pos();
        last        = {&prev, &now, ev.pos()};
        if (repeat) return;
        auto const& name = prev.getTypeName();
        if (name.size() < 17 || name[0] != 'c' || !name.starts_with("complementaries:")) return;
        if (&now.getBlockType() == &prev.getBlockType()) return;
        auto& region = ev.blockSource();
        if (region.getILevel().isClientSide()) return;
        int  dim = static_cast<int>(region.getDimensionId());
        auto pos = ev.pos();
        if (name == kGoblet) {
            goblet::onRemoved(dim, pos);
        } else if (name == kFlowerBox) {
            flower_box::onRemoved(dim, pos, prev);
        } else if (name == kBookPile || name == kBookPileLying) {
            book_pile::onRemoved(dim, pos, prev);
        } else if (name == kUrn) {
            urn::onRemoved(dim, pos, prev);
        }
    }

    std::vector<ll::event::ListenerPtr> mListeners;
    std::unordered_map<int64, FacingID> mPlacingFace;
    struct LastUse {
        uint64   tick = 0;
        BlockPos pos;
        bool     handled = false;
    };
    std::unordered_map<int64, LastUse> mLastUse;
    bool                               mDbHooked = false;
    uint64                             mTick     = 0;
};

CM_REGISTER_MODULE(TablewareModule);

} // namespace cm::feature::tableware
