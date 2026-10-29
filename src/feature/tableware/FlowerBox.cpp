// Flower box: three plant slots (left to right seen from the front), on the floor or on a wall.

#include <algorithm>
#include <array>
#include <cmath>

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"

#include "feature/tableware/Tableware.h"
#include "mod/Log.h"

namespace cm::feature::tableware::flower_box {

namespace {
using Sound = SharedTypes::Legacy::LevelSoundEvent;

constexpr std::array<std::string_view, 15> kPlants{
    "minecraft:dandelion",
    "minecraft:poppy",
    "minecraft:blue_orchid",
    "minecraft:allium",
    "minecraft:azure_bluet",
    "minecraft:red_tulip",
    "minecraft:orange_tulip",
    "minecraft:white_tulip",
    "minecraft:pink_tulip",
    "minecraft:oxeye_daisy",
    "minecraft:cornflower",
    "minecraft:lily_of_the_valley",
    "minecraft:wither_rose",
    "minecraft:oak_sapling",
    "minecraft:birch_sapling",
};

std::string slotState(int slot) { return "cm:flower" + std::to_string(slot); }

/// The slot under the click: the box's length split in three, left to right seen from the front.
int slotAt(Block const& block, Vec3 const& hit) {
    auto  dir = direction(block);
    float u;
    if (dir == "north") u = 1.0f - hit.x;
    else if (dir == "south") u = hit.x;
    else if (dir == "west") u = hit.z;
    else u = 1.0f - hit.z;
    return std::clamp(static_cast<int>(std::floor(u * 3.0f)), 0, 2);
}
} // namespace

int plantOf(std::string_view id) {
    for (int i = 0; i < static_cast<int>(kPlants.size()); ++i) {
        if (kPlants[i] == id) return i + 1;
    }
    return 0;
}

std::string_view plantItem(int value) {
    if (value < 1 || value > static_cast<int>(kPlants.size())) return {};
    return kPlants[value - 1];
}

bool onUse(UseContext const& ctx, bool apply) {
    if (!is(ctx.block, kFlowerBox)) return false;
    if (ctx.player.isSneaking() && !isEmpty(ctx.held)) return false; // sneak + item: the item's own use
    int  slot    = slotAt(ctx.block, ctx.hit);
    int  current = state(ctx.block, slotState(slot));
    auto held    = itemId(ctx.held);
    if (current == 0) {
        int plant = plantOf(held);
        if (plant == 0) return false;
        if (!apply) return true;
        auto const* next = with(
            ctx.block,
            {
                {slotState(slot), plant}
        }
        );
        if (!next) return false;
        place(ctx.region, ctx.pos, *next);
        consumeHeld(ctx.player);
        playSound(ctx.player, static_cast<int>(Sound::ItemUseOn), ctx.pos);
        log().info("tableware: planted {} in a flower box", held);
        return true;
    }
    if (!isEmpty(ctx.held)) return false;
    if (!apply) return true;
    auto const* next = with(
        ctx.block,
        {
            {slotState(slot), 0}
    }
    );
    if (!next) return false;
    place(ctx.region, ctx.pos, *next);
    giveOrDrop(ctx.player, makeItem(plantItem(current)));
    playSound(ctx.player, static_cast<int>(Sound::ItemUseOn), ctx.pos);
    log().info("tableware: took {} from a flower box", plantItem(current));
    return true;
}

void onPlaced(Player& player, BlockPos const& pos, FacingID face) {
    auto&       region = player.getDimensionBlockSource();
    auto const& block  = region.getBlock(pos);
    if (!is(block, kFlowerBox)) return;
    auto         wall = directionOfFace(face);
    Block const* next = wall.empty() ? with(
                                           block,
                                           {
                                               {"cm:wall", 0}
    }
                                       )
                                     : with(block, {{"cm:wall", 1}, {"minecraft:cardinal_direction", wall}});
    if (next && next != &block) place(region, pos, *next);
}

void onRemoved(int dim, BlockPos const& pos, Block const& prev) {
    std::vector<ItemStack> drops;
    for (int slot = 0; slot < 3; ++slot) {
        auto id = plantItem(state(prev, slotState(slot)));
        if (!id.empty()) drops.push_back(makeItem(id));
    }
    dropLater(dim, pos, std::move(drops));
}

} // namespace cm::feature::tableware::flower_box
