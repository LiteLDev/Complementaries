// Selftests of U10 `building` (test area dx 1000..1099).

#include <string>
#include <vector>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/util/Random.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffectIds.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/TickingQueueType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/FireBlock.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/building/Building.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "selftest/SelfTest.h"

namespace {

using cm::selftest::Context;
using namespace cm::feature::building;
using Facing = ScriptModuleMinecraft::ScriptFacing;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

BlockPos at(cm::core::GlobalPos const& g, int dx = 0, int dy = 0, int dz = 0) { return BlockPos{g.x + dx, g.y + dy, g.z + dz}; }
cm::core::GlobalPos gp(cm::core::GlobalPos g, int dx = 0, int dy = 0, int dz = 0) {
    g.x += dx;
    g.y += dy;
    g.z += dz;
    return g;
}

std::string idAt(Context& ctx, BlockPos const& p) { return cm::game::blockIdAt(regionOf(ctx), p); }
int         stateAt(Context& ctx, BlockPos const& p, std::string_view s) { return stateOf(regionOf(ctx).getBlock(p), s).value_or(-1); }

void command(std::string const& cmd) {
    auto level = ll::service::getLevel();
    if (!level) return;
    ServerCommandOrigin origin("complementaries-selftest", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(cmd, origin);
}

std::string posText(BlockPos const& p) { return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z); }

ItemStack item(std::string_view id, int count = 1) {
    ItemStack s;
    s.reinit(id, count, 0);
    return s;
}

SimulatedPlayer& simPlayer(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

/// Uses `stack` (put in slot 0) on `pos` from `face`, then waits for the deferred edit.
ll::coro::CoroTask<bool> useOn(Context& ctx, ItemStack const& stack, BlockPos const& pos, Facing face, Vec3 facePos) {
    auto& p = simPlayer(ctx);
    co_await ctx.ticks(10); // the game ignores item uses in quick succession
    ctx.give(p, stack, 0);
    p.setSelectedItem(stack);
    bool ok = p.simulateUseItemInSlotOnBlock(0, pos, face, facePos);
    co_await ctx.ticks(3);
    co_return ok;
}

/// Clears a column of air above and below a test position (the test area may hold leftovers).
void clearArea(Context& ctx, cm::core::GlobalPos const& base, int w, int h) {
    for (int dx = -1; dx <= w; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = 0; dy < h; ++dy) cm::game::clearBlock(regionOf(ctx), at(base, dx, dy, dz));
        }
    }
}

std::vector<char const*> const kAllBlocks{
    "ash",              "ash_bricks",          "ash_bricks_slab",        "ash_bricks_stairs",   "ash_bricks_wall",
    "stone_tile",       "stone_tile_slab",     "stone_tile_stairs",      "stone_tile_wall",     "blackstone_tile",
    "blackstone_tile_slab", "blackstone_tile_stairs", "blackstone_tile_wall", "lapis_bricks", "lapis_bricks_slab",
    "lapis_bricks_stairs", "lapis_bricks_wall", "checker_block",         "checker_slab",        "daub",
    "daub_frame",       "daub_brace",          "daub_cross_brace",       "timber_frame",        "timber_brace",
    "timber_cross_brace", "raked_gravel",      "fine_wood",              "fine_wood_stairs",    "fine_wood_slab",
    "soap_block",       "flint_block",         "feather_block",          "sugar_cube"
};

} // namespace

