// Comparator output for the instrument blocks.
//
// Data-driven blocks have no analog output. Each of our BlockType objects gets a private copy of its
// vtable in which `hasComparatorSignal` / `getComparatorSignal` point to the functions below, so no other
// block type is touched (the base implementations are folded with unrelated functions and can't be hooked).
// Block types can be recreated per world on the client, so this runs on every world load; the copies are
// kept for the whole process (a block type may still point at one) and `uninstall` restores the original
// vtables of the current block types.

#include <memory>
#include <optional>
#include <vector>

#include "ll/api/memory/Memory.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/instrument/Instrument.h"
#include "mod/Log.h"

namespace cm::feature::instrument {

namespace {
/// Entries copied: every virtual function of BlockType (the last one is `entityInside`) plus a few spare
/// slots in case the data-driven block class appends some.
constexpr std::size_t kSpareSlots = 8;

struct Table {
    void**                   original = nullptr;
    std::unique_ptr<void*[]> entries; ///< [0] = RTTI locator, [1..] = vtable
    [[nodiscard]] void**     vptr() const { return entries.get() + 1; }
};
std::vector<Table>& tables() {
    static std::vector<Table> v;
    return v;
}

bool hasSignal(BlockType const*) { return true; }

int getSignal(BlockType const*, BlockSource& region, BlockPos const& pos, Block const& block, unsigned char) {
    auto kind = kindOf(block);
    if (kind == Kind::WindVane) return state(block, "complementaries:wind");
    if (auto* rec = find(core::GlobalPos{static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z})) {
        return rec->signal;
    }
    return 0;
}

Table const* ours(void** vptr) {
    for (auto const& t : tables()) {
        if (t.vptr() == vptr) return &t;
    }
    return nullptr;
}

void setVptr(BlockType const* type, void** vptr) {
    auto* t = const_cast<BlockType*>(type);
    ll::memory::modify(t, sizeof(void*), [&] { *reinterpret_cast<void***>(t) = vptr; });
}

std::vector<BlockType const*> ourTypes() {
    std::vector<BlockType const*> out;
    for (auto kind : {Kind::Globe, Kind::GlobeSepia, Kind::Clock, Kind::Hourglass, Kind::WindVane}) {
        if (auto type = BlockType::tryGetFromRegistry(HashedString(std::string(idOf(kind))))) out.push_back(type.as_ptr());
    }
    return out;
}
} // namespace

void installComparators() {
    auto hasIdx  = ll::memory::getVtableIndex(&BlockType::hasComparatorSignal);
    auto getIdx  = ll::memory::getVtableIndex(&BlockType::getComparatorSignal);
    auto lastIdx = ll::memory::getVtableIndex(&BlockType::entityInside);
    if (!hasIdx || !getIdx || !lastIdx || *hasIdx > *lastIdx || *getIdx > *lastIdx) {
        log().warn("instrument: comparator vtable slots not found; comparators won't read instruments");
        return;
    }
    std::size_t const count = *lastIdx + 1 + kSpareSlots;
    for (auto const* type : ourTypes()) {
        auto** vptr = *reinterpret_cast<void** const*>(type);
        if (ours(vptr)) continue;
        Table const* table = nullptr;
        for (auto const& t : tables()) {
            if (t.original == vptr) table = &t;
        }
        if (!table) {
            Table t;
            t.original   = vptr;
            t.entries    = std::make_unique<void*[]>(count + 1);
            t.entries[0] = vptr[-1];
            for (std::size_t i = 0; i < count; ++i) t.entries[i + 1] = vptr[i];
            t.entries[*hasIdx + 1] = reinterpret_cast<void*>(&hasSignal);
            t.entries[*getIdx + 1] = reinterpret_cast<void*>(&getSignal);
            tables().push_back(std::move(t));
            table = &tables().back();
        }
        setVptr(type, table->vptr());
    }
}

void uninstallComparators() {
    for (auto const* type : ourTypes()) {
        if (auto const* t = ours(*reinterpret_cast<void** const*>(type))) setVptr(type, t->original);
    }
}

bool comparatorsInstalled() {
    auto types = ourTypes();
    if (types.empty()) return false;
    for (auto const* type : types) {
        if (!ours(*reinterpret_cast<void** const*>(type))) return false;
    }
    return true;
}

} // namespace cm::feature::instrument
