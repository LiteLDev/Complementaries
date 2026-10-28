// Slice map: a locked vanilla filled map whose pixels we draw ourselves, showing the world cut at
// a height. Maps are remembered in the world DB (`cm:tool:slice:<mapId>`) and redrawn around the
// players holding them.

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include "mc/deps/core/math/Color.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/Int64Tag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"
#include "mc/world/level/saveddata/maps/MapItemTrackedActor.h"

#include "feature/tool/Tool.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::tool::slicemap {

namespace {

constexpr int kSize     = 128;
constexpr int kMaxDepth = 64;
constexpr int kInitialColumns = 4096; ///< drawn at once when the map is made (the rest while held)
constexpr std::uint32_t kVoid = 0xFF161210; ///< ABGR: nothing below the slice

struct Slice {
    int dim = 0;
    int y   = 0;
    int cx  = 0; ///< world x of map pixel 0
    int cz  = 0;
    int cursor = 0;
};

std::map<std::int64_t, Slice>& slices() {
    static std::map<std::int64_t, Slice> m;
    return m;
}

std::string dbKey(std::int64_t id) { return std::format("cm:tool:slice:{}", id); }

Slice* sliceOf(std::int64_t id) {
    auto& m = slices();
    if (auto it = m.find(id); it != m.end()) return &it->second;
    auto j = game::db::load(dbKey(id));
    if (!j || !j->is_object()) return nullptr;
    Slice s{j->value("dim", 0), j->value("y", 0), j->value("x0", 0), j->value("z0", 0), 0};
    return &(m[id] = s);
}

MapItemSavedData* dataOf(std::int64_t id) {
    auto* l = level();
    if (!l) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return l->getMapSavedData(uid);
}

std::uint32_t abgr(mce::Color const& c, float shade) {
    auto byte = [shade](float f) { return static_cast<std::uint32_t>(std::lround(std::clamp(f * shade, 0.0f, 1.0f) * 255.0f)); };
    return 0xFF000000u | (byte(c.b) << 16) | (byte(c.g) << 8) | byte(c.r);
}

/// The colour of column (wx, wz) seen from the slice height down.
std::optional<std::uint32_t> sample(BlockSource& region, int wx, int y, int wz) {
    BlockPos top{wx, y, wz};
    if (!region.hasChunksAt(top, 0, false)) return std::nullopt;
    int minY = region.getMinHeight();
    for (int dy = 0; dy < kMaxDepth; ++dy) {
        BlockPos p{wx, y - dy, wz};
        if (p.y < minY) break;
        auto const& b = region.getBlock(p);
        if (b.isAir()) continue;
        auto c = b.getBlockType().getMapColor(region, p, b);
        if (c.a <= 0.01f) continue;
        // walls cut by the slice are a bit darker than the floor below, deeper floors fade out
        float shade = dy == 0 ? 0.78f : std::max(0.45f, 1.0f - static_cast<float>(dy - 1) * 0.03f);
        return abgr(c, shade);
    }
    return kVoid;
}

void markDirty(MapItemSavedData& data, int x0, int z0, int x1, int z1) {
    data.mDirtyForSave   = true;
    data.mDirtyPixelData = true;
    for (auto& tracked : *data.mTrackedEntities) {
        if (!tracked) continue;
        auto& t = *tracked;
        if (t.mMinDirtyX > t.mMaxDirtyX || t.mMinDirtyY > t.mMaxDirtyY) {
            t.mMinDirtyX = static_cast<uint>(x0);
            t.mMinDirtyY = static_cast<uint>(z0);
            t.mMaxDirtyX = static_cast<uint>(x1);
            t.mMaxDirtyY = static_cast<uint>(z1);
        } else {
            t.mMinDirtyX = std::min<uint>(t.mMinDirtyX, static_cast<uint>(x0));
            t.mMinDirtyY = std::min<uint>(t.mMinDirtyY, static_cast<uint>(z0));
            t.mMaxDirtyX = std::max<uint>(t.mMaxDirtyX, static_cast<uint>(x1));
            t.mMaxDirtyY = std::max<uint>(t.mMaxDirtyY, static_cast<uint>(z1));
        }
    }
}

std::string loreFor(int y) { return fmtCode("r7") + tr("message.supplementaries.slice_map", "Height: {}", y); }

} // namespace

std::int64_t mapIdOf(ItemStackBase const& item) {
    if (!isItem(item, "minecraft:filled_map") || !item.mUserData) return 0;
    auto const& tag = *item.mUserData;
    if (!tag.contains("map_uuid")) return 0;
    auto const& v = tag["map_uuid"];
    if (v.hold<Int64Tag>()) return v.get<Int64Tag>().data;
    if (v.hold<IntTag>()) return v.get<IntTag>().data;
    return 0;
}

std::optional<int> sliceHeight(ItemStackBase const& item) {
    if (!isItem(item, "minecraft:filled_map") || !item.mUserData || !item.mUserData->contains("cm_slice")) return std::nullopt;
    return intData(item, "cm_slice", 0);
}

