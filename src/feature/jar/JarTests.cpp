// U1 jar selftests (dx 100..199).

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"
#include "mc/common/Brightness.h"
#include "mc/world/phys/AABB.h"

#include "feature/jar/Jar.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace {

using cm::selftest::Context;
using namespace cm::feature::jar;
namespace game = cm::game;
namespace core = cm::core;

core::GlobalPos up(core::GlobalPos p, int dy = 1) {
    p.y += dy;
    return p;
}

Block const& blockAt(core::GlobalPos const& p) { return regionOf(p.dim)->getBlock(game::blockPos(p)); }

/// Places a jar and remembers to remove it without a drop.
bool placeJar(Context& ctx, core::GlobalPos const& p, Contents const& c = {}) {
    if (!ctx.setBlock(p, std::string(kJarId))) return false;
    setJar(p, c);
    suppressDrop(p);
    return true;
}

void hold(Player& player, ItemStack const& item) {
    player.setSelectedItem(item);
    player.refreshInventory();
}

int countItems(core::GlobalPos const& p, std::string_view id) {
    auto* region = regionOf(p.dim);
    AABB  box{
        Vec3{static_cast<float>(p.x) - 1.5f, static_cast<float>(p.y) - 1.5f, static_cast<float>(p.z) - 1.5f},
        Vec3{static_cast<float>(p.x) + 2.5f, static_cast<float>(p.y) + 2.5f, static_cast<float>(p.z) + 2.5f}
    };
    int n = 0;
    for (auto* a : region->getEntities(box, 0.f)) {
        if (!a || a->getTypeName() != "minecraft:item") continue;
        ++n;
        (void)id;
    }
    return n;
}

void killItems(core::GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    AABB  box{
        Vec3{static_cast<float>(p.x) - 1.5f, static_cast<float>(p.y) - 1.5f, static_cast<float>(p.z) - 1.5f},
        Vec3{static_cast<float>(p.x) + 2.5f, static_cast<float>(p.y) + 2.5f, static_cast<float>(p.z) + 2.5f}
    };
    for (auto* a : region->getEntities(box, 0.f)) {
        if (a && (a->getTypeName() == "minecraft:item" || a->getTypeName() == "minecraft:xp_orb")) a->remove();
    }
}

} // namespace

CM_REGISTER_SELFTEST("jar.fluid_rules", [](Context& ctx) {
    Tank t{{}, 12};
    auto water = fluidFromItem({"minecraft:water_bucket", 0});
    ctx.require(water.has_value(), "a water bucket holds water");
    ctx.check(water->fluid.count == kBucket && water->emptyId == "minecraft:bucket", "bucket = 4 bottles, leaves a bucket");
    ctx.check(t.add(water->fluid, false) == 4, "the bucket fits");
    auto bottle = fillItem(t.fluid, "minecraft:glass_bottle");
    ctx.check(bottle && bottle->id == "minecraft:potion" && bottle->aux == 0, "a glass bottle fills to a water bottle");
    ctx.check(t.fluid.count == 3, "the bottle took one");
    ctx.check(!fillItem(t.fluid, "minecraft:bucket"), "3 bottles do not fill a bucket");
    auto swift = fluidFromItem({"minecraft:potion", 14});
    ctx.check(swift && swift->fluid.fluid == "potion" && swift->fluid.data == 14, "potions keep their type");
    ctx.check(t.add(swift->fluid, true) == 0, "potions do not mix with water");
    Tank p{{}, 12};
    p.add(swift->fluid, false);
    auto slow = fluidFromItem({"minecraft:potion", 17});
    ctx.check(p.add(slow->fluid, true) == 0, "different potions do not mix");
    auto splash = fluidFromItem({"minecraft:splash_potion", 14});
    ctx.check(p.add(splash->fluid, true) == 0, "splash and drinkable potions do not mix");
    auto back = fillItem(p.fluid, "minecraft:glass_bottle");
    ctx.check(back && back->id == "minecraft:potion" && back->aux == 14, "the potion comes back with its type");
    ctx.check(p.empty(), "the tank is empty again");
    Tank stew{{}, 12};
    stew.add(fluidFromItem({"minecraft:mushroom_stew", 0})->fluid, false);
    ctx.check(stew.fluid.count == kBowl, "a bowl is 2 bottles");
    auto tear = fluidFromItem({"minecraft:ghast_tear", 0});
    ctx.check(tear && tear->emptyId.empty(), "a ghast tear leaves nothing in the hand");
    Tank tears{{}, 12};
    tears.add(tear->fluid, false);
    auto got = fillItem(tears.fluid, "");
    ctx.check(got && got->id == "minecraft:ghast_tear", "an empty hand takes a ghast tear back");
    Tank full{{"water", 12}, 12};
    ctx.check(full.comparatorOutput() == 15 && full.pixelLevel() == 12, "full jar: comparator 15, 12 px");
    Tank one{{"water", 1}, 12};
    ctx.check(one.comparatorOutput() == 2 && one.pixelLevel() == 1, "one bottle: comparator 2, 1 px");
    ctx.check(contentFor({"potion", 1, 14, "minecraft:potion"}) == kContentFirstPotion + potionColorGroup(14), "potion colour");
    Contents c;
    c.fluid = {"honey", 3};
    auto j  = nlohmann::json(c);
    ctx.check(j.get<Contents>() == c, "contents round-trip through JSON");
});

