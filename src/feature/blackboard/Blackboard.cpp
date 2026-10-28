// U5 blackboard: block data, display entities, interaction, items and drops.

#include "feature/blackboard/Blackboard.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyContainer.h"
#include "mc/world/actor/state/PropertyMetadata.h"
#include "mc/world/actor/state/PropertyValues.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/blackboard/Editor.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("blackboard", cm::feature::blackboard::BlackboardConfig);

namespace cm::feature::blackboard {

namespace {

constexpr char const* kDbPrefix   = "cm:blackboard:";
constexpr char const* kItemTag    = "cm_blackboard";
constexpr int         kCheckEvery = 40; ///< ticks between display checks

// front index -> (dx, dz) of the direction the drawing faces
constexpr std::pair<int, int> kFrontDir[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

struct State {
    std::map<core::GlobalPos, Board> boards; ///< non-empty boards of the loaded world
    std::set<core::GlobalPos>        dirty;  ///< changed since the last save
    std::map<core::GlobalPos, Board> removed; ///< boards broken this tick (for their drops)
    std::unordered_map<std::int64_t, Board> placing; ///< item data of a block being placed, by player
    std::map<core::GlobalPos, int>   missing; ///< checks a drawn board was seen without displays
    int                              tick = 0;
};
State& state() {
    static State s;
    return s;
}

std::string dbKey(core::GlobalPos const& p) { return kDbPrefix + p.key(); }

std::optional<core::GlobalPos> parseKey(std::string_view suffix) {
    core::GlobalPos p;
    int*            parts[4] = {&p.dim, &p.x, &p.y, &p.z};
    std::size_t     start    = 0;
    for (int i = 0; i < 4; ++i) {
        auto end = i < 3 ? suffix.find(':', start) : suffix.size();
        if (end == std::string_view::npos) return std::nullopt;
        try {
            *parts[i] = std::stoi(std::string(suffix.substr(start, end - start)));
        } catch (...) {
            return std::nullopt;
        }
        start = end + 1;
    }
    return p;
}

nlohmann::json toJson(Board const& b) { return {{"values", toHex(b.px)}, {"glow", b.glow}, {"waxed", b.waxed}}; }

std::optional<Board> fromJson(nlohmann::json const& j) {
    if (!j.is_object()) return std::nullopt;
    Board b;
    if (auto px = parseHex(j.value("values", std::string()))) b.px = *px;
    b.glow  = j.value("glow", false);
    b.waxed = j.value("waxed", false);
    return b;
}

bool serverSide(BlockSource& region) { return !region.getLevel().isClientSide(); }

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

// ---- display entities -------------------------------------------------------------------------

std::string halfTag(int half) { return half ? "cm_bb_1" : "cm_bb_0"; }

/// Writes entity properties directly (the game's queue only applies them during entity events) and
/// marks them dirty so they are synced to the clients.
void writeDisplay(Actor& actor, int half, int front, Board const& board) {
    auto props = actor.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) return;
    auto& container = *props->mPropertyContainer;
    auto& values    = *container.mValues;
    auto  setInt    = [&](std::string_view name, int v) {
        auto idx = container._getPropertyTypeArrayIndex(HashedString::computeHash(name), PropertyMetadata::ContainedType::IntProperty);
        if (!idx || *idx >= values.mIntValues->size()) return;
        if ((*values.mIntValues)[*idx] == v) return;
        (*values.mIntValues)[*idx] = v;
        props->mDirtyIntProperties->set(*idx);
    };
    setInt("complementaries:half", half);
    setInt("complementaries:facing", front);
    setInt("complementaries:colors", static_cast<int>(colorMask(board.px, half * 8, 8)));
    auto packed = packHalf(board.px, half);
    for (std::size_t k = 0; k < packed.size(); ++k) setInt("complementaries:p" + std::to_string(k), packed[k]);
    auto glow = container._getPropertyTypeArrayIndex(
        HashedString::computeHash("complementaries:glow"),
        PropertyMetadata::ContainedType::BoolProperty
    );
    if (glow && *glow < values.mBoolValues->size() && (*values.mBoolValues)[*glow] != board.glow) {
        (*values.mBoolValues)[*glow] = board.glow;
        props->mDirtyBoolProperties->set(*glow);
    }
}
Actor* spawnDisplay(BlockSource& region, BlockPos const& pos, int half) {
    auto snbt = std::format(
        R"({{identifier:"{}",Pos:[{}f,{}f,{}f],Rotation:[0.0f,0.0f],Tags:["{}"]}})",
        kDisplayId,
        pos.x + 0.5,
        pos.y + 0.0,
        pos.z + 0.5,
        halfTag(half)
    );
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) {
        log().error("blackboard: bad display nbt");
        return nullptr;
    }
    auto actor = region.spawnActor(*tag);
    return actor ? actor.as_ptr() : nullptr;
}

/// Spawns / updates / removes the two display entities of the board at `pos`.
void syncDisplays(BlockSource& region, BlockPos const& pos, Board const& board, int front) {
    auto existing = displaysAt(region, pos);
    if (board.blank() || front < 0) {
        for (auto* a : existing) a->remove();
        return;
    }
    for (int half = 0; half < 2; ++half) {
        Actor* keep = nullptr;
        for (auto* a : existing) {
            if (!a->hasTag(halfTag(half))) continue;
            if (!keep) keep = a;
            else a->remove(); // duplicate
        }
        if (!keep) keep = spawnDisplay(region, pos, half);
        if (keep) writeDisplay(*keep, half, front, board);
    }
}

void storeBoard(core::GlobalPos const& gp, Board const& board) {
    auto& s = state();
    if (board.empty()) {
        if (s.boards.erase(gp)) s.dirty.insert(gp);
    } else {
        s.boards[gp] = board;
        s.dirty.insert(gp);
    }
}

void flush() {
    auto& s = state();
    if (!game::db::available()) return;
    for (auto const& gp : s.dirty) {
        auto it = s.boards.find(gp);
        if (it == s.boards.end()) game::db::erase(dbKey(gp));
        else game::db::save(dbKey(gp), toJson(it->second));
    }
    s.dirty.clear();
}

void loadAll() {
    auto& s = state();
    s.boards.clear();
    s.dirty.clear();
    s.removed.clear();
    s.missing.clear();
    if (!game::db::available()) return;
    std::vector<core::GlobalPos> keys;
    game::db::forEachKey(kDbPrefix, [&keys](std::string_view suffix) {
        if (auto p = parseKey(suffix)) keys.push_back(*p);
    });
    for (auto const& p : keys) {
        if (auto j = game::db::load(dbKey(p))) {
            if (auto b = fromJson(*j); b && !b->empty()) s.boards[p] = *b;
        }
    }
    log().info("blackboard: {} boards loaded", s.boards.size());
}

/// Periodic check: drawn boards whose displays went missing get them back, boards whose block is
/// gone are forgotten.
void checkBoards() {
    auto& s = state();
    std::vector<core::GlobalPos> gone;
    for (auto const& [gp, board] : s.boards) {
        auto* region = regionOf(gp.dim);
        if (!region) continue;
        auto pos = game::blockPos(gp);
        if (!region->hasChunksAt(pos, 0, false)) continue;
        auto const& block = region->getBlock(pos);
        if (!isBlackboard(block)) {
            gone.push_back(gp);
            continue;
        }
        if (board.blank()) continue;
        auto displays = displaysAt(*region, pos);
        if (displays.size() == 2) {
            s.missing.erase(gp);
            continue;
        }
        // entities load a little after their chunk: only respawn after two checks in a row
        if (++s.missing[gp] < 2 && displays.empty()) continue;
        s.missing.erase(gp);
        syncDisplays(*region, pos, board, frontOf(block));
    }
    for (auto const& gp : gone) {
        storeBoard(gp, Board{});
        s.missing.erase(gp);
    }
}

void consumeOne(Player& player, ItemStack& item) {
    if (player.isCreative()) return;
    item.remove(1);
    player.sendInventory(false);
}

void spawnParticles(BlockSource& region, BlockPos const& pos, std::string const& effect, int count) {
    auto& level = region.getLevel();
    for (int i = 0; i < count; ++i) {
        float dx = 0.15f + 0.7f * static_cast<float>((i * 37) % 10) / 10.f;
        float dy = 0.15f + 0.7f * static_cast<float>((i * 53) % 10) / 10.f;
        level.spawnParticleEffect(effect, Vec3{pos.x + dx, pos.y + dy, pos.z + 0.5f}, &region.getDimension());
    }
}

// ---- hooks ------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    BlackboardDropsHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::spawnResources,
    void,
    BlockSource&                  region,
    BlockPos const&               pos,
    Block const&                  block,
    IRandom&                      random,
    ResourceDropsContext const&   ctx,
    Actor const*                  actorContext
) {
    if (isBlackboard(block) && serverSide(region)) {
        auto  gp = game::globalPos(region, pos);
        auto& s  = state();
        std::optional<Board> board;
        if (auto it = s.removed.find(gp); it != s.removed.end()) board = it->second;
        else if (auto it2 = s.boards.find(gp); it2 != s.boards.end()) board = it2->second;
        if (board && !board->empty()) {
            auto item = makeBoardItem(*board);
            region.getLevel().getSpawner().spawnItem(
                region,
                item,
                nullptr,
                Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f},
                10
            );
            s.removed.erase(gp);
            return;
        }
    }
    origin(region, pos, block, random, ctx, actorContext);
}