CM_REGISTER_SELFTEST("building.registered", [](Context& ctx) {
    for (auto const* path : kAllBlocks) {
        std::string id = std::string("complementaries:") + path;
        ctx.check(cm::game::blockWith(id) != nullptr, "block registered: " + id);
    }
    for (auto const* id : {"complementaries:ash", "complementaries:ash_brick", "complementaries:stone_tile_stairs"}) {
        ctx.check(!item(id).isNull(), std::string("item registered: ") + id);
    }
    ctx.check(cm::translate("cm.building.name", "?", "en_US") == "Building blocks", "lang fragment loaded");
    ctx.check(ashLayersFor(1.0, 0.99) == 1, "chance 1 gives one layer");
    ctx.check(ashLayersFor(1.5, 0.2) == 2 && ashLayersFor(1.5, 0.7) == 1, "chance 1.5 gives one or two layers");
    ctx.check(ashLayersFor(0.0, 0.0) == 0 && ashLayersFor(0.3, 0.5) == 0, "low chance gives none");
});

CM_REGISTER_SELFTEST("building.cardinal_states", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1000);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto p = at(base, 0, 0, 0);
    ctx.cleanupBlock(base);
    command("setblock " + posText(p) + " complementaries:stone_tile_stairs [\"minecraft:cardinal_direction\"=\"east\",\"minecraft:vertical_half\"=\"top\"]");
    co_await ctx.ticks(1);
    ctx.check(stairFacing(regionOf(ctx).getBlock(p)) == 3, "cardinal_direction east reads as Java facing 3");
    ctx.check(stairHalf(regionOf(ctx).getBlock(p)) == 1, "vertical_half top reads as 1");
    command("setblock " + posText(p) + " minecraft:oak_stairs [\"weirdo_direction\"=3]");
    co_await ctx.ticks(1);
    ctx.check(stairFacing(regionOf(ctx).getBlock(p)) == 2, "vanilla stairs weirdo_direction 3 is north");
    command("setblock " + posText(p) + " complementaries:ash [\"complementaries:layers\"=6]");
    co_await ctx.ticks(1);
    ctx.check(ashLayers(regionOf(ctx).getBlock(p)) == 6, "ash layers=6 reads back as 6 layers");
});

CM_REGISTER_SELFTEST("building.stairs_shape", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1005);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 3, 2);
    // A faces north, B in front (north) faces west: A is outer_left
    std::string const id = "complementaries:ash_bricks_stairs";
    ctx.require(ctx.setBlock(gp(base, 0, 0, 0), id, {{"minecraft:cardinal_direction", 2}}), "place A");
    ctx.require(ctx.setBlock(gp(base, 0, 0, -1), id, {{"minecraft:cardinal_direction", 1}}), "place B");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, at(base), "complementaries:shape") == 3, "outer_left next to a perpendicular stair in front, got " + std::to_string(stateAt(ctx, at(base), "complementaries:shape")));
    // C faces north, D behind (south) faces west: C is inner_left
    ctx.require(ctx.setBlock(gp(base, 2, 0, 0), id, {{"minecraft:cardinal_direction", 2}}), "place C");
    ctx.require(ctx.setBlock(gp(base, 2, 0, 1), id, {{"minecraft:cardinal_direction", 1}}), "place D");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, at(base, 2), "complementaries:shape") == 1, "inner_left with a perpendicular stair behind, got " + std::to_string(stateAt(ctx, at(base, 2), "complementaries:shape")));
    // removing D makes C straight again
    cm::game::clearBlock(regionOf(ctx), at(base, 2, 0, 1));
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, at(base, 2), "complementaries:shape") == 0, "straight again without neighbours");
    // a vanilla stair works as a neighbour too
    ctx.require(ctx.setBlock(gp(base, 2, 0, 1), "minecraft:stone_stairs", {{"weirdo_direction", 1}}), "place vanilla stairs facing west");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, at(base, 2), "complementaries:shape") == 1, "vanilla stairs make inner_left too");
});