CM_REGISTER_SELFTEST("jar.water_and_bottles", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(100);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(placeJar(ctx, pos), "jar placed (behavior pack loaded)");
    auto& player = ctx.player();
    hold(player, makeItem("minecraft:water_bucket"));
    ctx.require(useJar(player, pos), "a water bucket empties into the jar");
    ctx.check(jarAt(pos).fluid == FluidStack{"water", 4}, "the jar holds 4 bottles of water");
    ctx.check(isItem(player.getSelectedItem(), "minecraft:bucket"), "the player gets the empty bucket");
    ctx.check(stateOf(blockAt(pos), kStateLevel) == 4 && contentOf(blockAt(pos)) == 1, "states: 4 px of water");
    hold(player, makeItem("minecraft:glass_bottle"));
    ctx.require(useJar(player, pos), "a glass bottle fills");
    ctx.check(isItem(player.getSelectedItem(), "minecraft:potion") && player.getSelectedItem().getAuxValue() == 0, "water bottle");
    ctx.check(jarAt(pos).fluid.count == 3, "3 bottles left");
    hold(player, makeItem("minecraft:bucket"));
    ctx.check(!useJar(player, pos), "3 bottles cannot fill a bucket");
    hold(player, makeItem("minecraft:lava_bucket"));
    ctx.check(!jarWouldHandle(player, pos), "lava does not go into water");
    auto json = game::db::load("cm:jar:j:" + pos.key());
    ctx.check(json && json->contains("fluid"), "contents saved in the world DB");
    hold(player, ItemStack::EMPTY_ITEM());
    co_return;
});

CM_REGISTER_SELFTEST("jar.lava_light", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(102);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(placeJar(ctx, pos, Contents{{"lava", 8}}), "jar placed");
    co_await ctx.ticks(1);
    ctx.check(contentOf(blockAt(pos)) == 2 && stateOf(blockAt(pos), kStateLevel) == 8, "lava, 8 px");
    auto const& lit = blockAt(pos);
    ctx.check(lit.mDirectData->mLightEmission->mValue >= 14, "lava jars glow");
    co_return;
});

