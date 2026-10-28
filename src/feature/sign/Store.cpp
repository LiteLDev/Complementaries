#include "feature/sign/Store.h"

#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/sign/SignGame.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::sign {

namespace {
constexpr std::string_view kPostPrefix  = "cm:sign:post:";
constexpr std::string_view kBoardPrefix = "cm:sign:board:";
constexpr std::string_view kMatPrefix   = "cm:sign:mat:";

std::string dbKey(std::string_view prefix, std::string const& key) { return std::string(prefix) + key; }

Vec3 postDisplayPos(KeyPos const& p) { return Vec3{p.x + 0.5f, p.y + 1.0f, p.z + 0.5f}; }

Vec3 boardDisplayPos(KeyPos const& p, int facing) {
    auto [dx, dz] = game::facingOffset(facing);
    return Vec3{p.x + 0.5f + dx * 0.51f, p.y + 0.5f, p.z + 0.5f + dz * 0.51f};
}

Vec3 matDisplayPos(KeyPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.07f, p.z + 0.5f}; }

/// The display actor of an entry: the stored one if loaded, otherwise a new one (when the chunk is
/// loaded). Updates `entity`; returns nullptr if it can't exist right now.
Actor* ensureDisplay(BlockSource& region, std::int64_t& entity, std::string_view type, Vec3 const& pos, bool* spawned) {
    if (auto* a = findActor(entity)) return a;
    if (!loadedAt(region, BlockPos{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))}))
        return nullptr;
    entity = spawnDisplay(region, type, pos);
    if (spawned) *spawned = entity != 0;
    return findActor(entity);
}

void dropDisplay(std::int64_t& entity) {
    if (auto* a = findActor(entity)) removeActor(*a);
    entity = 0;
}

} // namespace

// ---- helpers ------------------------------------------------------------------------------------
bool isPostBlock(std::string_view id) {
    if (id.ends_with("_fence") || id.ends_with("_wall")) return true;
    return id == "minecraft:cobblestone_wall" || id == "minecraft:fence";
}

bool isBoardItem(std::string_view id) {
    return id == "minecraft:writable_book" || id == "minecraft:written_book" || id == "minecraft:filled_map"
        || id == "minecraft:empty_map" || id == "minecraft:banner";
}

std::vector<std::string> bookPages(ItemStack const& item) {
    std::vector<std::string> pages;
    auto                     id = itemId(item);
    if (id != "minecraft:writable_book" && id != "minecraft:written_book") return pages;
    auto const* tag = item.mUserData.get();
    if (!tag || !tag->contains("pages", Tag::Type::List)) return pages;
    for (auto const& entry : tag->at("pages").get<ListTag>()) {
        if (!entry || entry->getId() != Tag::Type::Compound) continue;
        auto const& page = static_cast<CompoundTag const&>(*entry);
        if (page.contains("text", Tag::Type::String)) {
            pages.push_back(static_cast<std::string const&>(page.at("text").get<StringTag>()));
        } else {
            pages.emplace_back();
        }
    }
    return pages;
}

std::string itemToSnbt(ItemStack const& item) {
    if (item.isNull()) return {};
    auto ctx = SaveContextFactory::createCloneSaveContext();
    auto tag = item.save(*ctx);
    return tag ? tag->toSnbt(SnbtFormat::Minimize, 0) : std::string{};
}

ItemStack itemFromSnbt(std::string const& snbt) {
    if (snbt.empty()) return {};
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) return {};
    return ItemStack::fromTag(*tag);
}

int blockFacing(BlockSource& region, BlockPos const& pos) {
    return game::intState(region.getBlock(pos), kFacingState).value_or(2) & 3;
}

// ---- store --------------------------------------------------------------------------------------
Store& Store::get() {
    static Store store;
    return store;
}

void Store::clear() {
    mPosts.clear();
    mBoards.clear();
    mMats.clear();
    mNoDrop.clear();
}

