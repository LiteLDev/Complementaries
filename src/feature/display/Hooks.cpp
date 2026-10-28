// U12 display: comparator output of the display blocks and the enchanting power of end crystals
// on pedestals.

#include <algorithm>
#include <cmath>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/memory/Symbol.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/display/Display.h"
#include "feature/display/Internal.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace cm::feature::display {

namespace {

// ---- comparator --------------------------------------------------------------------------------------
//
// Data-driven blocks are plain `BlockType`s whose `hasComparatorSignal` / `getComparatorSignal` are
// folded "return 0" bodies (shared with many other functions, so they can't be hooked). Our three
// block types get their own copy of the BlockType vtable with those two entries replaced.

constexpr size_t kVtableEntries = 132; ///< virtual functions of BlockType in the 26.51.7 headers

bool hasComparatorSignalImpl(BlockType const*) { return true; }

int getComparatorSignalImpl(BlockType const*, BlockSource& region, BlockPos const& pos, Block const&, uchar) {
    return comparatorSignal(game::globalPos(region, pos));
}

struct VtablePatch {
    void*              table[kVtableEntries + 1]{}; ///< [0] = RTTI complete object locator
    bool               ready = false;
    void**             original = nullptr;
    std::vector<BlockType*> patched;
    bool               warned = false;
};

VtablePatch& vp() {
    static VtablePatch p;
    return p;
}

void patchComparators() {
    auto& p   = vp();
    auto  has = ll::memory::getVtableIndex(&BlockType::hasComparatorSignal);
    auto  get = ll::memory::getVtableIndex(&BlockType::getComparatorSignal);
    if (!has || !get || *has >= kVtableEntries || *get >= kVtableEntries) {
        if (!p.warned) log().warn("Display: comparator support unavailable (vtable index not found)");
        p.warned = true;
        return;
    }
    void* expected = ll::memory::SymbolView("??_7BlockType@@6B@").resolve(true);
    for (auto id : {kPedestal, kItemShelf, kStatue}) {
        auto type = BlockType::tryGetFromRegistry(HashedString(std::string(id)));
        if (!type) continue;
        auto*   bt   = const_cast<BlockType*>(&*type);
        void**& vptr = *reinterpret_cast<void***>(bt);
        if (vptr == &p.table[1]) {
            if (std::find(p.patched.begin(), p.patched.end(), bt) == p.patched.end()) p.patched.push_back(bt);
            continue;
        }
        if (expected && vptr != expected) {
            if (!p.warned) log().warn("Display: {} is not a plain BlockType, no comparator output", id);
            p.warned = true;
            continue;
        }
        if (!p.ready) {
            p.original = vptr;
            p.table[0] = vptr[-1];
            for (size_t i = 0; i < kVtableEntries; ++i) p.table[i + 1] = vptr[i];
            p.table[*has + 1] = reinterpret_cast<void*>(&hasComparatorSignalImpl);
            p.table[*get + 1] = reinterpret_cast<void*>(&getComparatorSignalImpl);
            p.ready           = true;
        }
        if (vptr != p.original) continue; // another vtable than the one we copied
        vptr = &p.table[1];
        p.patched.push_back(bt);
    }
}

void unpatchComparators() {
    auto& p = vp();
    for (auto* bt : p.patched) {
        void**& vptr = *reinterpret_cast<void***>(bt);
        if (vptr == &p.table[1]) vptr = p.original;
    }
    p.patched.clear();
}

// ---- enchanting ---------------------------------------------------------------------------------------
//
// Java: PedestalBlock#getEnchantPowerBonus. Bedrock counts the bookshelves returned by
// EnchantUtils::getBookCasePositions; a pedestal with an end crystal adds `crystalEnchanting` entries
// when it stands where a bookshelf would count.

LL_STATIC_HOOK(
    DisplayBookcaseHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::getBookCasePositions,
    std::vector<Vec3>,
    BlockSource& source,
    Vec3 const&  pos
) {
    auto out   = origin(source, pos);
    auto power = config::get<DisplayConfig>().crystalEnchanting;
    if (power <= 0.0 || source.getLevel().isClientSide()) return out;
    auto table  = game::blockPosOf(pos);
    int  copies = static_cast<int>(std::lround(power));
    for (int dy = 0; dy <= 1; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                if (std::abs(dx) != 2 && std::abs(dz) != 2) continue;
                BlockPos at{table.x + dx, table.y + dy, table.z + dz};
                BlockPos mid{table.x + dx / 2, table.y + dy, table.z + dz / 2};
                if (!source.getBlock(mid).isAir()) continue;
                if (kindOf(source.getBlock(at)) != Kind::Pedestal) continue;
                auto item = displayedItem(game::globalPos(source, at));
                if (!item || pedestalTypeOf(*item) != PedestalType::Crystal) continue;
                for (int i = 0; i < copies; ++i) {
                    out.emplace_back(static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z));
                }
            }
        }
    }
    return out;
}

} // namespace

namespace detail {

void onWorldReady() { patchComparators(); }

void onWorldClosed() {
    // Restore while the block types are still alive; the next world patches them again. This way no
    // BlockType pointer is kept across worlds.
    unpatchComparators();
    resetHatStands();
}

void installHooks() {
    DisplayBookcaseHook::hook();
    installHatStand();
}

void uninstallHooks() {
    DisplayBookcaseHook::unhook();
    uninstallHatStand();
    unpatchComparators();
}

} // namespace detail

} // namespace cm::feature::display
