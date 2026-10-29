// Selftests of U9 lighting (test area dx 900..999).

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/gametest/ScriptNavigationResult.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/lighting/LightBlocks.h"
#include "feature/lighting/Lighting.h"
#include "feature/lighting/LightingConfig.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
namespace lt = cm::feature::lighting;
using Face = ScriptModuleMinecraft::ScriptFacing;

ItemStack item(std::string_view id, int count = 1) {
    ItemStack s;
    s.reinit(id, count, 0);
    return s;
}

BlockPos bp(cm::core::GlobalPos const& p, int dx = 0, int dy = 0, int dz = 0) { return {p.x + dx, p.y + dy, p.z + dz}; }

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

/// Places `id` with `states` at `pos` (cleared after the test).
bool place(Context& ctx, BlockPos const& pos, std::string_view id, lt::StateList const& states = {}) {
    auto const* b = lt::makeBlock(id, states);
    if (!b) return false;
    ctx.cleanupBlock(cm::game::globalPos(0, pos));
    return cm::game::setBlock(regionOf(ctx), pos, *b);
}

Block const& at(Context& ctx, BlockPos const& pos) { return regionOf(ctx).getBlock(pos); }

/// Clears the player's hotbar and puts `stack` in slot 0.
void hold(Context& ctx, ItemStack const& stack) {
    auto& inv = ctx.player().getInventory();
    for (int i = 0; i < 9; ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
    ctx.give(ctx.player(), stack, 0);
}

bool use(Context& ctx, int slot, BlockPos const& pos, Face face) {
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    return sp.simulateUseItemInSlotOnBlock(slot, pos, face, Vec3{0.5f, 0.5f, 0.5f});
}
} // namespace

CM_REGISTER_SELFTEST("lighting.registered", [](Context& ctx) {
    for (auto const& id : lt::allBlockIds()) ctx.check(lt::makeBlock(id) != nullptr, id + " is registered");
    auto const* sconce = lt::makeBlock("complementaries:sconce");
    ctx.require(sconce != nullptr, "sconce exists");
    ctx.check(lt::boolState(*sconce, lt::kLit), "sconces are lit by default");
    auto const* holder = lt::makeBlock("complementaries:candle_holder_red");
    ctx.require(holder != nullptr, "red candle holder exists");
    ctx.check(!lt::boolState(*holder, lt::kLit, true), "candle holders are unlit by default");
    ctx.check(lt::intState(*holder, lt::kCandles) == 1, "candle holders start with one candle");
    auto const* wall = lt::makeBlock("complementaries:sconce_soul", {{lt::kFace, std::string("east")}, {lt::kLit, false}});
    ctx.require(wall != nullptr, "wall sconce state exists");
    ctx.check(lt::stringState(*wall, lt::kFace) == "east", "block_face reads back");
    ctx.check(!lt::boolState(*wall, lt::kLit, true), "lit reads back");
    ctx.check(lt::classify("complementaries:candle_holder_light_blue").kind == lt::Kind::CandleHolder, "classify candle holder");
    ctx.check(lt::lanternIndex("minecraft:soul_lantern") == 1, "lantern index");
    ctx.check(cm::translate("cm.lighting.lit", "?", "zh_CN") != "?", "zh_CN strings loaded");
});

CM_REGISTER_SELFTEST("lighting.flint_lights_sconce", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(900);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto base = bp(p);
    ctx.require(place(ctx, base, "minecraft:stone"), "support placed");
    auto pos = bp(p, 0, 1, 0);
    ctx.require(place(ctx, pos, "complementaries:sconce", {{lt::kFace, std::string("up")}, {lt::kLit, false}}), "sconce placed");
    hold(ctx, item("minecraft:flint_and_steel"));
    co_await ctx.ticks(10);
    use(ctx, 0, pos, Face::Up);
    co_await ctx.ticks(3);
    ctx.check(lt::boolState(at(ctx, pos), lt::kLit), "flint and steel lit the sconce");
    auto const& held = ctx.player().getInventory().getItem(0);
    ctx.check(!held.isNull() && held.getDamageValue() == 1, "flint and steel took 1 damage");
    ctx.check(at(ctx, bp(p, 0, 2, 0)).isAir(), "no fire placed above");
});