LL_TYPE_INSTANCE_HOOK(
    BlackboardPickHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::asItemInstance,
    ItemInstance,
    BlockSource&    region,
    BlockPos const& pos,
    bool            withData
) {
    auto result = origin(region, pos, withData);
    if (withData && isBlackboard(*this) && serverSide(region)) {
        auto board = boardAt(game::globalPos(region, pos));
        if (!board.empty()) writeBoardToItem(result, board);
    }
    return result;
}

// ---- module -----------------------------------------------------------------------------------

class BlackboardModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](auto& ev) {
            auto block = ev.block();
            if (!block || !isBlackboard(*block)) return;
            auto& player = ev.self();
            if (use(player, ev.item(), ev.blockPos(), static_cast<int>(ev.face()), ev.clickPos())) ev.cancel();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>([](auto& ev) {
            auto& player = ev.self();
            auto  board  = boardOfItem(player.getSelectedItem());
            auto  key    = player.getOrCreateUniqueID().rawID;
            if (board) state().placing[key] = *board;
            else state().placing.erase(key);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](auto& ev) {
            auto& s   = state();
            auto  key = ev.self().getOrCreateUniqueID().rawID;
            auto  it  = s.placing.find(key);
            if (it == s.placing.end()) return;
            auto board = it->second;
            s.placing.erase(it);
            if (!isBlackboard(ev.placedBlock())) return;
            setBoard(ev.self().getDimensionBlockSource(), ev.pos(), board);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](auto& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (!serverSide(region)) return;
            bool was = isBlackboard(ev.previousBlock());
            bool is  = isBlackboard(ev.newBlock());
            if (!was && !is) return;
            auto  gp = game::globalPos(region, ev.pos());
            auto& s  = state();
            if (was && is) {
                // rotated (structure / command): turn the drawing with it
                if (frontOf(ev.previousBlock()) != frontOf(ev.newBlock())) {
                    if (auto it = s.boards.find(gp); it != s.boards.end()) {
                        syncDisplays(region, ev.pos(), it->second, frontOf(ev.newBlock()));
                    }
                }
                return;
            }
            if (was) {
                if (auto it = s.boards.find(gp); it != s.boards.end()) {
                    s.removed[gp] = it->second;
                    for (auto* a : displaysAt(region, ev.pos())) a->remove();
                    storeBoard(gp, Board{});
                }
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            auto& s = state();
            s.removed.clear();
            if (++s.tick % kCheckEvery == 0) checkBoards();
        }));
        BlackboardDropsHook::hook();
        BlackboardPickHook::hook();