CM_REGISTER_SELFTEST("building.wall_connect", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1010);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 3, 3);
    std::string const id = "complementaries:stone_tile_wall";
    ctx.require(ctx.setBlock(gp(base, 1), id), "place wall");
    ctx.require(ctx.setBlock(gp(base, 2), "minecraft:stone"), "stone east");
    co_await ctx.ticks(2);
    auto w = at(base, 1);
    ctx.check(stateAt(ctx, w, "complementaries:wall_east") == 1, "low connection to the stone");
    ctx.check(stateAt(ctx, w, "complementaries:wall_west") == 0, "no connection to air");
    ctx.check(stateAt(ctx, w, "complementaries:wall_post") == 1, "post on a wall end");
    ctx.require(ctx.setBlock(gp(base, 0), "minecraft:cobblestone_wall"), "vanilla wall west");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, w, "complementaries:wall_west") == 1, "connects to vanilla walls");
    ctx.check(stateAt(ctx, w, "complementaries:wall_post") == 0, "no post in a straight wall");
    ctx.require(ctx.setBlock(gp(base, 1, 1), "minecraft:stone"), "stone above");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, w, "complementaries:wall_east") == 2 && stateAt(ctx, w, "complementaries:wall_west") == 2, "tall under a solid block");
});

CM_REGISTER_SELFTEST("building.slab_merge", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1015);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 2, 3);
    std::string const id = "complementaries:stone_tile_slab";
    ctx.require(ctx.setBlock(base, id), "place a bottom slab");
    ctx.cleanupBlock(gp(base, 0, 1));
    co_await useOn(ctx, item(id, 4), at(base), Facing::Up, Vec3{0.5f, 0.5f, 0.5f});
    ctx.check(stateAt(ctx, at(base), "complementaries:double") == 1, "slab on the top face of a bottom slab makes a double slab");
    ctx.check(idAt(ctx, at(base, 0, 1)) != id, "no extra slab above");
    ctx.check(simPlayer(ctx).getSelectedItem().mCount == 3, "one slab used, have " + std::to_string(simPlayer(ctx).getSelectedItem().mCount));
});

CM_REGISTER_SELFTEST("building.ash", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1020);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 2, 5);
    auto& region = regionOf(ctx);
    // layering with the item
    ctx.require(ctx.setBlock(base, std::string(kAsh), {{"complementaries:layers", 1}}), "place one ash layer");
    ctx.check(ashLayers(region.getBlock(at(base))) == 1, "one layer");
    co_await useOn(ctx, item(kAsh, 2), at(base), Facing::Up, Vec3{0.5f, 0.1f, 0.5f});
    ctx.check(ashLayers(region.getBlock(at(base))) == 2, "ash on ash adds a layer");
    // falling ash merges into the ash below
    ctx.cleanupBlock(gp(base, 0, 3));
    ctx.require(addAsh(region, at(base, 0, 3), 3) == 3, "three layers placed in the air");
    co_await ctx.ticks(12);
    ctx.check(idAt(ctx, at(base, 0, 3)) == "minecraft:air", "the ash fell");
    ctx.check(ashLayers(region.getBlock(at(base))) == 5, "and merged: 5 layers, got " + std::to_string(ashLayers(region.getBlock(at(base)))));
    // overflow stays on top
    ctx.cleanupBlock(gp(base, 0, 1));
    addAsh(region, at(base, 0, 2), 6);
    co_await ctx.ticks(12);
    ctx.check(ashLayers(region.getBlock(at(base))) == 8, "full block of ash, got " + std::to_string(ashLayers(region.getBlock(at(base)))));
    ctx.check(ashLayers(region.getBlock(at(base, 0, 1))) == 3, "the rest piles on top, got " + std::to_string(ashLayers(region.getBlock(at(base, 0, 1)))));
});

