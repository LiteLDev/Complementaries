// Selftests of the mechanism unit (U7, test dx 700..799).

#include <algorithm>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/mechanism/Crankable.h"
#include "feature/mechanism/Mechanism.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
using namespace cm::feature::mechanism;
namespace game = cm::game;
using V = rotation::Value;

BlockPos east(BlockPos const& p, int n = 1) { return offset(p, rotation::kEast, n); }
BlockPos up(BlockPos const& p, int n = 1) { return offset(p, rotation::kUp, n); }

bool put(Context& ctx, BlockSource& region, BlockPos const& p, std::string_view id, StateList const& states = {}) {
    auto const* b = blockFrom(id, states);
    if (!b) return false;
    ctx.cleanupBlock(game::globalPos(region, p));
    return place(region, p, *b);
}

std::string idAt(BlockSource& region, BlockPos const& p) { return region.getBlock(p).getTypeName(); }

BlockSource& regionFor(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

float feetY(Player& p) { return p.getAABB().min.y; }

void standOn(Player& p, BlockPos const& block) {
    p.teleport(
        Vec3{static_cast<float>(block.x) + 0.5f, static_cast<float>(block.y) + 1.f, static_cast<float>(block.z) + 0.5f},
        DimensionType(0)
    );
}
} // namespace

CM_REGISTER_SELFTEST("mechanism.blocks", [](Context& ctx) {
    for (auto id : {ids::kLauncher, ids::kHead, ids::kArm, ids::kBellows, ids::kTurnTable, ids::kCog, ids::kCrank})
        ctx.check(game::blockWith(id) != nullptr, std::string(id) + " is registered");
    for (Kind k : {Kind::Launcher, Kind::Bellows, Kind::TurnTable, Kind::Head, Kind::Arm, Kind::Crank}) {
        std::string_view id = k == Kind::Launcher ? ids::kLauncher
                            : k == Kind::Bellows  ? ids::kBellows
                            : k == Kind::TurnTable ? ids::kTurnTable
                            : k == Kind::Head      ? ids::kHead
                            : k == Kind::Arm       ? ids::kArm
                                                   : ids::kCrank;
        for (int d = 0; d < 6; ++d) {
            auto const* b = blockFrom(id, facingStates(k, d));
            ctx.check(b && facingOf(*b) == d, std::string(id) + " facing " + std::to_string(d) + " round-trips");
        }
    }
    ctx.check(config().bellows.period >= 10 && config().turnTable.period >= 1, "config is loaded and clamped");
});

CM_REGISTER_SELFTEST("mechanism.launcher_extends", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(700);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    ctx.require(put(ctx, region, p, ids::kLauncher, facingStates(Kind::Launcher, rotation::kUp)), "launcher placed");
    ctx.cleanupBlock(game::globalPos(region, up(p)));
    co_await ctx.ticks(2);
    ctx.check(isTracked(game::globalPos(region, p)), "the launcher is tracked");
    ctx.require(put(ctx, region, east(p), "minecraft:redstone_block"), "power placed");
    co_await ctx.ticks(8);
    ctx.check(boolStateOf(region.getBlock(p), "complementaries:extended"), "powered launcher extends");
    ctx.check(idAt(region, up(p)) == ids::kHead, "the head is in front (" + idAt(region, up(p)) + ")");
    placeAir(region, east(p));
    co_await ctx.ticks(8);
    ctx.check(!boolStateOf(region.getBlock(p), "complementaries:extended"), "unpowered launcher retracts");
    ctx.check(region.getBlock(up(p)).isAir(), "the head is gone");
});

CM_REGISTER_SELFTEST("mechanism.launcher_blocked", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(703);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    ctx.require(put(ctx, region, p, ids::kLauncher, facingStates(Kind::Launcher, rotation::kEast)), "launcher placed");
    ctx.require(put(ctx, region, east(p), "minecraft:stone"), "obstacle placed");
    ctx.require(put(ctx, region, up(p), "minecraft:redstone_block"), "power placed");
    co_await ctx.ticks(6);
    ctx.check(!boolStateOf(region.getBlock(p), "complementaries:extended"), "a blocked launcher stays retracted");
    ctx.check(idAt(region, east(p)) == "minecraft:stone", "the obstacle is untouched");
});

