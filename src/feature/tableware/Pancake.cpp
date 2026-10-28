// Pancake: stack up to 8, add a honey topping, eat the top one.

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"

#include "feature/tableware/Tableware.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::tableware::pancake {

namespace {
using Sound = SharedTypes::Legacy::LevelSoundEvent;

constexpr int   kMaxLayers  = 8;
constexpr int   kNutrition  = 3;
constexpr float kSaturation = 0.6f;
} // namespace

bool onUse(UseContext const& ctx, bool apply) {
    if (!is(ctx.block, kPancake)) return false;
    int  layers  = state(ctx.block, "cm:pancakes") + 1;
    int  topping = state(ctx.block, "cm:topping");
    auto held    = itemId(ctx.held);

    if (held == kPancake) {
        if (layers >= kMaxLayers) return false; // the item places a new pancake on top
        if (!apply) return true;
        auto const* next = with(ctx.block, {{"cm:pancakes", layers}});
        if (!next) return false;
        place(ctx.region, ctx.pos, *next);
        consumeHeld(ctx.player);
        playSound(ctx.player, static_cast<int>(Sound::Place), ctx.pos, static_cast<int>(next->mNetworkId));
        log().info("tableware: stacked a pancake ({})", layers + 1);
        return true;
    }
    if (held == "minecraft:honey_bottle") {
        if (topping != 0) return false;
        if (!apply) return true;
        auto const* next = with(ctx.block, {{"cm:topping", 1}});
        if (!next) return false;
        place(ctx.region, ctx.pos, *next);
        exchangeHeld(ctx.player, makeItem("minecraft:glass_bottle"));
        playSound(ctx.player, static_cast<int>(Sound::BottleEmpty), ctx.pos);
        log().info("tableware: added honey to a pancake");
        return true;
    }
    if (!creative(ctx.player) && !ctx.player.isHungry()) return false;
    if (!apply) return true;
    ctx.player.eat(kNutrition + (topping != 0 ? 1 : 0), kSaturation);
    if (layers <= 1) {
        game::clearBlock(ctx.region, ctx.pos);
    } else if (auto const* next = with(ctx.block, {{"cm:pancakes", layers - 2}})) {
        place(ctx.region, ctx.pos, *next);
    }
    playSound(ctx.player, static_cast<int>(Sound::Eat), ctx.pos);
    log().info("tableware: ate a pancake");
    return true;
}

} // namespace cm::feature::tableware::pancake