CM_REGISTER_SELFTEST("building.rain", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1055);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 3, 2);
    auto& region = regionOf(ctx);
    ctx.cleanupBlock(base);
    ctx.require(addAsh(region, at(base), 2) == 2, "two layers of ash");
    co_await ctx.ticks(4);
    ctx.check(region.hasTickInPendingTicks(at(base), region.getBlock(at(base)).getBlockType(), TickingQueueType::Internal), "ash has a periodic tick queued");
    rainTick(region, at(base), false);
    ctx.check(ashLayers(region.getBlock(at(base))) == 2, "no wash without the roll");
    rainTick(region, at(base), true);
    ctx.check(ashLayers(region.getBlock(at(base))) == 1, "rain washes a layer away");
    rainTick(region, at(base), true);
    ctx.check(region.getBlock(at(base)).isAir(), "the last layer goes too");
    ctx.require(ctx.setBlock(gp(base, 2), std::string(kSugarCube)), "sugar cube");
    rainTick(region, at(base, 2), false);
    ctx.check(region.getBlock(at(base, 2)).isAir(), "rain dissolves sugar cubes");
});

CM_REGISTER_SELFTEST("building.fire_ash", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1025);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 2, 3);
    auto&       region = regionOf(ctx);
    auto const* fire   = cm::game::blockWith("minecraft:fire");
    ctx.require(fire != nullptr, "fire block");
    auto const& fireType = static_cast<FireBlock const&>(fire->getBlockType());
    bool        ash      = false;
    for (int i = 0; i < 20 && !ash; ++i) {
        ctx.require(ctx.setBlock(gp(base, 0, 1), "minecraft:oak_planks"), "place planks");
        Random random(static_cast<uint>(1234 + i), false);
        fireType.checkBurn(region, at(base, 0, 1), 1, random, 100, at(base, 0, 2));
        co_await ctx.ticks(1);
        ash = kindOf(region.getBlock(at(base, 0, 1))) == Kind::Ash || kindOf(region.getBlock(at(base))) == Kind::Ash;
    }
    ctx.cleanupBlock(base);
    ctx.check(ash, "a burned block leaves ash");
});

CM_REGISTER_SELFTEST("building.raked_gravel", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1030);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 3, 3);
    ctx.require(ctx.setBlock(base, "minecraft:gravel"), "gravel");
    co_await useOn(ctx, item("minecraft:iron_hoe"), at(base), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    ctx.check(idAt(ctx, at(base)) == std::string(kRakedGravel), "a hoe rakes gravel");
    ctx.check(simPlayer(ctx).getSelectedItem().getDamageValue() == 1, "the hoe lost durability");
    // a raked neighbour to the east makes both run east-west
    ctx.require(ctx.setBlock(gp(base, 1), std::string(kRakedGravel), {{"complementaries:shape", 0}}), "raked neighbour");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, at(base), "complementaries:shape") == 1, "east-west line");
    // a corner: add one to the north of the first
    ctx.require(ctx.setBlock(gp(base, 0, 0, -1), std::string(kRakedGravel)), "raked to the north");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, at(base), "complementaries:shape") == 2, "north-east corner, got " + std::to_string(stateAt(ctx, at(base), "complementaries:shape")));
    // a solid block on top turns it back into gravel
    ctx.require(ctx.setBlock(gp(base, 0, 1), "minecraft:stone"), "stone on top");
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, at(base)) == "minecraft:gravel", "back to gravel under a solid block");
});

CM_REGISTER_SELFTEST("building.timber", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1035);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 2, 3);
    ctx.require(ctx.setBlock(base, "complementaries:timber_brace", {{"complementaries:flipped", 1}}), "timber brace");
    co_await useOn(ctx, item(kDaub, 2), at(base), Facing::North, Vec3{0.5f, 0.5f, 0.0f});
    ctx.check(idAt(ctx, at(base)) == "complementaries:daub_brace", "daub fills the brace");
    ctx.check(stateAt(ctx, at(base), "complementaries:flipped") == 1, "the brace keeps its direction");
    co_await useOn(ctx, item("minecraft:iron_axe"), at(base), Facing::North, Vec3{0.5f, 0.5f, 0.0f});
    ctx.check(idAt(ctx, at(base)) == std::string(kDaub), "an axe takes the frame off");
    auto  box   = AABB{Vec3{(float)base.x - 2, (float)base.y - 1, (float)base.z - 3}, Vec3{(float)base.x + 3, (float)base.y + 3, (float)base.z + 3}};
    bool  drop  = false;
    for (auto* a : regionOf(ctx).getEntities(box, 1.0f, ActorType::ItemEntity)) {
        auto& ia = static_cast<ItemActor&>(*a);
        if (ia.item().getTypeName() == "complementaries:timber_brace") {
            drop = true;
            a->remove();
        }
    }
    auto& inv = simPlayer(ctx).getInventory();
    for (int i = 0; i < inv.getContainerSize() && !drop; ++i) drop = inv.getItem(i).getTypeName() == "complementaries:timber_brace";
    ctx.check(drop, "the timber brace drops (on the ground or picked up)");
    co_await useOn(ctx, item("complementaries:timber_frame"), at(base), Facing::North, Vec3{0.5f, 0.5f, 0.0f});
    ctx.check(idAt(ctx, at(base)) == "complementaries:daub_frame", "a timber frame used on daub frames it");
});