CM_REGISTER_SELFTEST("mechanism.launcher_launches", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(706);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    ctx.require(put(ctx, region, p, ids::kLauncher, facingStates(Kind::Launcher, rotation::kUp)), "launcher placed");
    ctx.cleanupBlock(game::globalPos(region, up(p)));
    auto& player = ctx.player();
    standOn(player, p);
    co_await ctx.ticks(10);
    float start = feetY(player);
    ctx.require(put(ctx, region, east(p), "minecraft:redstone_block"), "power placed");
    float top = start;
    for (int i = 0; i < 20; ++i) {
        co_await ctx.ticks(1);
        top = std::max(top, feetY(player));
    }
    ctx.info("feet from " + std::to_string(start) + " up to " + std::to_string(top));
    ctx.check(top - start > 3.f, "the player was launched (rose " + std::to_string(top - start) + " blocks)");
});

CM_REGISTER_SELFTEST("mechanism.launcher_trampoline", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(709);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    ctx.require(put(ctx, region, p, ids::kLauncher, facingStates(Kind::Launcher, rotation::kUp)), "launcher placed");
    ctx.cleanupBlock(game::globalPos(region, up(p)));
    auto& player = ctx.player();
    co_await ctx.ticks(2);
    int health = player.getHealth();
    player.teleport(
        Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 14.f, static_cast<float>(p.z) + 0.5f},
        DimensionType(0)
    );
    bool fired = false;
    for (int i = 0; i < 80 && !fired; ++i) {
        co_await ctx.ticks(1);
        fired = boolStateOf(region.getBlock(p), "complementaries:extended");
    }
    ctx.check(fired, "a long fall onto the launcher fires it");
    co_await ctx.ticks(5);
    ctx.check(player.getHealth() >= health, "no fall damage (" + std::to_string(player.getHealth()) + " < " + std::to_string(health) + ")");
    ctx.check(feetY(player) > static_cast<float>(p.y) + 2.f, "the player bounced up");
    co_await ctx.ticks(10);
    ctx.check(!boolStateOf(region.getBlock(p), "complementaries:extended"), "it retracts again (unpowered)");
});

CM_REGISTER_SELFTEST("mechanism.cog_chain", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(712);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    for (int i = 1; i <= 3; ++i) ctx.require(put(ctx, region, east(p, i), ids::kCog), "cog placed");
    ctx.require(put(ctx, region, east(p, 4), "minecraft:redstone_lamp"), "lamp placed");
    ctx.require(put(ctx, region, p, "minecraft:redstone_block"), "power placed");
    co_await ctx.ticks(8);
    for (int i = 1; i <= 3; ++i) {
        int pw = intStateOf(region.getBlock(east(p, i)), "complementaries:power", -1);
        ctx.check(pw == 16 - i, "cog " + std::to_string(i) + " has power " + std::to_string(16 - i) + " (got " + std::to_string(pw) + ")");
    }
    ctx.check(boolStateOf(region.getBlock(east(p, 1)), "complementaries:twisting"), "a powered cog twists");
    ctx.check(idAt(region, east(p, 4)) == "minecraft:lit_redstone_lamp", "the cog chain lights a lamp (" + idAt(region, east(p, 4)) + ")");
    placeAir(region, p);
    co_await ctx.ticks(12);
    for (int i = 1; i <= 3; ++i)
        ctx.check(intStateOf(region.getBlock(east(p, i)), "complementaries:power", -1) == 0, "cog " + std::to_string(i) + " turns off");
    ctx.check(idAt(region, east(p, 4)) == "minecraft:redstone_lamp", "the lamp turns off (" + idAt(region, east(p, 4)) + ")");
});

CM_REGISTER_SELFTEST("mechanism.cog_repeater", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(718);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp); // redstone block | repeater | cog, another cog beside the repeater
    BlockPos rep    = east(p);
    BlockPos side   = offset(rep, rotation::kSouth);
    ctx.require(put(ctx, region, east(p, 2), ids::kCog), "cog placed");
    ctx.require(put(ctx, region, side, ids::kCog), "side cog placed");
    ctx.require(put(ctx, region, p, "minecraft:redstone_block"), "power placed");
    int works = 0;
    for (std::string dir : {"east", "west"}) {
        ctx.require(
            put(ctx, region, rep, "minecraft:unpowered_repeater", {{"minecraft:cardinal_direction", V{dir}}}),
            "repeater placed"
        );
        co_await ctx.ticks(12);
        auto [rs, rd] = componentInfo(region, rep);
        ctx.info("repeater " + dir + " component: strength " + std::to_string(rs) + ", direction " + std::to_string(rd) + ", block " + idAt(region, rep));
        int front = intStateOf(region.getBlock(east(p, 2)), "complementaries:power", -1);
        int beside = intStateOf(region.getBlock(side), "complementaries:power", -1);
        ctx.info("repeater " + dir + ": cog " + std::to_string(front) + ", side cog " + std::to_string(beside));
        ctx.check(beside == 0, "a repeater does not power sideways");
        if (front == 15) ++works;
        else ctx.check(front == 0, "a reversed repeater does not power the cog");
        placeAir(region, rep);
        co_await ctx.ticks(6);
    }
    ctx.check(works == 1, "exactly one repeater direction powers the cog");
});