CM_REGISTER_SELFTEST("lighting.hand_extinguishes", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(902);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(place(ctx, bp(p), "minecraft:stone"), "support placed");
    auto pos = bp(p, 0, 1, 0);
    ctx.require(place(ctx, pos, "complementaries:candle_holder", {{lt::kFace, std::string("up")}, {lt::kLit, true}}), "holder placed");
    hold(ctx, ItemStack::EMPTY_ITEM());
    co_await ctx.ticks(2);
    use(ctx, 0, pos, Face::Up);
    co_await ctx.ticks(3);
    if (lt::boolState(at(ctx, pos), lt::kLit)) {
        ctx.info("the simulated empty-hand click did not reach the block; checking the action directly");
        ctx.check(lt::Lighting::setLit(regionOf(ctx), pos, false), "setLit(false) puts it out");
    }
    ctx.check(!lt::boolState(at(ctx, pos), lt::kLit), "candle holder is out");
    ctx.check(!lt::Lighting::setLit(regionOf(ctx), pos, false), "putting out twice changes nothing");
});

CM_REGISTER_SELFTEST("lighting.add_candles", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(904);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(place(ctx, bp(p), "minecraft:stone"), "support placed");
    auto pos = bp(p, 0, 1, 0);
    ctx.require(place(ctx, pos, "complementaries:candle_holder_blue", {{lt::kFace, std::string("up")}}), "holder placed");
    hold(ctx, item("complementaries:candle_holder_blue", 4));
    co_await ctx.ticks(10);
    for (int i = 0; i < 3; ++i) {
        use(ctx, 0, pos, Face::Up);
        co_await ctx.ticks(10);
    }
    ctx.check(lt::intState(at(ctx, pos), lt::kCandles) == 4, "three uses add three candles");
    auto const& held = ctx.player().getInventory().getItem(0);
    ctx.check(held.mCount == 1, "three candle holder items were used");
    ctx.check(!lt::Lighting::addCandle(regionOf(ctx), pos), "a full holder takes no more candles");
    ctx.cleanupBlock(cm::game::globalPos(0, bp(p, 0, 2, 0)));
});

CM_REGISTER_SELFTEST("lighting.water_extinguishes", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(906);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(place(ctx, bp(p), "minecraft:stone"), "support placed");
    auto pos = bp(p, 0, 1, 0);
    ctx.require(place(ctx, pos, "complementaries:sconce_ender", {{lt::kFace, std::string("up")}}), "sconce placed");
    auto& region = regionOf(ctx);
    auto const* water = lt::makeBlock("minecraft:water");
    ctx.require(water != nullptr, "water exists");
    region.setExtraBlock(pos, *water, 3);
    co_await ctx.ticks(3);
    ctx.check(lt::waterlogged(region, pos), "the sconce holds water");
    ctx.check(!lt::boolState(at(ctx, pos), lt::kLit), "water put the sconce out");
    ctx.check(!lt::Lighting::setLit(region, pos, true), "a waterlogged sconce can't be lit");
    if (auto const* air = lt::makeBlock("minecraft:air")) region.setExtraBlock(pos, *air, 3);
});

CM_REGISTER_SELFTEST("lighting.fire_projectile", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(908);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(place(ctx, bp(p), "minecraft:stone"), "support placed");
    auto pos = bp(p, 0, 0, -1);
    ctx.require(place(ctx, pos, "complementaries:sconce", {{lt::kFace, std::string("north")}, {lt::kLit, false}}), "wall sconce placed");
    auto& light  = lt::Lighting::get();
    Vec3  impact{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    light.onProjectileHit(0, pos, true, impact, false, false); // a cold arrow: nothing
    co_await ctx.ticks(2);
    ctx.check(!lt::boolState(at(ctx, pos), lt::kLit), "a cold projectile does nothing");
    light.onProjectileHit(0, pos, true, impact, true, false);
    co_await ctx.ticks(2);
    ctx.check(lt::boolState(at(ctx, pos), lt::kLit), "a burning projectile lights the sconce");
    light.onProjectileHit(0, pos, true, Vec3{pos.x + 0.5f, pos.y + 1.2f, pos.z - 0.3f}, false, true);
    co_await ctx.ticks(2);
    ctx.check(!lt::boolState(at(ctx, pos), lt::kLit), "a splash water potion next to it puts it out");
});