void Store::load() {
    clear();
    namespace db = game::db;
    std::vector<std::string> keys;
    db::forEachKey(std::string(kPostPrefix), [&](std::string_view s) { keys.emplace_back(s); });
    for (auto const& k : keys) {
        if (auto j = db::load(dbKey(kPostPrefix, k))) mPosts[k] = postFromJson(*j);
    }
    keys.clear();
    db::forEachKey(std::string(kBoardPrefix), [&](std::string_view s) { keys.emplace_back(s); });
    for (auto const& k : keys) {
        auto j = db::load(dbKey(kBoardPrefix, k));
        if (!j || !j->is_object()) continue;
        Board b;
        b.item    = j->value("item", std::string{});
        b.page    = j->value("page", 0);
        b.color   = j->value("color", kDefaultDye) & 15;
        b.glowing = j->value("glowing", false);
        b.powered = j->value("powered", false);
        b.entity  = j->value("entity", std::int64_t{0});
        mBoards[k] = std::move(b);
    }
    keys.clear();
    db::forEachKey(std::string(kMatPrefix), [&](std::string_view s) { keys.emplace_back(s); });
    for (auto const& k : keys) {
        auto j = db::load(dbKey(kMatPrefix, k));
        if (!j || !j->is_object()) continue;
        Mat m;
        if (auto it = j->find("lines"); it != j->end() && it->is_array()) {
            for (std::size_t i = 0; i < m.lines.size() && i < it->size(); ++i) {
                if ((*it)[i].is_string()) m.lines[i] = (*it)[i].get<std::string>();
            }
        }
        m.color   = j->value("color", kDefaultDye) & 15;
        m.glowing = j->value("glowing", false);
        m.waxed   = j->value("waxed", false);
        m.entity  = j->value("entity", std::int64_t{0});
        mMats[k]  = std::move(m);
    }
}

// ---- posts --------------------------------------------------------------------------------------
Post* Store::post(std::string const& key) {
    auto it = mPosts.find(key);
    return it == mPosts.end() ? nullptr : &it->second;
}

void Store::savePost(std::string const& key, Post const& post) { game::db::save(dbKey(kPostPrefix, key), toJson(post)); }

void Store::syncPost(std::string const& key, Post& post) {
    auto p = parsePosKey(key);
    if (!p) return;
    auto* region = regionOf(p->dim);
    if (!region) return;
    auto  pos     = postDisplayPos(*p);
    auto  before  = post.entity;
    auto* actor   = ensureDisplay(*region, post.entity, kSignPostEntity, pos, nullptr);
    if (actor) applyDisplay(*actor, pos, packArm(post.up), packArm(post.down), postNameTag(post));
    if (post.entity != before) savePost(key, post);
}

void Store::putPost(std::string const& key, Post post) {
    if (post.empty()) {
        dropDisplay(post.entity);
        mPosts.erase(key);
        game::db::erase(dbKey(kPostPrefix, key));
        return;
    }
    auto& stored = mPosts[key];
    if (!post.entity) post.entity = stored.entity;
    stored = std::move(post);
    savePost(key, stored);
    syncPost(key, stored);
}

void Store::removePost(std::string const& key, bool drop) {
    auto it = mPosts.find(key);
    if (it == mPosts.end()) return;
    Post post = std::move(it->second);
    mPosts.erase(it);
    game::db::erase(dbKey(kPostPrefix, key));
    dropDisplay(post.entity);
    if (mNoDrop.erase(key)) drop = false;
    if (!drop) return;
    auto p = parsePosKey(key);
    if (!p) return;
    auto* region = regionOf(p->dim);
    if (!region) return;
    for (Arm const* a : {&post.up, &post.down}) {
        if (!a->active) continue;
        dropItem(*region, Vec3{p->x + 0.5f, p->y + 0.5f, p->z + 0.5f}, makeItem(signPostItemId(a->wood)));
    }
}

// ---- boards -------------------------------------------------------------------------------------
Board* Store::board(std::string const& key) {
    auto it = mBoards.find(key);
    return it == mBoards.end() ? nullptr : &it->second;
}

void Store::saveBoard(std::string const& key, Board const& b) {
    game::db::save(
        dbKey(kBoardPrefix, key),
        {{"item", b.item}, {"page", b.page}, {"color", b.color}, {"glowing", b.glowing}, {"powered", b.powered}, {"entity", b.entity}}
    );
}

std::vector<std::string> Store::boardLines(Board const& b, int* pageCount) const {
    if (pageCount) *pageCount = 0;
    if (b.item.empty()) return {};
    auto item  = itemFromSnbt(b.item);
    auto pages = bookPages(item);
    if (!pages.empty()) {
        if (pageCount) *pageCount = static_cast<int>(pages.size());
        int page = b.page % static_cast<int>(pages.size());
        return wrapText(stripFormatting(pages[static_cast<std::size_t>(page)]), kBoardLineWidth);
    }
    if (item.isNull()) return {};
    auto id = itemId(item);
    if (id == "minecraft:writable_book" || id == "minecraft:written_book") return {};
    auto name = item.getCustomName();
    if (name.empty()) name = item.getName();
    return wrapText(stripFormatting(name), kBoardLineWidth);
}