CM_REGISTER_SELFTEST("mechanism.crank_turns", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(722);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp); // stone with a crank on top, lamps beside the crank and the stone
    ctx.require(put(ctx, region, p, "minecraft:stone"), "support placed");
    ctx.require(put(ctx, region, up(p), ids::kCrank, facingStates(Kind::Crank, rotation::kUp)), "crank placed");
    ctx.require(put(ctx, region, east(up(p)), "minecraft:redstone_lamp"), "lamp placed");
    ctx.require(put(ctx, region, offset(p, rotation::kWest), "minecraft:redstone_lamp"), "lamp placed");
    co_await ctx.ticks(2);
    ctx.check(crank::turn(region, up(p), false, nullptr) == 1, "turn -> 1");
    ctx.check(crank::turn(region, up(p), false, nullptr) == 2, "turn -> 2");
    ctx.check(crank::turn(region, up(p), true, nullptr) == 1, "sneak turn -> 1");
    co_await ctx.ticks(6);
    ctx.check(idAt(region, east(up(p))) == "minecraft:lit_redstone_lamp", "the crank powers its neighbours");
    ctx.check(idAt(region, offset(p, rotation::kWest)) == "minecraft:lit_redstone_lamp", "the crank strongly powers its support");
    ctx.check(crank::turn(region, up(p), true, nullptr) == 0, "sneak turn -> 0");
    ctx.check(crank::turn(region, up(p), true, nullptr) == 15, "wraps 0 -> 15");
    ctx.check(crank::turn(region, up(p), false, nullptr) == 0, "wraps 15 -> 0");
    // a crankable block behind the crank is turned too
    int  calls = 0;
    bool lastCcw = false;
    registerCrankable("minecraft:stone", [&](BlockSource&, BlockPos const&, bool ccw, Player*) {
        ++calls;
        lastCcw = ccw;
    });
    crank::turn(region, up(p), true, nullptr);
    unregisterCrankable("minecraft:stone");
    ctx.check(calls == 1 && lastCcw, "the crankable block behind is turned");
    co_await ctx.ticks(10);
    ctx.check(idAt(region, east(up(p))) == "minecraft:lit_redstone_lamp", "power 15 keeps the lamp lit");
});

CM_REGISTER_SELFTEST("mechanism.crank_pops_off", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(726);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = up(game::blockPos(gp));
    ctx.require(put(ctx, region, p, "minecraft:stone"), "support placed");
    ctx.require(put(ctx, region, east(p), ids::kCrank, facingStates(Kind::Crank, rotation::kEast)), "crank placed");
    co_await ctx.ticks(3);
    ctx.check(idAt(region, east(p)) == ids::kCrank, "a supported crank stays");
    placeAir(region, p);
    co_await ctx.ticks(3);
    ctx.check(region.getBlock(east(p)).isAir(), "the crank pops off without support");
});