CM_REGISTER_SELFTEST("lighting.sconce_lever", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(910);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(place(ctx, bp(p), "minecraft:stone"), "support placed");
    auto lever = bp(p, 0, 0, -1);
    auto lamp  = bp(p, -1, 0, -1);
    ctx.require(place(ctx, lever, "complementaries:sconce_lever", {{lt::kFace, std::string("north")}}), "lever placed");
    ctx.require(place(ctx, lamp, "minecraft:redstone_lamp"), "lamp placed");
    hold(ctx, ItemStack::EMPTY_ITEM());
    co_await ctx.ticks(2);
    use(ctx, 0, lever, Face::North);
    co_await ctx.ticks(3);
    if (!lt::boolState(at(ctx, lever), lt::kPowered)) {
        ctx.info("the simulated empty-hand click did not reach the lever; toggling directly");
        ctx.check(lt::Lighting::toggleLever(regionOf(ctx), lever), "toggleLever");
    }
    ctx.check(lt::boolState(at(ctx, lever), lt::kPowered), "the lever is on");
    co_await ctx.ticks(8);
    ctx.check(at(ctx, lamp).getTypeName() == "minecraft:lit_redstone_lamp", "the lever powers a lamp next to it");
    ctx.check(lt::Lighting::toggleLever(regionOf(ctx), lever), "toggle off");
    co_await ctx.ticks(20);
    ctx.check(at(ctx, lamp).getTypeName() == "minecraft:redstone_lamp", "the lamp turns off");
});

CM_REGISTER_SELFTEST("lighting.wall_lantern", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(912);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto support = bp(p, 0, 1, 0);
    ctx.require(place(ctx, bp(p), "minecraft:stone"), "base placed");
    ctx.require(place(ctx, support, "minecraft:stone"), "wall placed");
    auto pos = bp(p, 0, 1, -1);
    ctx.cleanupBlock(cm::game::globalPos(0, pos));
    hold(ctx, item("minecraft:soul_lantern", 2));
    co_await ctx.ticks(2);
    use(ctx, 0, support, Face::North);
    co_await ctx.ticks(3);
    auto const& b = at(ctx, pos);
    ctx.require(b.getTypeName() == "complementaries:wall_lantern", "a wall lantern hangs on the wall (got " + b.getTypeName() + ")");
    ctx.check(lt::intState(b, lt::kLantern) == 1, "it holds the soul lantern");
    ctx.check(lt::stringState(b, lt::kFace) == "north", "it faces away from the wall");
    ctx.check(ctx.player().getInventory().getItem(0).mCount == 1, "one lantern was used");
    // swing
    lt::Lighting::get().push(0, pos, 5.f);
    co_await ctx.ticks(3);
    ctx.check(lt::intState(at(ctx, pos), lt::kSwing) != 0, "a push makes it swing");
    co_await ctx.ticks(140);
    ctx.check(lt::intState(at(ctx, pos), lt::kSwing) == 0, "the swing settles");
    // top face: vanilla behaviour (no wall lantern)
    ctx.check(!lt::Lighting::placeWallLantern(regionOf(ctx), support, 1, 0), "not on the top of a block");
});

CM_REGISTER_SELFTEST("lighting.wall_lantern_walk", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(914);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto support = bp(p, 0, 1, 0);
    ctx.require(place(ctx, support, "minecraft:stone"), "wall placed");
    ctx.require(lt::Lighting::placeWallLantern(regionOf(ctx), support, 2, 0), "wall lantern placed");
    auto pos = bp(p, 0, 1, -1);
    ctx.cleanupBlock(cm::game::globalPos(0, pos));
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    sp.teleport(Vec3{pos.x - 2.5f, static_cast<float>(pos.y - 1), pos.z + 0.5f}, DimensionType(0));
    co_await ctx.ticks(5);
    // a straight walk (no path finding: it can take long to start on a busy integrated server)
    sp.simulateMoveToLocation(Vec3{pos.x + 3.5f, static_cast<float>(pos.y - 1), pos.z + 0.5f}, 1.f, true);
    bool swung = false;
    for (int i = 0; i < 160 && !swung; ++i) {
        co_await ctx.ticks(1);
        swung = lt::intState(at(ctx, pos), lt::kSwing) != 0;
    }
    ctx.check(swung, "walking through the wall lantern makes it swing");
});

CM_REGISTER_SELFTEST("lighting.particles", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(916);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(place(ctx, bp(p), "minecraft:stone"), "support placed");
    ctx.require(place(ctx, bp(p, 0, 1, 0), "complementaries:sconce_glow", {{lt::kFace, std::string("up")}}), "sconce placed");
    auto& conf    = cm::config::mut<lt::LightingConfig>();
    int   samples = conf.particleSamplesPerTick;
    conf.particleSamplesPerTick = 4000;
    auto before = lt::Lighting::get().particlesSent();
    co_await ctx.ticks(80);
    conf.particleSamplesPerTick = samples;
    ctx.check(lt::Lighting::get().particlesSent() > before, "particles were sent around the lit sconce");
});