CM_REGISTER_SELFTEST("jar.cookies", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(104);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(placeJar(ctx, pos), "jar placed");
    auto& player = ctx.player();
    auto  cookies = makeItem("minecraft:cookie", 5);
    hold(player, cookies);
    ctx.require(useJar(player, pos), "a cookie goes in");
    ctx.require(useJar(player, pos), "a second cookie goes in");
    ctx.check(jarAt(pos).items.size() == 2, "2 cookies stored");
    ctx.check(player.getSelectedItem().mCount == 3 || player.isCreative(), "2 cookies used");
    ctx.check(contentOf(blockAt(pos)) == kContentCookie && stateOf(blockAt(pos), kStateLevel) == 2, "states: 2 cookies");
    hold(player, makeItem("minecraft:water_bucket"));
    ctx.check(!jarWouldHandle(player, pos), "no water in a cookie jar");
    hold(player, ItemStack::EMPTY_ITEM());
    ctx.require(useJar(player, pos), "an empty hand takes a cookie");
    ctx.check(isItem(player.getSelectedItem(), "minecraft:cookie"), "the cookie is in the hand");
    ctx.check(jarAt(pos).items.size() == 1, "1 cookie left");
    hold(player, ItemStack::EMPTY_ITEM());
    co_return;
});

CM_REGISTER_SELFTEST("jar.fish_bucket", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(106);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(placeJar(ctx, pos), "jar placed");
    auto& player = ctx.player();
    hold(player, makeItem("minecraft:cod_bucket"));
    ctx.require(useJar(player, pos), "a cod bucket goes in");
    auto c = jarAt(pos);
    ctx.check(c.mob && c.mob->type == "minecraft:cod" && c.mob->bucket == "minecraft:cod_bucket", "a cod is stored");
    ctx.check(isItem(player.getSelectedItem(), "minecraft:bucket"), "an empty bucket is given back");
    ctx.check(contentOf(blockAt(pos)) == 1 && stateOf(blockAt(pos), kStateLevel) == 12, "the fish swims in water");
    co_await ctx.ticks(2);
    ctx.check(hasDisplay(pos), "the fish is drawn by the helper entity");
    ctx.require(useJar(player, pos), "an empty bucket takes the fish back");
    ctx.check(isItem(player.getSelectedItem(), "minecraft:cod_bucket"), "cod bucket returned");
    ctx.check(!jarAt(pos).mob, "the jar is empty");
    co_await ctx.ticks(2);
    ctx.check(!hasDisplay(pos), "the helper entity is gone");
    hold(player, ItemStack::EMPTY_ITEM());
    co_return;
});

CM_REGISTER_SELFTEST("jar.item_contents", [](Context& ctx) {
    auto     jar = makeItem(kJarId);
    Contents c;
    c.fluid = {"potion", 3, 14, "minecraft:potion"};
    setItemContents(jar, c);
    auto back = itemContents(jar);
    ctx.check(back && *back == c, "contents survive on the item");
    ctx.check(!jar.getCustomLore().empty(), "the item has a tooltip");
    auto lore = tooltipFor(c, nullptr);
    ctx.check(!lore.empty() && lore.front().find("3") != std::string::npos, "tooltip shows the amount");
    setItemContents(jar, {});
    ctx.check(!itemContents(jar), "cleared contents");
    ctx.check(tooltipFor({}, nullptr).front().find("cookies") != std::string::npos, "empty jars show the hint");
});

CM_REGISTER_SELFTEST("jar.drops_contents", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(108);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(kJarId)), "jar placed");
    setJar(pos, Contents{{"honey", 5}});
    killItems(pos);
    game::clearBlock(*regionOf(pos.dim), game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(countItems(pos, kJarId) >= 1, "breaking the jar drops it");
    ctx.check(!game::db::load("cm:jar:j:" + pos.key()), "the DB entry is gone");
    auto* region = regionOf(pos.dim);
    AABB  box{
        Vec3{static_cast<float>(pos.x) - 1.5f, static_cast<float>(pos.y) - 1.5f, static_cast<float>(pos.z) - 1.5f},
        Vec3{static_cast<float>(pos.x) + 2.5f, static_cast<float>(pos.y) + 2.5f, static_cast<float>(pos.z) + 2.5f}
    };
    bool honey = false;
    for (auto* a : region->getEntities(box, 0.f)) {
        if (!a || a->getTypeName() != "minecraft:item") continue;
        CompoundTag tag;
        if (a->save(tag) && tag.contains("Item")) {
            auto item = ItemStack::fromTag(tag["Item"].get<CompoundTag>());
            if (auto c = itemContents(item); c && c->fluid == FluidStack{"honey", 5}) honey = true;
        }
    }
    ctx.check(honey, "the dropped jar keeps its honey");
    killItems(pos);
    co_return;
});

