// U1 jar: placed jars (storage, visuals, right click behaviour, drops).

#include "feature/jar/Jar.h"

#include <map>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::jar {

namespace {

std::map<core::GlobalPos, Contents> gCache; ///< loaded jar contents (empty jars are cached too)

std::string dbKey(core::GlobalPos const& p) { return "cm:jar:j:" + p.key(); }

Vec3 center(core::GlobalPos const& p) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f};
}

bool isCookie(ItemStack const& stack) {
    if (stack.isNull()) return false;
    auto id = stack.getTypeName();
    for (auto const& c : cfg().cookieItems) {
        if (c == id) return true;
    }
    return false;
}

int randomXp() {
    static std::mt19937 rng{std::random_device{}()};
    return 3 + static_cast<int>(rng() % 5) + static_cast<int>(rng() % 5); // bottle o' enchanting: 3..11
}

void useUp(Player& player, ItemStack item) {
    if (auto* it = item.mItem.get()) it->useTimeDepleted(item, &player.getLevel(), &player);
}

/// Drinks one bottle of `fluid` (Supplementaries `tryDrinkUpFluid`). Returns true when consumed.
bool drink(Player& player, FluidStack& fluid) {
    if (fluid.empty()) return false;
    auto const* def = fluidDef(fluid.fluid);
    if (!def || def->food == FoodKind::None) return false;
    switch (def->food) {
    case FoodKind::WholeItem:
        useUp(player, makeItem(fluid.fluid == "potion" ? fluid.bottle : def->foodItem, 1, fluid.data));
        break;
    case FoodKind::Stew:
    case FoodKind::SuspiciousStew: {
        if (!player.isHungry() && !player.isCreative()) return false;
        int nutrition = fluid.fluid == "rabbit_stew" ? 10 : 6;
        player.eat(nutrition / kBowl, 0.6f);
        break;
    }
    case FoodKind::Milk: {
        auto const& effects = player.getAllEffects();
        if (!effects.empty()) player.removeEffect(static_cast<int>(effects.front().mId));
        break;
    }
    case FoodKind::Experience:
        player.addExperience(randomXp());
        playSound(player.getDimensionBlockSource(), player.getPosition(), "random.orb");
        break;
    default:
        return false;
    }
    fluid.count -= 1;
    if (fluid.count <= 0) fluid = {};
    return true;
}

std::string_view fillSound(FluidStack const& f, std::string_view container) {
    if (container == "minecraft:bucket") {
        if (f.fluid == "lava") return "bucket.fill_lava";
        if (f.fluid == "powder_snow") return "bucket.fill_powder_snow";
        return "bucket.fill_water";
    }
    if (container == "minecraft:glass_bottle") return "bottle.fill";
    return "random.pop";
}

std::string_view emptySound(FluidStack const& f, std::string_view container) {
    if (container == "minecraft:bucket") {
        if (f.fluid == "lava") return "bucket.empty_lava";
        if (f.fluid == "powder_snow") return "bucket.empty_powder_snow";
        return "bucket.empty_water";
    }
    if (container == "minecraft:glass_bottle") return "bottle.empty";
    return "random.pop";
}

} // namespace

bool drinkFluid(Player& player, FluidStack& fluid) { return drink(player, fluid); }

// ---- storage -------------------------------------------------------------------------------------

Contents jarAt(core::GlobalPos const& pos) {
    if (auto it = gCache.find(pos); it != gCache.end()) return it->second;
    Contents c;
    if (auto j = game::db::load(dbKey(pos))) c = j->get<Contents>();
    gCache[pos] = c;
    return c;
}

void setJar(core::GlobalPos const& pos, Contents const& c) {
    gCache[pos] = c;
    if (c.empty()) {
        game::db::erase(dbKey(pos));
    } else {
        game::db::save(dbKey(pos), nlohmann::json(c));
    }
    if (auto* region = regionOf(pos.dim)) {
        auto bp = game::blockPos(pos);
        if (region->hasChunksAt(bp, 0, false) && isBlock(region->getBlock(bp), kJarId)) {
            auto v      = visualFor(c, capacity());
            auto states = contentStates(v.content);
            states.emplace_back(kStateLevel, v.level);
            setStates(*region, bp, states);
        }
    }
    syncDisplay(pos, c);
    // faucets fed by this jar (beside it) or pouring into it (above) show its fluid
    core::GlobalPos const near[] = {
        {pos.dim, pos.x,     pos.y + 1, pos.z    },
        {pos.dim, pos.x + 1, pos.y,     pos.z    },
        {pos.dim, pos.x - 1, pos.y,     pos.z    },
        {pos.dim, pos.x,     pos.y,     pos.z + 1},
        {pos.dim, pos.x,     pos.y,     pos.z - 1},
    };
    for (auto const& n : near) {
        if (faucetKnown(n)) faucetNeighborChanged(n);
    }
}

void forgetJar(core::GlobalPos const& pos) {
    gCache.erase(pos);
    game::db::erase(dbKey(pos));
    removeDisplay(pos);
}

void clearJarCache() { gCache.clear(); }

ItemStack jarItem(Contents const& c) {
    auto item = makeItem(kJarId, 1, 0);
    if (!c.empty()) setItemContents(item, c);
    return item;
}

// ---- right click -------------------------------------------------------------------------------