CM_REGISTER_SELFTEST("building.sugar_cube", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1040);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 3, 4);
    auto& region = regionOf(ctx);
    ctx.require(ctx.setBlock(base, std::string(kSugarCube)), "sugar cube");
    ctx.require(ctx.setBlock(gp(base, 1), "minecraft:water"), "water next to it");
    co_await ctx.ticks(5);
    ctx.check(idAt(ctx, at(base)) != std::string(kSugarCube), "the sugar cube dissolved");
    // it falls
    cm::game::clearBlock(region, at(base, 1));
    ctx.require(ctx.setBlock(gp(base, 0, 2), std::string(kSugarCube)), "sugar cube in the air");
    ctx.require(ctx.setBlock(gp(base, 1, 2), "minecraft:stone"), "neighbour change");
    co_await ctx.ticks(10);
    ctx.check(idAt(ctx, at(base)) == std::string(kSugarCube), "the sugar cube fell");
    ctx.cleanupBlock(base);
});

CM_REGISTER_SELFTEST("building.sugar_horse", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1045);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 2, 3);
    command("summon minecraft:horse " + posText(at(base, 1)));
    co_await ctx.ticks(2);
    Actor* horse = nullptr;
    auto   box   = AABB{Vec3{(float)base.x - 1, (float)base.y - 1, (float)base.z - 2}, Vec3{(float)base.x + 3, (float)base.y + 3, (float)base.z + 2}};
    for (auto* a : regionOf(ctx).getEntities(box, 1.0f, ActorType::Horse)) horse = a;
    ctx.require(horse != nullptr, "a horse spawned");
    auto& p = simPlayer(ctx);
    auto  s = item(kSugarCube, 2);
    ctx.give(p, s, 0);
    p.setSelectedItem(s);
    p.interact(*horse, horse->getPosition());
    co_await ctx.ticks(1);
    ctx.check(horse->getEffect(static_cast<unsigned>(MobEffectIds::MovementSpeed)) != nullptr, "the horse got Speed");
    ctx.check(p.getSelectedItem().mCount == 1, "one sugar cube used");
    horse->remove();
});

CM_REGISTER_SELFTEST("building.feather_fall", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1050);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearArea(ctx, base, 2, 14);
    ctx.require(ctx.setBlock(base, std::string(kFeather)), "feather block");
    auto& p = ctx.spawnPlayer();
    co_await ctx.ticks(2);
    int before = p.getHealth();
    p.teleport(Vec3{(float)base.x + 0.5f, (float)base.y + 12.f, (float)base.z + 0.5f}, DimensionType(0));
    for (int i = 0; i < 80; ++i) {
        co_await ctx.ticks(1);
        if (i > 5 && p.isOnGround()) break;
    }
    co_await ctx.ticks(2);
    ctx.check(p.getHealth() == before, "no fall damage on a feather block (" + std::to_string(before) + " -> " + std::to_string(p.getHealth()) + ")");
});
