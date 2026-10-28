// Book piles: sneak-place books on the floor (standing pile for books / enchanted books, lying
// pile for written books / books and quills), up to 4; they add enchanting power.

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/managers/models/EnchantingContainerManagerModel.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"

#include "feature/tableware/Tableware.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::tableware::book_pile {

namespace {
using Sound = SharedTypes::Legacy::LevelSoundEvent;

enum class Kind { None, Book, Enchanted, Written, Writable };

Kind kindOf(std::string const& id) {
    if (id == "minecraft:book") return Kind::Book;
    if (id == "minecraft:enchanted_book") return Kind::Enchanted;
    if (id == "minecraft:written_book") return Kind::Written;
    if (id == "minecraft:writable_book") return Kind::Writable;
    return Kind::None;
}

bool lyingKind(Kind k) { return k == Kind::Written || k == Kind::Writable; }

bool enabled(Kind k) {
    if (k == Kind::None) return false;
    return lyingKind(k) ? config().writtenBooks : config().placeableBooks;
}

bool isPile(Block const& b) { return is(b, kBookPile) || is(b, kBookPileLying); }

/// Whether a book of kind `k` may go into a pile of type `lying`.
bool fits(Kind k, bool lying) { return config().mixedBooks || lyingKind(k) == lying; }

/// The visual state value of a book in a pile.
int visualOf(Kind k, bool lying) {
    if (lying) return k == Kind::Writable || k == Kind::Book ? 2 : 1;
    if (k == Kind::Enchanted) return 7;
    return 1 + randomInt(6); // plain covers 1..6
}

std::string slotState(int slot) { return "cm:book" + std::to_string(slot); }

int countOf(Block const& b) {
    int n = 0;
    while (n < 4 && state(b, slotState(n)) > 0) ++n;
    return n;
}

std::string dbKey(int dim, BlockPos const& pos) { return posKey("books", dim, pos); }

std::vector<std::string> loadBooks(int dim, BlockPos const& pos) {
    std::vector<std::string> out;
    if (auto data = game::db::load(dbKey(dim, pos)); data && data->contains("items") && (*data)["items"].is_array()) {
        for (auto const& s : (*data)["items"]) {
            if (s.is_string()) out.push_back(s.get<std::string>());
        }
    }
    return out;
}

void saveBooks(int dim, BlockPos const& pos, std::vector<std::string> const& items) {
    if (items.empty()) {
        game::db::erase(dbKey(dim, pos));
    } else {
        game::db::save(dbKey(dim, pos), {{"items", items}});
    }
}

/// Fallback item for a slot whose NBT is unknown (pile made by a command).
ItemStack itemForVisual(int value, bool lying) {
    if (lying) return makeItem(value == 2 ? "minecraft:writable_book" : "minecraft:written_book");
    return makeItem(value == 7 ? "minecraft:enchanted_book" : "minecraft:book");
}

ItemStack singleOf(ItemStack const& held) {
    ItemStack one = held;
    one.set(1);
    return one;
}

int dimOf(BlockSource const& region) { return static_cast<int>(region.getDimensionId()); }

/// Piles we remove ourselves (last book taken): their removal drops nothing.
std::unordered_set<std::string>& suppressed() {
    static std::unordered_set<std::string> s;
    return s;
}
} // namespace

bool onPlaceUse(UseContext const& ctx, bool apply) {
    if (isPile(ctx.block) || ctx.face != FacingID::Up || !ctx.player.isSneaking()) return false;
    auto kind = kindOf(itemId(ctx.held));
    if (!enabled(kind)) return false;
    BlockPos above{ctx.pos.x, ctx.pos.y + 1, ctx.pos.z};
    if (!game::replaceable(ctx.region, above)) return false;
    bool lying = lyingKind(kind);
    if (!apply) return true;
    auto const* pile = make(
        lying ? kBookPileLying : kBookPile,
        {{"minecraft:cardinal_direction", directionTowards(ctx.player)}, {"cm:book0", visualOf(kind, lying)}}
    );
    if (!pile || !place(ctx.region, above, *pile)) return false;
    saveBooks(dimOf(ctx.region), above, {itemToSnbt(singleOf(ctx.held))});
    consumeHeld(ctx.player);
    playSound(ctx.player, static_cast<int>(Sound::Place), above, static_cast<int>(pile->mNetworkId));
    log().info("tableware: placed a book pile");
    return true;
}