        game::db::onWorldReady([] { loadAll(); });
        game::db::onWorldSave([] { flush(); });
        game::db::onWorldClosed([] {
            closeAllEditors();
            auto& s = state();
            s.boards.clear();
            s.dirty.clear();
            s.removed.clear();
            s.placing.clear();
            s.missing.clear();
        });
        if (game::db::available()) loadAll(); // BDS: the world already exists
    }

    void uninstall() {
        closeAllEditors();
        flush();
        BlackboardDropsHook::unhook();
        BlackboardPickHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

// ---- public API -------------------------------------------------------------------------------

BlackboardConfig const& config() { return cm::config::get<BlackboardConfig>(); }
bool canOpenGui() { return config().interaction_mode != "manual"; }
bool canManualDraw() { return config().interaction_mode != "gui"; }

bool isBlackboard(Block const& block) { return block.getTypeName() == kBlockId; }

int frontOf(Block const& block) {
    if (!isBlackboard(block)) return -1;
    try {
        auto const& states = block.mSerializationId->at("states");
        auto const& dir    = states.get<CompoundTag>().at("minecraft:cardinal_direction").get<StringTag>();
        std::string_view d = dir;
        if (d == "north") return 0;
        if (d == "east") return 1;
        if (d == "south") return 2;
        if (d == "west") return 3;
    } catch (...) {}
    return 0;
}

int faceIdOf(int front) {
    switch (front & 3) {
    case 0:
        return 2;
    case 1:
        return 5;
    case 2:
        return 3;
    default:
        return 4;
    }
}

std::pair<int, int> hitPixel(int front, BlockPos const& pos, Vec3 const& hit) {
    auto inBlock = [](float v) { return v >= -0.001f && v <= 1.001f; };
    Vec3 rel     = hit;
    if (!(inBlock(hit.x) && inBlock(hit.y) && inBlock(hit.z))) {
        rel = Vec3{hit.x - pos.x, hit.y - pos.y, hit.z - pos.z};
    }
    auto cell = [](float v) { return std::clamp(static_cast<int>(std::floor(v * 16.f)), 0, 15); };
    int  row  = 15 - cell(rel.y);
    int  col  = 0;
    switch (front & 3) {
    case 0: // faces north: the viewer's right is west
        col = 15 - cell(rel.x);
        break;
    case 2: // south: right is east
        col = cell(rel.x);
        break;
    case 1: // east: right is north
        col = 15 - cell(rel.z);
        break;
    default: // west: right is south
        col = cell(rel.z);
        break;
    }
    return {col, row};
}

Board boardAt(core::GlobalPos const& pos) {
    auto& s = state();
    if (auto it = s.boards.find(pos); it != s.boards.end()) return it->second;
    return {};
}

void setBoard(BlockSource& region, BlockPos const& pos, Board const& board) {
    auto gp = game::globalPos(region, pos);
    storeBoard(gp, board);
    syncDisplays(region, pos, board, frontOf(region.getBlock(pos)));
}

int displayProperty(Actor& actor, std::string_view name) {
    auto props = actor.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) return -1;
    auto& container = *props->mPropertyContainer;
    auto  idx = container._getPropertyTypeArrayIndex(HashedString::computeHash(name), PropertyMetadata::ContainedType::IntProperty);
    if (!idx || *idx >= container.mValues->mIntValues->size()) return -1;
    return (*container.mValues->mIntValues)[*idx];
}