CM_REGISTER_SELFTEST("jar.capture_release", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(110);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& player = ctx.player();
    auto* region = regionOf(pos.dim);
    auto  level  = ll::service::getLevel();
    Vec3  at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + 0.5f};
    auto* bee = level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:bee"), nullptr, at, false, false, false);
    ctx.require(bee != nullptr, "a bee spawned");
    auto* zombie = level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:cow"), nullptr, at, false, false, false);
    hold(player, makeItem(kJarId));
    if (zombie) {
        ctx.check(canCatch(*zombie, &player) == CaptureFail::NotCatchable, "a cow does not fit in a jar");
        zombie->remove();
    }
    ctx.check(canCatch(*bee, &player) == CaptureFail::None, "bees are catchable");
    ctx.require(useJarItemOnMob(player, *bee), "the jar catches the bee");
    auto c = itemContents(player.getSelectedItem());
    ctx.require(c && c->mob && c->mob->type == "minecraft:bee", "the jar item holds the bee");
    ctx.check(!c->mob->nbt.empty() && c->mob->display == 1, "the bee keeps its data and is drawn");
    co_await ctx.ticks(1);
    auto  held     = player.getSelectedItem();
    auto  released = releaseMob(*region, *c->mob, at, 0.f);
    ctx.require(released != nullptr, "the bee is released");
    ctx.check(released->getTypeName() == "minecraft:bee", "a bee comes out");
    released->remove();
    // a placed jar with the bee spawns its display entity
    ctx.require(placeJar(ctx, pos, *c), "jar with a bee placed");
    co_await ctx.ticks(2);
    ctx.check(hasDisplay(pos), "the bee is drawn in the jar");
    ctx.check(contentOf(blockAt(pos)) == 0, "no fluid shown");
    setJar(pos, {});
    co_await ctx.ticks(1);
    ctx.check(!hasDisplay(pos), "emptying the jar removes the display");
    hold(player, ItemStack::EMPTY_ITEM());
    (void)held;
    co_return;
});

CM_REGISTER_SELFTEST("jar.faucet_water_to_cauldron", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto below  = ctx.testPos(150);
    auto faucet = up(below);
    auto behind = faucet;
    behind.z += 1; // facing north (2): the source is south of it
    ctx.require(co_await ctx.ensureLoaded(below), "test area loaded");
    ctx.require(ctx.setBlock(below, "minecraft:cauldron"), "cauldron");
    ctx.require(ctx.setBlock(behind, "minecraft:water"), "water source");
    ctx.require(ctx.setBlock(faucet, std::string(kFaucetId), {{std::string(kStateFacing), 2}}), "faucet placed");
    faucetPlaced(faucet);
    ctx.check(faucetKnown(faucet), "faucet registered");
    co_await ctx.ticks(2);
    ctx.check(contentOf(blockAt(faucet)) == 1, "the faucet sees water behind it");
    int cd = runFaucetOnce(faucet);
    ctx.check(cd == 20, "one bottle moved, 20 ticks cooldown");
    ctx.check(vanillaInt(blockAt(below), "fill_level", -1) == 2, "the cauldron has 1/3 water");
    ctx.check(game::blockIdAt(*regionOf(behind.dim), game::blockPos(behind)) == "minecraft:water", "water sources are infinite");
    toggleFaucet(faucet);
    co_await ctx.ticks(1);
    ctx.check(stateOf(blockAt(faucet), kStateEnabled) == 1, "right click opens it");
    co_await ctx.ticks(45);
    ctx.check(vanillaInt(blockAt(below), "fill_level", -1) >= 4, "an open faucet keeps filling");
    toggleFaucet(faucet);
    faucetRemoved(faucet);
    co_return;
});