CM_REGISTER_SELFTEST("mechanism.turn_table_rotates", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(730);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    ctx.require(put(ctx, region, p, ids::kTurnTable, facingStates(Kind::TurnTable, rotation::kUp)), "turn table placed");
    ctx.require(
        put(ctx, region, up(p), "minecraft:furnace", {{"minecraft:cardinal_direction", V{std::string("north")}}}),
        "furnace placed"
    );
    co_await ctx.ticks(1);
    ctx.check(turntable::rotateFront(region, p) == "minecraft:furnace", "the furnace is rotated");
    auto dir = stateOf(region.getBlock(up(p)), "minecraft:cardinal_direction");
    ctx.check(dir && std::get<std::string>(*dir) == "east", "clockwise: north -> east");
    ctx.check(turntable::toggle(region, p), "toggle -> counter-clockwise");
    turntable::rotateFront(region, p);
    turntable::rotateFront(region, p);
    dir = stateOf(region.getBlock(up(p)), "minecraft:cardinal_direction");
    ctx.check(dir && std::get<std::string>(*dir) == "west", "counter-clockwise twice: east -> west");
    ctx.check(!turntable::toggle(region, p), "toggle -> clockwise");

    // containers keep their contents
    ctx.require(
        put(ctx, region, up(p), "minecraft:chest", {{"minecraft:cardinal_direction", V{std::string("south")}}}),
        "chest placed"
    );
    co_await ctx.ticks(1);
    auto container = region.tryGetContainer(up(p));
    ctx.require(container.has_value(), "chest container");
    ItemStack item;
    item.reinit("minecraft:diamond", 5, 0);
    container->setItem(0, item);
    ctx.check(turntable::rotateFront(region, p) == "minecraft:chest", "the chest is rotated");
    co_await ctx.ticks(1);
    auto after = region.tryGetContainer(up(p));
    ctx.check(after.has_value() && after->getItem(0).mCount == 5, "the chest keeps its items");
    if (after) after->removeAllItems();

});

CM_REGISTER_SELFTEST("mechanism.turn_table_powered", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(734);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    ctx.require(put(ctx, region, p, ids::kTurnTable, facingStates(Kind::TurnTable, rotation::kUp)), "turn table placed");
    ctx.require(put(ctx, region, up(p), "minecraft:oak_log", {{"pillar_axis", V{std::string("x")}}}), "log placed");
    co_await ctx.ticks(2);
    ctx.check(!boolStateOf(region.getBlock(p), "complementaries:rotating"), "idle while unpowered");
    ctx.require(put(ctx, region, east(p), "minecraft:redstone_block"), "power placed");
    bool turned = false;
    for (int i = 0; i < 10 && !turned; ++i) {
        co_await ctx.ticks(1);
        auto a = stateOf(region.getBlock(up(p)), "pillar_axis");
        turned = a && std::get<std::string>(*a) == "z";
    }
    ctx.check(boolStateOf(region.getBlock(p), "complementaries:rotating"), "rotating while powered");
    ctx.check(turned, "the powered table turns the log (x -> z)");
    placeAir(region, east(p));
    co_await ctx.ticks(4);
    ctx.check(!boolStateOf(region.getBlock(p), "complementaries:rotating"), "stops when unpowered");
});

CM_REGISTER_SELFTEST("mechanism.bellows_blows", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(740);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    ctx.require(put(ctx, region, p, ids::kBellows, facingStates(Kind::Bellows, rotation::kEast)), "bellows placed");
    auto& player = ctx.player();
    standOn(player, offset(east(p, 2), rotation::kDown));
    co_await ctx.ticks(10);
    float x0 = player.getPosition().x;
    ctx.require(put(ctx, region, offset(p, rotation::kNorth), "minecraft:redstone_block"), "power placed");
    int maxStage = 0;
    for (int i = 0; i < 6; ++i) {
        co_await ctx.ticks(1);
        maxStage = std::max(maxStage, intStateOf(region.getBlock(p), "complementaries:stage", 0));
    }
    float x1 = player.getPosition().x;
    ctx.info("player x " + std::to_string(x0) + " -> " + std::to_string(x1) + ", stage " + std::to_string(maxStage));
    ctx.check(maxStage > 0, "the powered bellows compresses");
    ctx.check(x1 - x0 > 0.3f, "the bellows blows the player away");
    placeAir(region, offset(p, rotation::kNorth));
    co_await ctx.ticks(30);
    ctx.check(intStateOf(region.getBlock(p), "complementaries:stage", -1) == 0, "it opens again once unpowered");
});

CM_REGISTER_SELFTEST("mechanism.bellows_press", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(745);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    auto&    region = regionFor(ctx);
    BlockPos p      = game::blockPos(gp);
    ctx.require(put(ctx, region, p, ids::kBellows, facingStates(Kind::Bellows, rotation::kNorth)), "bellows placed");
    co_await ctx.ticks(2);
    bellows::press(region, p);
    int maxStage = 0;
    for (int i = 0; i < 8; ++i) {
        co_await ctx.ticks(1);
        maxStage = std::max(maxStage, intStateOf(region.getBlock(p), "complementaries:stage", 0));
    }
    ctx.check(maxStage >= 5, "a manual press runs one cycle (stage " + std::to_string(maxStage) + ")");
    co_await ctx.ticks(10);
    ctx.check(intStateOf(region.getBlock(p), "complementaries:stage", -1) == 0, "and stops");
});