std::vector<Actor*> displaysAt(BlockSource& region, BlockPos const& pos) {
    std::vector<Actor*> out;
    AABB                box{
        Vec3{static_cast<float>(pos.x) + 0.25f, static_cast<float>(pos.y) - 0.1f, static_cast<float>(pos.z) + 0.25f},
        Vec3{static_cast<float>(pos.x) + 0.75f, static_cast<float>(pos.y) + 0.1f, static_cast<float>(pos.z) + 0.75f}
    };
    for (auto* a : region.getEntities(box, 0.5f)) {
        if (!a || a->getTypeName() != kDisplayId) continue;
        auto const& p = a->getPosition();
        if (std::abs(p.x - (pos.x + 0.5f)) > 0.01f || std::abs(p.z - (pos.z + 0.5f)) > 0.01f) continue;
        if (std::abs(p.y - static_cast<float>(pos.y)) > 0.01f) continue;
        out.push_back(a);
    }
    return out;
}

std::optional<Board> boardOfItem(ItemStackBase const& item) {
    if (item.isNull() || item.getTypeName() != kBlockId) return std::nullopt;
    auto const& tag = item.mUserData;
    if (!tag || !tag->contains(kItemTag)) return std::nullopt;
    try {
        auto const& data = tag->at(kItemTag).get<CompoundTag>();
        Board       b;
        if (data.contains("values")) {
            std::string const& hex = data.at("values").get<StringTag>();
            if (auto px = parseHex(hex)) b.px = *px;
        }
        if (data.contains("glow")) b.glow = data.at("glow").get<ByteTag>().data != 0;
        if (data.contains("waxed")) b.waxed = data.at("waxed").get<ByteTag>().data != 0;
        if (b.empty()) return std::nullopt;
        return b;
    } catch (...) {
        return std::nullopt;
    }
}

