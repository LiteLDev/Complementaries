// Soap: washes dye (and antique ink) off blocks, sheep and items; bubbles in water.

#include <array>

#include "mc/deps/shared_types/legacy/actor/PaletteColor.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorColor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "feature/tool/Tool.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::tool::soap {

namespace {

constexpr std::array<std::string_view, 16> kColors = {
    "white",
    "orange",
    "magenta",
    "light_blue",
    "yellow",
    "lime",
    "pink",
    "gray",
    "light_gray",
    "cyan",
    "purple",
    "blue",
    "brown",
    "green",
    "red",
    "black",
};

/// The colour prefix of `path` ("red_wool" -> "red"), empty when none.
std::string_view colorPrefix(std::string_view path) {
    std::string_view best;
    for (auto c : kColors) {
        if (path.size() > c.size() && path.starts_with(c) && path[c.size()] == '_' && c.size() > best.size()) best = c;
    }
    return best;
}

/// The washed id of a block / item id, empty when it is already clean or not washable.
std::string washedId(std::string_view id) {
    if (!id.starts_with("minecraft:")) return {};
    auto path = id.substr(10);
    if (path == "sticky_piston") return "minecraft:piston";
    auto color = colorPrefix(path);
    if (color.empty()) return {};
    auto rest  = path.substr(color.size() + 1);
    bool white = color == "white";
    if (rest == "wool") return white ? "" : "minecraft:white_wool";
    if (rest == "carpet") return white ? "" : "minecraft:white_carpet";
    if (rest == "stained_glass") return "minecraft:glass";
    if (rest == "stained_glass_pane") return "minecraft:glass_pane";
    if (rest == "terracotta") return "minecraft:hardened_clay";
    if (rest == "shulker_box") return "minecraft:undyed_shulker_box";
    if (rest == "candle") return "minecraft:candle";
    if (rest == "candle_cake") return "minecraft:candle_cake";
    return {};
}

void effects(int dim, Vec3 const& pos) {
    particle(dim, pos, "complementaries:suds");
    playSound(dim, pos, "random.swim", 0.5f, 1.6f);
}

bool lookingAtWater(Player& player) {
    auto& region = player.getDimensionBlockSource();
    Vec3  from   = eyePos(player);
    Vec3  to     = from + lookVector(player) * 4.0f;
    auto  hit    = region.clip(
        from,
        to,
        true,
        ShapeType::Outline,
        5,
        false,
        false,
        nullptr,
        [](BlockSource const&, Block const&, bool) { return true; },
        true
    );
    return hit.mIsHitLiquid
        || (hit.mType == HitResultType::Tile && game::blockIdAt(region, hit.mBlock).find("water") != std::string::npos);
}

} // namespace

Block const* cleanBlock(Block const& block) {
    auto washed = washedId(block.getTypeName());
    if (washed.empty()) return nullptr;
    if (auto const* b = blockWithStates(washed, blockStates(block))) return b;
    return game::blockWith(washed);
}

bool washBlock(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (antique::isSign(block)) {
        bool antiqued = antique::removeFromSign(region, pos);
        bool cleaned  = antique::setSignLook(region, pos, 0xFF000000, false);
        return antiqued || cleaned;
    }
    auto const* clean = cleanBlock(block);
    if (!clean) return false;
    return replaceKeepingData(region, pos, *clean);
}

bool washItem(ItemStack& item) {
    if (item.isNull()) return false;
    bool changed = false;
    if (antique::isAntiqueItem(item)) {
        antique::setAntiqueItem(item, false);
        changed = true;
    }
    if (item.mUserData && item.mUserData->contains("customColor")) {
        removeData(item, "customColor"); // dyed leather / wolf armour
        changed = true;
    }
    if (isItem(item, "minecraft:banner") && item.mUserData && item.mUserData->contains("Patterns")) {
        removeData(item, "Patterns");
        changed = true;
    }
    auto washed = washedId(itemId(item));
    if (!washed.empty()) {
        ItemStack clean = makeItem(washed, item.mCount);
        if (!clean.isNull()) {
            if (item.mUserData) clean.mUserData = item.mUserData->clone(); // shulker contents, names
            item    = clean;
            changed = true;
        }
    }
    return changed;
}

bool washEntity(Actor& target) {
    if (!target.isType(ActorType::Sheep)) return false;
    auto& data  = *target.mEntityData;
    auto  color = data.getInt8(static_cast<ushort>(ActorDataIDs::ColorIndex));
    if (color == static_cast<schar>(SharedTypes::Legacy::PaletteColor::White)) return false;
    ActorColor::setColor(*data.mData, SharedTypes::Legacy::PaletteColor::White);
    return true;
}

bool useOnBlock(Player& player, BlockPos const& pos) {
    if (!config().soap.enabled) return false;
    auto& region = player.getDimensionBlockSource();
    if (!washBlock(region, pos)) return false;
    consumeMainhand(player, 1);
    effects(static_cast<int>(region.getDimensionId()), center(pos));
    log().info("soap washed the block at {} {} {}", pos.x, pos.y, pos.z);
    return true;
}

bool useOnEntity(Player& player, Actor& target) {
    if (!config().soap.enabled || !washEntity(target)) return false;
    consumeMainhand(player, 1);
    effects(static_cast<int>(target.getDimensionId()), target.getPosition() + Vec3{0, 0.8f, 0});
    log().info("soap washed a {}", target.getTypeName());
    return true;
}

bool useInAir(Player& player) {
    if (!config().soap.enabled) return false;
    int       dim   = static_cast<int>(player.getDimensionId());
    ItemStack other = player.getOffhandSlot();
    if (!other.isNull() && washItem(other)) {
        setOffhand(player, other);
        consumeMainhand(player, 1);
        effects(dim, eyePos(player) + lookVector(player) * 0.6f);
        log().info("soap washed {} for {}", itemId(other), player.getRealName());
        return true;
    }
    if (player.isInWater() || lookingAtWater(player)) {
        for (int i = 1; i <= 3; ++i)
            particle(dim, eyePos(player) + lookVector(player) * (0.8f * static_cast<float>(i)), "complementaries:suds");
        playSound(dim, player.getPosition(), "random.swim", 0.6f, 1.8f);
        return true;
    }
    return false;
}

} // namespace cm::feature::tool::soap