bool onUse(UseContext const& ctx, bool apply) {
    if (!isPile(ctx.block)) return false;
    bool lying = is(ctx.block, kBookPileLying);
    int  count = countOf(ctx.block);
    int  dim   = dimOf(ctx.region);
    if (ctx.player.isSneaking()) {
        auto kind = kindOf(itemId(ctx.held));
        if (!enabled(kind) || !fits(kind, lying) || count >= 4) return false;
        if (!apply) return true;
        auto const* next = with(ctx.block, {{slotState(count), visualOf(kind, lying)}});
        if (!next) return false;
        auto books = loadBooks(dim, ctx.pos);
        books.resize(count);
        books.push_back(itemToSnbt(singleOf(ctx.held)));
        place(ctx.region, ctx.pos, *next);
        saveBooks(dim, ctx.pos, books);
        consumeHeld(ctx.player);
        playSound(ctx.player, static_cast<int>(Sound::Place), ctx.pos, static_cast<int>(next->mNetworkId));
        log().info("tableware: added a book to a pile ({})", count + 1);
        return true;
    }
    if (!isEmpty(ctx.held) || count == 0) return false;
    if (!apply) return true;
    auto books = loadBooks(dim, ctx.pos);
    int  slot  = count - 1;
    auto item  = slot < static_cast<int>(books.size()) ? itemFromSnbt(books[slot]) : std::nullopt;
    if (!item) item = itemForVisual(state(ctx.block, slotState(slot)), lying);
    if (static_cast<int>(books.size()) > slot) books.resize(slot);
    if (count == 1) {
        saveBooks(dim, ctx.pos, {});
        suppressed().insert(dbKey(dim, ctx.pos));
        game::clearBlock(ctx.region, ctx.pos);
        suppressed().erase(dbKey(dim, ctx.pos));
    } else {
        auto const* next = with(ctx.block, {{slotState(slot), 0}});
        if (!next) return false;
        saveBooks(dim, ctx.pos, books);
        place(ctx.region, ctx.pos, *next);
    }
    giveOrDrop(ctx.player, *item);
    playSound(ctx.player, static_cast<int>(Sound::ItemUseOn), ctx.pos);
    log().info("tableware: took a book from a pile");
    return true;
}

void onRemoved(int dim, BlockPos const& pos, Block const& prev) {
    if (suppressed().contains(dbKey(dim, pos))) return;
    bool  lying = is(prev, kBookPileLying);
    int   count = countOf(prev);
    auto  books = loadBooks(dim, pos);
    saveBooks(dim, pos, {});
    std::vector<ItemStack> drops;
    for (int i = 0; i < count; ++i) {
        auto item = i < static_cast<int>(books.size()) ? itemFromSnbt(books[i]) : std::nullopt;
        drops.push_back(item ? *item : itemForVisual(state(prev, slotState(i)), lying));
    }
    dropLater(dim, pos, std::move(drops));
}

float enchantPower(BlockSource& region, BlockPos const& table) {
    float power = 0.0f;
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dz = -2; dz <= 2; ++dz) {
            if (std::abs(dx) < 2 && std::abs(dz) < 2) continue;
            for (int dy = 0; dy <= 1; ++dy) {
                BlockPos p{table.x + dx, table.y + dy, table.z + dz};
                BlockPos gap{table.x + dx / 2, table.y + dy, table.z + dz / 2};
                if (!region.hasChunksAt(p, 0, false)) continue;
                auto const& b = region.getBlock(p);
                if (!isPile(b) || !region.getBlock(gap).isAir()) continue;
                bool lying = is(b, kBookPileLying);
                for (int i = 0; i < 4; ++i) {
                    int v = state(b, slotState(i));
                    if (v == 0) break;
                    power += (!lying && v == 7) ? config().enchantedBookPower : config().bookPower;
                }
            }
        }
    }
    return power;
}

namespace {
struct TableContext {
    BlockSource* region = nullptr;
    BlockPos     pos;
};
thread_local TableContext gTable;

LL_TYPE_INSTANCE_HOOK(
    EnchantRecalcHook,
    ll::memory::HookPriority::Normal,
    EnchantingContainerManagerModel,
    &EnchantingContainerManagerModel::recalculateOptions,
    void
) {
    Player& player = this->mPlayer;
    if (player.isClientSide()) return origin();
    auto saved = gTable;
    gTable     = {&player.getDimensionBlockSource(), this->mBlockPos};
    origin();
    gTable = saved;
}

LL_STATIC_HOOK(
    EnchantCostsHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::getEnchantCosts,
    std::vector<int>,
    IRandom&             random,
    ItemStackBase const& item,
    int                  bookcaseCount
) {
    if (gTable.region) {
        float extra   = enchantPower(*gTable.region, gTable.pos);
        bookcaseCount = std::min(15, static_cast<int>(std::floor(static_cast<float>(bookcaseCount) + extra)));
    }
    return origin(random, item, bookcaseCount);
}
} // namespace

void installHooks() {
    EnchantRecalcHook::hook();
    EnchantCostsHook::hook();
}

void uninstallHooks() {
    EnchantRecalcHook::unhook();
    EnchantCostsHook::unhook();
}

} // namespace cm::feature::tableware::book_pile