CM_REGISTER_SELFTEST("jar.faucet_jar_to_jar", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto below  = ctx.testPos(154);
    auto faucet = up(below);
    auto behind = faucet;
    behind.x -= 1; // facing east (3): the source is west of it
    ctx.require(co_await ctx.ensureLoaded(below), "test area loaded");
    ctx.require(placeJar(ctx, below), "jar below");
    ctx.require(placeJar(ctx, behind, Contents{{"honey", 3}}), "honey jar behind");
    ctx.require(ctx.setBlock(faucet, std::string(kFaucetId), {{std::string(kStateFacing), 3}}), "faucet placed");
    faucetPlaced(faucet);
    co_await ctx.ticks(1);
    ctx.check(stateOf(blockAt(faucet), kStateConnected) == 1, "connected to the jar below");
    ctx.check(contentOf(blockAt(faucet)) == 3, "shows honey");
    ctx.check(runFaucetOnce(faucet) == 20, "one bottle moved");
    ctx.check(jarAt(below).fluid == FluidStack{"honey", 1}, "1 honey below");
    ctx.check(jarAt(behind).fluid == FluidStack{"honey", 2}, "2 honey behind");
    setJar(below, Contents{{"water", 2}});
    ctx.check(runFaucetOnce(faucet) == 0, "honey does not mix with water");
    faucetRemoved(faucet);
    co_return;
});

CM_REGISTER_SELFTEST("jar.faucet_redstone", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base   = ctx.testPos(158);
    auto faucet = up(base);
    auto power  = faucet;
    power.x += 1;
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    ctx.require(ctx.setBlock(faucet, std::string(kFaucetId), {{std::string(kStateFacing), 2}}), "faucet placed");
    faucetPlaced(faucet);
    ctx.require(ctx.setBlock(power, "minecraft:redstone_block"), "redstone block beside it");
    co_await ctx.ticks(12);
    ctx.check(stateOf(blockAt(faucet), kStatePowered) == 1, "redstone powers the faucet");
    game::clearBlock(*regionOf(power.dim), game::blockPos(power));
    co_await ctx.ticks(12);
    ctx.check(stateOf(blockAt(faucet), kStatePowered) == 0, "unpowered again");
    faucetRemoved(faucet);
    co_return;
});

CM_REGISTER_SELFTEST("jar.faucet_xp", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto below  = ctx.testPos(162);
    auto faucet = up(below);
    auto behind = faucet;
    behind.z += 1;
    ctx.require(co_await ctx.ensureLoaded(below), "test area loaded");
    ctx.require(placeJar(ctx, behind, Contents{{"experience", 2}}), "xp jar behind");
    ctx.require(ctx.setBlock(faucet, std::string(kFaucetId), {{std::string(kStateFacing), 2}}), "faucet placed");
    faucetPlaced(faucet);
    co_await ctx.ticks(1);
    ctx.check(runFaucetOnce(faucet) == 20, "one bottle of xp poured out");
    ctx.check(jarAt(behind).fluid.count == 1, "1 bottle left");
    co_await ctx.ticks(2);
    auto* region = regionOf(below.dim);
    AABB  box{
        Vec3{static_cast<float>(below.x) - 1.f, static_cast<float>(below.y) - 1.f, static_cast<float>(below.z) - 1.f},
        Vec3{static_cast<float>(below.x) + 2.f, static_cast<float>(below.y) + 2.f, static_cast<float>(below.z) + 2.f}
    };
    int orbs = 0;
    for (auto* a : region->getEntities(box, 0.f)) {
        if (a && a->getTypeName() == "minecraft:xp_orb") ++orbs;
    }
    ctx.check(orbs > 0, "xp orbs dropped below");
    killItems(below);
    faucetRemoved(faucet);
    co_return;
});
