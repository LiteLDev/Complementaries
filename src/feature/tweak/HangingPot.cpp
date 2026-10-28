// Hanging flower pots (docs/spec/tweak.md §1.3).

#include <array>

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "game/Blocks.h"

namespace cm::feature::tweak {

namespace {
// Block states hold at most 16 values, so the plant index i is stored as
// `complementaries:plant_group` = i / 16 and `complementaries:plant` = i % 16 (0 / 0 = empty).
constexpr char kGroup[] = "complementaries:plant_group";
constexpr char kPlant[] = "complementaries:plant";

// Index i + 1 is the plant index; the BP permutations use the same order
// (assets/pack/bp/blocks/hanging_flower_pot.json, tools/textures/tweak.ps1).
constexpr std::array<char const*, 32> kPlants = {
    "minecraft:poppy",          "minecraft:dandelion",        "minecraft:blue_orchid",       "minecraft:allium",
    "minecraft:azure_bluet",    "minecraft:red_tulip",        "minecraft:orange_tulip",      "minecraft:white_tulip",
    "minecraft:pink_tulip",     "minecraft:oxeye_daisy",      "minecraft:cornflower",        "minecraft:lily_of_the_valley",
    "minecraft:wither_rose",    "minecraft:torchflower",      "minecraft:oak_sapling",       "minecraft:spruce_sapling",
    "minecraft:birch_sapling",  "minecraft:jungle_sapling",   "minecraft:acacia_sapling",    "minecraft:dark_oak_sapling",
    "minecraft:cherry_sapling", "minecraft:mangrove_propagule", "minecraft:red_mushroom",    "minecraft:brown_mushroom",
    "minecraft:deadbush",       "minecraft:crimson_fungus",   "minecraft:warped_fungus",     "minecraft:crimson_roots",
    "minecraft:warped_roots",   "minecraft:bamboo",           "minecraft:open_eyeblossom",   "minecraft:closed_eyeblossom",
};

bool isHangingPot(Block const& b) { return blockName(b) == kHangingPotBlock; }

/// A block a hanging pot can hang from when clicked on any face (ropes and chains).
bool isHanger(Block const& b) { return blockName(b) == kRopeBlock || isChain(blockName(b)); }
} // namespace

int potPlant(Block const& block) {
    return game::intState(block, kGroup).value_or(0) * 16 + game::intState(block, kPlant).value_or(0);
}

Block const* withPotPlant(Block const& block, int index) {
    auto const* b = withState(block, kGroup, index / 16);
    return b ? withState(*b, kPlant, index % 16) : nullptr;
}

int plantIndex(std::string const& itemId) {
    for (std::size_t i = 0; i < kPlants.size(); ++i) {
        if (itemId == kPlants[i]) return static_cast<int>(i) + 1;
    }
    return 0;
}

std::string plantItem(int index) {
    if (index < 1 || index > static_cast<int>(kPlants.size())) return {};
    return kPlants[index - 1];
}

int plantCount() { return static_cast<int>(kPlants.size()); }

bool hangingPotUseOn(UseOn const& use) {
    if (!config().hangingFlowerPots) return false;
    auto& region   = use.player.getDimensionBlockSource();
    auto  playerId = use.player.getOrCreateUniqueID();
    int   dim      = static_cast<int>(region.getDimensionId());

    if (isHangingPot(use.block)) {
        int  flower = potPlant(use.block);
        int  plant  = plantIndex(use.itemId);
        auto pos    = use.pos;
        if (flower == 0 && plant > 0) {
            defer([=] {
                auto* player = findPlayer(playerId);
                auto* r      = regionOf(dim);
                if (!player || !r || plantIndex(itemId(player->getSelectedItem())) != plant) return;
                Block const& b = r->getBlock(pos);
                if (!isHangingPot(b) || potPlant(b) != 0) return;
                if (auto const* next = withPotPlant(b, plant)) {
                    game::setBlock(*r, pos, *next);
                    playBlockSound(*r, Sound::Place, pos, b);
                    consumeSelected(*player);
                }
            });
            return true;
        }
        if (flower > 0 && use.itemId.empty()) {
            defer([=] {
                auto* player = findPlayer(playerId);
                auto* r      = regionOf(dim);
                if (!player || !r) return;
                Block const& b = r->getBlock(pos);
                int          f = isHangingPot(b) ? potPlant(b) : 0;
                if (f == 0) return;
                if (auto const* next = withPotPlant(b, 0)) {
                    game::setBlock(*r, pos, *next);
                    giveOrDrop(*player, makeItem(plantItem(f)));
                    playBlockSound(*r, Sound::Break, pos, b);
                }
            });
            return true;
        }
        return flower > 0 && plant > 0; // a full pot keeps the plant in hand from being planted next to it
    }

    if (use.itemId != "minecraft:flower_pot") return false;
    bool hanger = isHanger(use.block);
    if (use.face != 0 && !hanger) return false;
    if (isInteractive(use.block) && !use.player.isSneaking()) return false;
    BlockPos target = offset(use.pos, 0);
    if (!game::replaceable(region, target)) return false;
    defer([=] {
        auto* player = findPlayer(playerId);
        auto* r      = regionOf(dim);
        if (!player || !r || itemId(player->getSelectedItem()) != "minecraft:flower_pot") return;
        if (!game::replaceable(*r, target)) return;
        auto const* block = game::blockWith(kHangingPotBlock);
        if (!block || !game::setBlock(*r, target, *block)) return;
        playBlockSound(*r, Sound::Place, target, *block);
        consumeSelected(*player);
    });
    return true;
}

} // namespace cm::feature::tweak