int render(std::int64_t mapId, BlockPos const& around, int radius, int columns) {
    auto* s    = sliceOf(mapId);
    auto* data = dataOf(mapId);
    auto* region = s ? regionOf(s->dim) : nullptr;
    if (!s || !data || !region) return 0;
    auto& pixels = *data->mPixels;
    if (pixels.size() < static_cast<size_t>(kSize * kSize)) pixels.resize(kSize * kSize, 0);
    int  drawn = 0, visited = 0;
    int  x0 = kSize, z0 = kSize, x1 = -1, z1 = -1;
    long r2 = static_cast<long>(radius) * radius;
    while (drawn < columns && visited < kSize * kSize) {
        int idx  = s->cursor;
        s->cursor = (s->cursor + 1) % (kSize * kSize);
        ++visited;
        int  px = idx % kSize, pz = idx / kSize;
        int  wx = s->cx + px, wz = s->cz + pz;
        long dx = wx - around.x, dz = wz - around.z;
        if (dx * dx + dz * dz > r2) continue;
        auto c = sample(*region, wx, s->y, wz);
        if (!c) continue;
        ++drawn;
        if (pixels[idx] == *c) continue;
        pixels[idx] = *c;
        x0 = std::min(x0, px);
        z0 = std::min(z0, pz);
        x1 = std::max(x1, px);
        z1 = std::max(z1, pz);
    }
    if (x1 >= 0) markDirty(*data, x0, z0, x1, z1);
    return drawn;
}

std::optional<std::uint32_t> pixel(std::int64_t mapId, int x, int z) {
    auto* data = dataOf(mapId);
    if (!data || x < 0 || z < 0 || x >= kSize || z >= kSize) return std::nullopt;
    auto& pixels = *data->mPixels;
    if (pixels.size() < static_cast<size_t>(kSize * kSize)) return std::nullopt;
    return pixels[z * kSize + x];
}

ItemStack create(int dim, BlockPos const& centerPos, int y) {
    auto* l = level();
    if (!l) return ItemStack::EMPTY_ITEM();
    // the same grid as vanilla maps of scale 0: 128 x 128 blocks
    int  x0  = static_cast<int>(std::floor((centerPos.x + 64.0) / kSize)) * kSize - 64;
    int  z0  = static_cast<int>(std::floor((centerPos.z + 64.0) / kSize)) * kSize - 64;
    auto uid = l->getNewUniqueID();
    auto& data = l->createMapSavedData(uid, BlockPos{x0 + 64, 0, z0 + 64}, DimensionType(dim), 0);
    data.mLocked = true;
    data.mPixels->assign(kSize * kSize, kVoid);
    ItemStack map = makeItem("minecraft:filled_map");
    if (map.isNull()) return map;
    MapItem::setItemInstanceInfo(map, data);
    setIntData(map, "cm_slice", y);
    setLoreLine(map, loreFor(y).substr(0, 6), loreFor(y));
    slices()[uid.rawID] = Slice{dim, y, x0, z0, 0};
    game::db::save(dbKey(uid.rawID), {{"dim", dim}, {"y", y}, {"x0", x0}, {"z0", z0}});
    render(uid.rawID, centerPos, static_cast<int>(64 * std::max(0.1f, config().sliceMap.rangeMultiplier)), kInitialColumns);
    log().info("slice map {} created at height {}", uid.rawID, y);
    return map;
}

bool useInAir(Player& player) {
    if (!config().sliceMap.enabled) return false;
    if (!isItem(player.getSelectedItem(), ids::EmptySliceMap)) return false;
    int  y   = static_cast<int>(std::floor(eyePos(player).y));
    auto map = create(static_cast<int>(player.getDimensionId()), player.getFeetBlockPos(), y);
    if (map.isNull()) return false;
    bool creative = isCreative(player);
    if (!creative && player.getSelectedItem().mCount <= 1) {
        setMainhand(player, map);
    } else {
        consumeMainhand(player, 1);
        ItemStack copy = map;
        if (!player.add(copy)) player.drop(map, false);
        player.sendInventory(false);
    }
    playSound(static_cast<int>(player.getDimensionId()), player.getPosition(), "ui.cartography_table.take_result", 1.0f, 1.0f);
    return true;
}

void tick() {
    if (!config().sliceMap.enabled) return;
    auto* l = level();
    if (!l) return;
    int radius = static_cast<int>(64 * std::max(0.1f, config().sliceMap.rangeMultiplier));
    l->forEachPlayer([radius](Player& p) {
        for (auto const* item : {&p.getSelectedItem(), &p.getOffhandSlot()}) {
            if (!sliceHeight(*item)) continue;
            auto id = mapIdOf(*item);
            auto* s = id ? sliceOf(id) : nullptr;
            if (s && s->dim == static_cast<int>(p.getDimensionId())) render(id, p.getFeetBlockPos(), radius, 1024);
        }
        return true;
    });
}

void clear() { slices().clear(); }

} // namespace cm::feature::tool::slicemap