void writeBoardToItem(ItemStackBase& item, Board const& board) {
    std::unique_ptr<CompoundTag> tag = item.mUserData ? item.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->remove(kItemTag);
    if (!board.empty()) {
        CompoundTag data;
        data.putString("values", toHex(board.px));
        data.putBoolean("glow", board.glow);
        data.putBoolean("waxed", board.waxed);
        tag->putCompound(kItemTag, std::move(data));
    }
    item.setUserData(std::move(tag));
    std::vector<std::string> lore;
    if (board.waxed) lore.push_back("§7" + tr("message.supplementaries.blackboard", "Waxed"));
    if (board.glow) lore.push_back("§7" + tr("cm.blackboard.glowing", "Glowing"));
    if (!board.blank()) {
        for (auto& line : pixelArt(board.px)) lore.push_back(std::move(line));
    }
    if (lore.empty()) item.clearCustomLore();
    else item.setCustomLore(lore);
}

ItemStack makeBoardItem(Board const& board, int count) {
    ItemStack item;
    item.reinit(kBlockId, count, 0);
    if (!board.empty()) writeBoardToItem(item, board);
    return item;
}

void playSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName   = name;
    pkt.mPos    = BlockPos{pos.x * 8 + 4, pos.y * 8 + 4, pos.z * 8 + 4};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    region.getDimension().sendPacketForPosition(pos, pkt, nullptr);
}

bool use(Player& player, ItemStack& item, BlockPos const& pos, int face, Vec3 const& hit) {
    if (!config().enabled) return false;
    auto& region = player.getDimensionBlockSource();
    auto const& block = region.getBlock(pos);
    if (!isBlackboard(block)) return false;
    // Java secondary use: sneaking with an item uses the item, not the block
    if (player.isSneaking() && !item.isNull()) return false;

    auto        gp    = game::globalPos(region, pos);
    Board       board = boardAt(gp);
    std::string id    = item.isNull() ? std::string() : item.getTypeName();

    // waxing and glowing work on every face
    if (id == "minecraft:honeycomb") {
        if (board.waxed) {
            playSound(region, pos, "complementaries.block.blackboard.fail");
            return true;
        }
        board.waxed = true;
        setBoard(region, pos, board);
        consumeOne(player, item);
        playSound(region, pos, "complementaries.block.blackboard.wax_on");
        spawnParticles(region, pos, "minecraft:wax_particle", 6);
        log().info("blackboard: waxed at {}", gp.key());
        return true;
    }
    if (id == "minecraft:glow_ink_sac") {
        if (board.glow) {
            playSound(region, pos, "complementaries.block.blackboard.fail");
            return true;
        }
        board.glow = true;
        setBoard(region, pos, board);
        consumeOne(player, item);
        playSound(region, pos, "complementaries.block.blackboard.glow");
        spawnParticles(region, pos, "minecraft:glow_particle", 6);
        log().info("blackboard: glowing at {}", gp.key());
        return true;
    }
    if (id == "minecraft:brush") {
        if (!board.waxed && !board.blank()) {
            board.clear();
            setBoard(region, pos, board);
            playSound(region, pos, "complementaries.block.blackboard.brush");
            log().info("blackboard: brushed at {}", gp.key());
        }
        return true;
    }
    if (id == "complementaries:soap") {
        // Java IWashable: one layer per wash
        if (board.waxed) board.waxed = false;
        else if (board.glow) board.glow = false;
        else if (!board.blank()) board.clear();
        else return false;
        setBoard(region, pos, board);
        consumeOne(player, item);
        spawnParticles(region, pos, "minecraft:water_splash_particle", 6);
        log().info("blackboard: washed at {}", gp.key());
        return true;
    }

    int front = frontOf(block);
    if (face == faceIdOf(front) && canManualDraw() && !board.waxed) {
        if (auto color = chalkOf(id, config().colored_blackboard)) {
            auto [x, y] = hitPixel(front, pos, hit);
            if (board.get(x, y) != *color) {
                board.set(x, y, *color);
                setBoard(region, pos, board);
                playSound(region, pos, "complementaries.block.blackboard.draw", 0.8f, 1.f);
                log().info("blackboard: drew {} at {} ({}, {})", static_cast<int>(*color), gp.key(), x, y);
            }
            return true;
        }
    }
    if (canOpenGui()) {
        if (!board.waxed) openEditor(player, pos);
        return true;
    }
    return false;
}

