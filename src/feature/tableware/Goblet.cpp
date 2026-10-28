// Goblet: holds one bottle of a fluid; fill with a bottle, empty with a glass bottle, drink with an
// empty hand. The fluid kind lives in `cm:fluid`; potions keep their exact item in the world DB.

#include <array>
#include <cmath>
#include <limits>

#include "mc/deps/core/math/Color.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/alchemy/Potion.h"
#include "mc/world/level/BlockSource.h"

#include "feature/tableware/Tableware.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::tableware::goblet {

namespace {
using Sound = SharedTypes::Legacy::LevelSoundEvent;

enum Fluid : int {
    Empty         = 0,
    Water         = 1,
    Honey         = 2,
    Milk          = 3,
    DragonBreath  = 4,
    Experience    = 5,
    FirstPotion   = 6, ///< 6..15: potion colour buckets
};

// Colour buckets of the potion states 6..15 (red, dark red, orange, yellow, green, light blue,
// blue, pink, grey, black).
constexpr std::array<std::array<int, 3>, 10> kBuckets{{
    {248, 36, 35},
    {107, 18, 18},
    {228, 154, 58},
    {242, 210, 58},
    {78, 147, 49},
    {124, 175, 198},
    {46, 82, 153},
    {205, 92, 171},
    {127, 131, 146},
    {53, 42, 39},
}};

constexpr char const* kNames[] = {"empty", "water", "honey", "milk", "dragon_breath", "experience"};

bool isPotionItem(std::string const& id) {
    return id == "minecraft:potion" || id == "minecraft:splash_potion" || id == "minecraft:lingering_potion";
}

std::shared_ptr<Potion const> potionById(int aux) {
    if (aux < 0 || aux >= 64) return nullptr;
    return Potion::mPotionsById()[aux];
}

int potionBucket(int aux) {
    auto potion = potionById(aux);
    if (!potion || potion->mEffects->empty()) return FirstPotion + 6; // plain potions are blue
    auto  c    = MobEffectInstance::getAverageColorValue(*potion->mEffects);
    int   best = 0;
    float dist = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(kBuckets.size()); ++i) {
        float dr = c.r * 255.0f - kBuckets[i][0], dg = c.g * 255.0f - kBuckets[i][1], db = c.b * 255.0f - kBuckets[i][2];
        float d = dr * dr + dg * dg + db * db;
        if (d < dist) {
            dist = d;
            best = i;
        }
    }
    return FirstPotion + best;
}

std::string fluidName(int fluid) { return fluid < FirstPotion ? kNames[fluid] : "potion"; }

std::string dbKey(BlockSource const& region, BlockPos const& pos) {
    return posKey("goblet", static_cast<int>(region.getDimensionId()), pos);
}

/// The bottle item that holds the goblet's fluid.
std::optional<ItemStack> bottleOf(BlockSource const& region, BlockPos const& pos, int fluid) {
    switch (fluid) {
    case Water:
    default:
        break;
    case Honey:
        return makeItem("minecraft:honey_bottle");
    case DragonBreath:
        return makeItem("minecraft:dragon_breath");
    case Experience:
        return makeItem("minecraft:experience_bottle");
    case Milk:
        return std::nullopt;
    }
    if (fluid == Empty) return std::nullopt;
    std::string id  = "minecraft:potion";
    int         aux = 0;
    if (auto data = game::db::load(dbKey(region, pos))) {
        id  = data->value("item", id);
        aux = data->value("aux", 0);
    } else if (fluid != Water) {
        return std::nullopt; // potion data lost: nothing to give back
    }
    return makeItem(id, 1, aux);
}
} // namespace

int fluidOf(ItemStack const& item) {
    auto id = itemId(item);
    if (id.empty()) return Empty;
    if (isPotionItem(id)) return item.getAuxValue() == 0 ? Water : potionBucket(item.getAuxValue());
    if (id == "minecraft:honey_bottle") return Honey;
    if (id == "minecraft:dragon_breath") return DragonBreath;
    if (id == "minecraft:experience_bottle") return Experience;
    return Empty;
}

bool onUse(UseContext const& ctx, bool apply) {
    if (!is(ctx.block, kGoblet)) return false;
    int  fluid = state(ctx.block, "cm:fluid");
    auto held  = itemId(ctx.held);

    // fill
    if (fluid == Empty) {
        int incoming = fluidOf(ctx.held);
        if (incoming == Empty) return false;
        if (!apply) return true;
        auto const* next = with(ctx.block, {{"cm:fluid", incoming}});
        if (!next) return false;
        if (isPotionItem(held)) {
            game::db::save(dbKey(ctx.region, ctx.pos), {{"item", held}, {"aux", ctx.held.getAuxValue()}});
        }
        place(ctx.region, ctx.pos, *next);
        exchangeHeld(ctx.player, makeItem("minecraft:glass_bottle"));
        playSound(ctx.player, static_cast<int>(Sound::BottleEmpty), ctx.pos);
        log().info("tableware: goblet filled with {}", fluidName(incoming));
        return true;
    }

    // empty into a glass bottle
    if (held == "minecraft:glass_bottle") {
        auto bottle = bottleOf(ctx.region, ctx.pos, fluid);
        if (!bottle) return false;
        if (!apply) return true;
        auto const* next = with(ctx.block, {{"cm:fluid", 0}});
        if (!next) return false;
        place(ctx.region, ctx.pos, *next);
        game::db::erase(dbKey(ctx.region, ctx.pos));
        exchangeHeld(ctx.player, *bottle);
        playSound(ctx.player, static_cast<int>(Sound::BottleFill), ctx.pos);
        log().info("tableware: goblet emptied");
        return true;
    }

    // drink
    if (!isEmpty(ctx.held)) return false;
    if (fluid != Water && fluid != Honey && fluid != Milk && fluid < FirstPotion) return false;
    if (!apply) return true;
    auto& player = ctx.player;
    if (fluid == Water) {
        player.stopFire();
    } else if (fluid == Honey) {
        player.eat(6, 0.1f);
        player.removeEffect(19); // poison
    } else if (fluid == Milk) {
        player.removeAllEffects();
    }
    if (fluid >= FirstPotion || fluid == Water) {
        if (auto data = game::db::load(dbKey(ctx.region, ctx.pos))) {
            if (auto potion = potionById(data->value("aux", 0))) {
                for (auto const& effect : *potion->mEffects) player.addEffect(effect);
            }
        }
    }
    auto const* next = with(ctx.block, {{"cm:fluid", 0}});
    if (next) place(ctx.region, ctx.pos, *next);
    game::db::erase(dbKey(ctx.region, ctx.pos));
    playSound(player, static_cast<int>(Sound::Drink), ctx.pos);
    log().info("tableware: drank {} from a goblet", fluidName(fluid));
    return true;
}

void onRemoved(int dim, BlockPos const& pos) { game::db::erase(posKey("goblet", dim, pos)); }

} // namespace cm::feature::tableware::goblet