void Store::syncBoard(std::string const& key, Board& b) {
    auto p = parsePosKey(key);
    if (!p) return;
    auto* region = regionOf(p->dim);
    if (!region) return;
    auto before = b.entity;
    auto lines  = boardLines(b);
    if (lines.empty()) {
        dropDisplay(b.entity);
    } else {
        int  facing = loadedAt(*region, game::blockPos({p->dim, p->x, p->y, p->z}))
                        ? blockFacing(*region, game::blockPos({p->dim, p->x, p->y, p->z}))
                        : 2;
        auto pos    = boardDisplayPos(*p, facing);
        if (auto* actor = ensureDisplay(*region, b.entity, kTextDisplayEntity, pos, nullptr)) {
            DisplayInfo info{DisplayKind::Board, facing, b.color, b.glowing};
            applyDisplay(*actor, pos, packDisplay(info), static_cast<int>(lines.size()), colouredLines(lines, b.color));
        }
    }
    if (b.entity != before) saveBoard(key, b);
}

void Store::putBoard(std::string const& key, Board board) {
    auto& stored = mBoards[key];
    if (!board.entity) board.entity = stored.entity;
    stored = std::move(board);
    saveBoard(key, stored);
    syncBoard(key, stored);
}

void Store::removeBoard(std::string const& key, bool drop) {
    auto it = mBoards.find(key);
    if (it == mBoards.end()) return;
    Board b = std::move(it->second);
    mBoards.erase(it);
    game::db::erase(dbKey(kBoardPrefix, key));
    dropDisplay(b.entity);
    if (mNoDrop.erase(key)) drop = false;
    if (!drop || b.item.empty()) return;
    auto p = parsePosKey(key);
    if (!p) return;
    if (auto* region = regionOf(p->dim)) dropItem(*region, Vec3{p->x + 0.5f, p->y + 0.5f, p->z + 0.5f}, itemFromSnbt(b.item));
}

// ---- doormats -----------------------------------------------------------------------------------
Mat* Store::mat(std::string const& key) {
    auto it = mMats.find(key);
    return it == mMats.end() ? nullptr : &it->second;
}

void Store::saveMat(std::string const& key, Mat const& m) {
    game::db::save(
        dbKey(kMatPrefix, key),
        {{"lines", m.lines}, {"color", m.color}, {"glowing", m.glowing}, {"waxed", m.waxed}, {"entity", m.entity}}
    );
}

void Store::syncMat(std::string const& key, Mat& m) {
    auto p = parsePosKey(key);
    if (!p) return;
    auto* region = regionOf(p->dim);
    if (!region) return;
    auto before = m.entity;
    // keep inner empty lines (their position matters), drop trailing ones
    std::vector<std::string> lines(m.lines.begin(), m.lines.end());
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.empty()) {
        dropDisplay(m.entity);
    } else {
        auto pos    = matDisplayPos(*p);
        int  facing = loadedAt(*region, game::blockPos({p->dim, p->x, p->y, p->z}))
                        ? blockFacing(*region, game::blockPos({p->dim, p->x, p->y, p->z}))
                        : 2;
        if (auto* actor = ensureDisplay(*region, m.entity, kTextDisplayEntity, pos, nullptr)) {
            DisplayInfo info{DisplayKind::Doormat, facing, m.color, m.glowing};
            applyDisplay(*actor, pos, packDisplay(info), static_cast<int>(lines.size()), colouredLines(lines, m.color));
        }
    }
    if (m.entity != before) saveMat(key, m);
}

void Store::putMat(std::string const& key, Mat mat) {
    auto& stored = mMats[key];
    if (!mat.entity) mat.entity = stored.entity;
    stored = std::move(mat);
    saveMat(key, stored);
    syncMat(key, stored);
}

void Store::removeMat(std::string const& key) {
    auto it = mMats.find(key);
    if (it == mMats.end()) return;
    dropDisplay(it->second.entity);
    mMats.erase(it);
    game::db::erase(dbKey(kMatPrefix, key));
    mNoDrop.erase(key);
}

// ---- upkeep -------------------------------------------------------------------------------------
void Store::onBlockChanged(int dim, BlockPos const& pos, std::string const& newBlock) {
    auto key = posKey(dim, pos.x, pos.y, pos.z);
    if (mPosts.contains(key) && !isPostBlock(newBlock)) removePost(key, true);
    if (mBoards.contains(key) && newBlock != kNoticeBoard) removeBoard(key, true);
    if (mMats.contains(key) && newBlock != kDoormat) removeMat(key);
}