bool useJar(Player& player, core::GlobalPos const& pos) {
    auto& conf = cfg();
    if (!conf.enabled) return false;
    auto* region = regionOf(pos.dim);
    if (!region) return false;
    Contents  c    = jarAt(pos);
    ItemStack hand = player.getSelectedItem();
    auto      here = center(pos);

    // 1. fluids: fill the held container, or pour it in
    if (conf.jarLiquids && c.items.empty() && !c.mob) {
        std::string emptyId = hand.isNull() ? std::string{} : hand.getTypeName();
        FluidStack  tank    = c.fluid;
        if (hand.isNull() || hand.getAuxValue() == 0) {
            FluidStack before = tank;
            if (auto filled = fillItem(tank, emptyId)) {
                c.fluid = tank;
                setJar(pos, c);
                playSound(*region, here, fillSound(before, emptyId));
                swapHeld(player, makeItem(filled->id, 1, filled->aux));
                log().info("jar: {} filled {} from the jar at {}", player.getRealName(), filled->id, pos.key());
                return true;
            }
        }
        if (auto d = fluidFromItem(refOf(hand))) {
            Tank t{c.fluid, capacity()};
            if (t.add(d->fluid, true) == d->fluid.count) {
                t.add(d->fluid, false);
                c.fluid = t.fluid;
                setJar(pos, c);
                playSound(*region, here, emptySound(d->fluid, d->emptyId));
                swapHeld(player, d->emptyId.empty() ? ItemStack::EMPTY_ITEM() : makeItem(d->emptyId));
                log().info("jar: {} poured {} into the jar at {}", player.getRealName(), d->fluid.fluid, pos.key());
                return true;
            }
        }
    }

    // 2. cookies
    if (conf.jarCookies && c.fluid.empty() && !c.mob && isCookie(hand)
        && c.items.size() < static_cast<size_t>(kMaxCookies)) {
        ItemStack one = hand;
        one.set(1);
        c.items.push_back(storeItem(one));
        setJar(pos, c);
        playSound(*region, here, "random.pop", 1.f, 0.9f + static_cast<float>(randomXp() % 10) / 100.f);
        if (!player.isCreative()) swapHeld(player, ItemStack::EMPTY_ITEM());
        log().info("jar: {} put a {} in the jar at {}", player.getRealName(), one.getTypeName(), pos.key());
        return true;
    }

    // 3. fish buckets
    if (c.items.empty() && c.fluid.empty()) {
        if (!c.mob) {
            if (auto m = mobFromBucket(hand)) {
                c.mob = *m;
                setJar(pos, c);
                playSound(*region, here, "bucket.empty_fish");
                swapHeld(player, makeItem("minecraft:bucket"));
                log().info("jar: {} put a {} in the jar at {}", player.getRealName(), m->type, pos.key());
                return true;
            }
        } else if (isItem(hand, "minecraft:bucket") && !c.mob->bucket.empty()) {
            auto bucket = makeItem(c.mob->bucket);
            c.mob.reset();
            setJar(pos, c);
            playSound(*region, here, "bucket.fill_fish");
            swapHeld(player, bucket);
            return true;
        }
    }

    // 4. drink / eat
    if (!player.isSneaking() && conf.drinkFromJar) {
        if (drink(player, c.fluid)) {
            setJar(pos, c);
            return true;
        }
        if (!c.items.empty() && !player.isCreative()) {
            auto food = restoreItem(c.items.back());
            if (auto* it = food.mItem.get(); it && it->isFood() && player.isHungry()) {
                c.items.pop_back();
                setJar(pos, c);
                useUp(player, food);
                return true;
            }
        }
    }

    // 5. take a cookie out
    if (hand.isNull() && !c.items.empty()) {
        auto item = restoreItem(c.items.back());
        c.items.pop_back();
        setJar(pos, c);
        swapHeld(player, item);
        log().info("jar: {} took a {} from the jar at {}", player.getRealName(), item.getTypeName(), pos.key());
        return true;
    }
    return false;
}

bool jarWouldHandle(Player const& player, core::GlobalPos const& pos) {
    auto& conf = cfg();
    if (!conf.enabled) return false;
    Contents  c    = jarAt(pos);
    ItemStack hand = player.getSelectedItem();
    if (conf.jarLiquids && c.items.empty() && !c.mob) {
        FluidStack tank = c.fluid;
        if ((hand.isNull() || hand.getAuxValue() == 0) && fillItem(tank, hand.isNull() ? "" : hand.getTypeName()))
            return true;
        if (auto d = fluidFromItem(refOf(hand))) {
            Tank t{c.fluid, capacity()};
            if (t.add(d->fluid, true) == d->fluid.count) return true;
        }
    }
    if (conf.jarCookies && c.fluid.empty() && !c.mob && isCookie(hand)
        && c.items.size() < static_cast<size_t>(kMaxCookies)) {
        return true;
    }
    if (c.items.empty() && c.fluid.empty()) {
        if (!c.mob && mobFromBucket(hand)) return true;
        if (c.mob && isItem(hand, "minecraft:bucket") && !c.mob->bucket.empty()) return true;
    }
    if (!player.isSneaking() && conf.drinkFromJar) {
        if (auto const* def = fluidDef(c.fluid.fluid); def && def->food != FoodKind::None) return true;
        if (!c.items.empty() && !player.isCreative()) return true;
    }
    return hand.isNull() && !c.items.empty();
}

void dropJar(core::GlobalPos const& pos, Contents const& c) {
    auto* region = regionOf(pos.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return;
    level->getSpawner().spawnItem(*region, jarItem(c), nullptr, center(pos), 0);
}

} // namespace cm::feature::jar