Canvas connectedBoards(BlockSource& region, BlockPos const& clicked, bool connected) {
    Canvas canvas;
    int    front = frontOf(region.getBlock(clicked));
    auto   self  = game::globalPos(region, clicked);
    if (!connected || front < 0) {
        canvas.cells = {self};
        return canvas;
    }
    // Java: flood fill a 5x5 window around the clicked board (same facing, not waxed), then keep
    // the 3x3 window holding the most boards.
    auto [rx, rz] = kFrontDir[(front + 3) & 3]; // counter-clockwise of the facing: the viewer's right
    std::optional<core::GlobalPos> around[5][5];
    std::vector<std::pair<int, int>> todo{{2, 2}};
    bool                             seen[5][5]{};
    while (!todo.empty()) {
        auto [c, r] = todo.back();
        todo.pop_back();
        if (c < 0 || r < 0 || c > 4 || r > 4 || seen[c][r]) continue;
        seen[c][r] = true;
        BlockPos p{clicked.x + rx * (c - 2), clicked.y + (2 - r), clicked.z + rz * (c - 2)};
        if (!region.hasChunksAt(p, 0, false)) continue;
        auto const& b = region.getBlock(p);
        if (!isBlackboard(b) || frontOf(b) != front) continue;
        auto gp = game::globalPos(region, p);
        if (boardAt(gp).waxed) continue;
        around[c][r] = gp;
        todo.push_back({c + 1, r});
        todo.push_back({c - 1, r});
        todo.push_back({c, r + 1});
        todo.push_back({c, r - 1});
    }
    auto count = [&](int sc, int sr) {
        int n = 0;
        for (int c = sc; c < sc + 3; ++c)
            for (int r = sr; r < sr + 3; ++r)
                if (around[c][r]) ++n;
        return n;
    };
    int startC = 1, startR = 1;
    for (int sc = 0; sc < 3; ++sc) {
        for (int sr = 0; sr < 3; ++sr) {
            if (count(sc, sr) > count(startC, startR)) {
                startC = sc;
                startR = sr;
            }
        }
    }
    int minC = 4, maxC = 0, minR = 4, maxR = 0;
    for (int c = startC; c < startC + 3; ++c) {
        for (int r = startR; r < startR + 3; ++r) {
            if (!around[c][r]) continue;
            minC = std::min(minC, c);
            maxC = std::max(maxC, c);
            minR = std::min(minR, r);
            maxR = std::max(maxR, r);
        }
    }
    canvas.cols = maxC - minC + 1;
    canvas.rows = maxR - minR + 1;
    canvas.cells.assign(static_cast<std::size_t>(canvas.cols * canvas.rows), std::nullopt);
    for (int c = minC; c <= maxC; ++c)
        for (int r = minR; r <= maxR; ++r)
            canvas.cells[static_cast<std::size_t>((c - minC) * canvas.rows + (r - minR))] = around[c][r];
    return canvas;
}

} // namespace cm::feature::blackboard

CM_REGISTER_MODULE(cm::feature::blackboard::BlackboardModule);