void Store::reconcile(bool sweepOrphans) {
    if (!ll::service::getLevel()) return;
    std::vector<std::string> gone;
    for (auto& [key, post] : mPosts) {
        auto p = parsePosKey(key);
        auto* region = p ? regionOf(p->dim) : nullptr;
        if (!region || !loadedAt(*region, game::blockPos({p->dim, p->x, p->y, p->z}))) continue;
        if (!isPostBlock(game::blockIdAt(*region, game::blockPos({p->dim, p->x, p->y, p->z})))) {
            gone.push_back(key);
            continue;
        }
        if (!findActor(post.entity)) syncPost(key, post);
    }
    for (auto const& k : gone) removePost(k, true);
    gone.clear();
    for (auto& [key, b] : mBoards) {
        auto p = parsePosKey(key);
        auto* region = p ? regionOf(p->dim) : nullptr;
        if (!region || !loadedAt(*region, game::blockPos({p->dim, p->x, p->y, p->z}))) continue;
        if (game::blockIdAt(*region, game::blockPos({p->dim, p->x, p->y, p->z})) != kNoticeBoard) {
            gone.push_back(key);
            continue;
        }
        if (!b.item.empty() && !findActor(b.entity)) syncBoard(key, b);
    }
    for (auto const& k : gone) removeBoard(k, true);
    gone.clear();
    for (auto& [key, m] : mMats) {
        auto p = parsePosKey(key);
        auto* region = p ? regionOf(p->dim) : nullptr;
        if (!region || !loadedAt(*region, game::blockPos({p->dim, p->x, p->y, p->z}))) continue;
        if (game::blockIdAt(*region, game::blockPos({p->dim, p->x, p->y, p->z})) != kDoormat) {
            gone.push_back(key);
            continue;
        }
        if (!findActor(m.entity)) syncMat(key, m);
    }
    for (auto const& k : gone) removeMat(k);
    mNoDrop.clear();

    if (!sweepOrphans) return;
    std::unordered_set<std::int64_t> owned;
    for (auto const& [k, v] : mPosts) owned.insert(v.entity);
    for (auto const& [k, v] : mBoards) owned.insert(v.entity);
    for (auto const& [k, v] : mMats) owned.insert(v.entity);
    for (auto* actor : ll::service::getLevel()->getRuntimeActorList()) {
        if (!actor) continue;
        auto type = actor->getTypeName();
        if (type != kSignPostEntity && type != kTextDisplayEntity) continue;
        if (!owned.contains(actor->getOrCreateUniqueID().rawID)) removeActor(*actor);
    }
}

namespace {
int signalFrom(BlockSource& region, BlockPos const& n) {
    auto const& b  = region.getBlock(n);
    auto        id = game::blockId(b);
    if (id == "minecraft:redstone_block" || id == "minecraft:redstone_torch" || id == "minecraft:powered_repeater"
        || id == "minecraft:powered_comparator")
        return 15;
    if (auto s = game::intState(b, "redstone_signal"); s && *s > 0) return *s;
    if (id == "minecraft:lever" && game::intState(b, "open_bit").value_or(0)) return 15;
    if (id.ends_with("_button") && game::intState(b, "button_pressed_bit").value_or(0)) return 15;
    if (id == "minecraft:observer" && game::intState(b, "powered_bit").value_or(0)) return 15;
    return 0;
}
} // namespace

void Store::tickBoards() {
    for (auto& [key, b] : mBoards) {
        auto p = parsePosKey(key);
        auto* region = p ? regionOf(p->dim) : nullptr;
        BlockPos pos = game::blockPos({p ? p->dim : 0, p ? p->x : 0, p ? p->y : 0, p ? p->z : 0});
        if (!region || !loadedAt(*region, pos)) continue;
        bool powered = false;
        for (BlockPos n : {BlockPos{pos.x + 1, pos.y, pos.z}, BlockPos{pos.x - 1, pos.y, pos.z}, BlockPos{pos.x, pos.y + 1, pos.z},
                           BlockPos{pos.x, pos.y - 1, pos.z}, BlockPos{pos.x, pos.y, pos.z + 1}, BlockPos{pos.x, pos.y, pos.z - 1}}) {
            if (signalFrom(*region, n) > 0) {
                powered = true;
                break;
            }
        }
        if (powered == b.powered) continue;
        b.powered = powered;
        int pages = 0;
        (void)boardLines(b, &pages);
        if (powered && pages > 1) {
            b.page = (b.page + 1) % pages;
            playSound(p->dim, Vec3{p->x + 0.5f, p->y + 0.5f, p->z + 0.5f}, "item.book.page_turn");
            log().info("sign: notice board {} turned to page {}", key, b.page + 1);
        }
        saveBoard(key, b);
        syncBoard(key, b);
        if (auto const* cur = &region->getBlock(pos); game::blockId(*cur) == kNoticeBoard) {
            if (auto const* nb = game::blockWith(
                    kNoticeBoard,
                    {{kFacingState, blockFacing(*region, pos)}, {kHasBookState, b.item.empty() ? 0 : 1}, {kPoweredState, powered ? 1 : 0}}
                )) {
                if (nb != cur) game::setBlock(*region, pos, *nb);
            }
        }
    }
}

} // namespace cm::feature::sign
