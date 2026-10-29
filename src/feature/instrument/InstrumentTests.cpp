// Selftests of U6 `instrument` (test positions dx 600..699).

#include <string>

#include "ll/api/service/Bedrock.h"

#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"
#include "mc/deps/core/math/Vec3.h"

#include "feature/instrument/Instrument.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
namespace ins = cm::feature::instrument;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

Block const& blockAt(Context& ctx, cm::core::GlobalPos const& p) { return regionOf(ctx).getBlock(cm::game::blockPos(p)); }

cm::core::GlobalPos above(cm::core::GlobalPos p, int dy = 1) {
    p.y += dy;
    return p;
}

/// Places `id` at `pos` (with a stone floor below) and waits until the unit tracks it.
ll::coro::CoroTask<ins::Record*> place(Context& ctx, cm::core::GlobalPos pos, std::string const& id) {
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(above(pos, -1), "minecraft:stone"), "floor placed");
    ctx.require(ctx.setBlock(pos, id), id + " placed (behavior pack loaded)");
    co_await ctx.ticks(3);
    auto* rec = ins::find(pos);
    ctx.require(rec != nullptr, id + " is tracked after placement");
    co_return rec;
}

int comparatorRead(Context& ctx, cm::core::GlobalPos const& p) {
    auto const& b = blockAt(ctx, p);
    return b.getBlockType().getComparatorSignal(regionOf(ctx), cm::game::blockPos(p), b, 0);
}
} // namespace

CM_REGISTER_SELFTEST("instrument.blocks_registered", [](Context& ctx) {
    for (auto id : {ins::kGlobe, ins::kGlobeSepia, ins::kClock, ins::kHourglass, ins::kWindVane}) {
        ctx.check(cm::game::blockWith(id) != nullptr, std::string(id) + " is registered");
    }
    ctx.check(ins::comparatorsInstalled(), "comparator output is installed on the instrument blocks");
});

CM_REGISTER_SELFTEST("instrument.globe_spin", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(600);
    auto* rec = co_await place(ctx, pos, std::string(ins::kGlobe));
    ctx.check(comparatorRead(ctx, pos) == 4, "resting globe outputs rotation 0 -> 4");
    auto& region = regionOf(ctx);
    ins::globe::spin(*rec, region, cm::game::blockPos(pos));
    ctx.check(rec->rotation == 1, "a spin turns the resting face");
    co_await ctx.ticks(2);
    rec = ins::find(pos);
    ctx.require(rec != nullptr, "still tracked");
    ctx.check(rec->yaw > 0, "spinning");
    ctx.check(comparatorRead(ctx, pos) == 15, "comparator reads 15 while spinning");
    int angle = ins::state(blockAt(ctx, pos), "complementaries:angle", -1);
    co_await ctx.ticks(1);
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:angle", -1) != angle, "the sphere turns every tick");
    co_await ctx.ticks(80);
    rec = ins::find(pos);
    ctx.require(rec != nullptr, "still tracked after the spin");
    ctx.check(rec->yaw == 0, "the spin decays to rest");
    ctx.check(comparatorRead(ctx, pos) == 3, "resting globe outputs rotation 1 -> 3");
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:angle", -1) == 8, "rests at 180 degrees");
});

CM_REGISTER_SELFTEST("instrument.globe_interact", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(603);
    auto* rec = co_await place(ctx, pos, std::string(ins::kGlobe));
    auto& sp  = static_cast<SimulatedPlayer&>(ctx.player());
    sp.teleport(Vec3{pos.x + 0.5f, static_cast<float>(pos.y), pos.z - 1.5f}, DimensionType(0));
    co_await ctx.ticks(2);
    ItemStack stick;
    stick.reinit("minecraft:stick", 1, 0);
    sp.simulateSetItem(stick, true, 0);
    sp.simulateUseItemInSlotOnBlock(0, cm::game::blockPos(pos), ScriptModuleMinecraft::ScriptFacing::North, Vec3{0.5f, 0.5f, 0.f});
    co_await ctx.ticks(3);
    rec = ins::find(pos);
    ctx.require(rec != nullptr, "still tracked");
    ctx.check(rec->yaw > 0, "a right click (holding a stick) spins the globe");
    co_await ctx.ticks(20);
    ItemStack shears;
    shears.reinit("minecraft:shears", 1, 0);
    sp.simulateSetItem(shears, true, 0);
    sp.simulateUseItemInSlotOnBlock(0, cm::game::blockPos(pos), ScriptModuleMinecraft::ScriptFacing::North, Vec3{0.5f, 0.5f, 0.f});
    co_await ctx.ticks(3);
    rec = ins::find(pos);
    ctx.check(rec && rec->sheared, "shears shear the globe");
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:skin", -1) == 14, "sheared model shown");
});

CM_REGISTER_SELFTEST("instrument.hourglass_interact", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(630);
    auto* rec = co_await place(ctx, pos, std::string(ins::kHourglass));
    auto& sp  = static_cast<SimulatedPlayer&>(ctx.player());
    sp.teleport(Vec3{pos.x + 0.5f, static_cast<float>(pos.y), pos.z - 1.5f}, DimensionType(0));
    co_await ctx.ticks(2);
    ItemStack sand;
    sand.reinit("minecraft:sand", 3, 0);
    sp.simulateSetItem(sand, true, 0);
    sp.simulateUseItemInSlotOnBlock(0, cm::game::blockPos(pos), ScriptModuleMinecraft::ScriptFacing::North, Vec3{0.5f, 0.5f, 0.f});
    co_await ctx.ticks(3);
    rec = ins::find(pos);
    ctx.require(rec != nullptr, "tracked");
    ctx.check(rec->item == "minecraft:sand", "sand inserted (" + rec->item + ")");
    ctx.check(sp.getSelectedItem().mCount == 2, "one sand used");
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:dust", -1) == 1, "sand texture shown");
    ctx.check(blockAt(ctx, above(pos, 0)).getTypeName() == ins::kHourglass, "no block placed instead");
});
CM_REGISTER_SELFTEST("instrument.globe_skins", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(606);
    auto* rec = co_await place(ctx, pos, std::string(ins::kGlobe));
    ins::Record r;
    r.kind = ins::Kind::Globe;
    int base = ins::globe::skinFor(r, 0);
    ctx.check(base == 0 || base == 3, "plain globe uses the overworld map (or Earth without random_world)");
    r.name = "Flat Earth";
    ctx.check(ins::globe::skinFor(r, 0) == 4 + base, "'flat earth' -> flat model");
    r.name = "LUNA";
    ctx.check(ins::globe::skinFor(r, 0) == 12, "'luna' -> moon");
    r.name = "Terra";
    ctx.check(ins::globe::skinFor(r, 0) == 3, "'terra' -> earth");
    r.name = "helios";
    ctx.check(ins::globe::skinFor(r, 0) == 13, "'helios' -> sun");
    r.name = "sphere";
    ctx.check(ins::globe::skinFor(r, 1) == 8 + (base == 3 ? 3 : 1), "'sphere' in the nether -> round, nether map");
    r.sheared = true;
    ctx.check(ins::globe::skinFor(r, 0) == 14, "sheared wins");
    rec->name  = "moon";
    rec->skin  = -1;
    rec->dirty = true;
    co_await ctx.ticks(2);
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:skin", -1) == 12, "a globe named moon shows the moon");
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:map", -1) == ins::globe::mapVariant(), "map state follows the globe seed");
});

CM_REGISTER_SELFTEST("instrument.globe_seed", [](Context& ctx) {
    auto saved  = cm::game::db::load("cm:instrument:globe_seed"); // a custom seed of this world, kept
    auto before = ins::globe::seed();
    ins::globe::setSeed(before + 12345, true);
    ctx.check(ins::globe::seed() == before + 12345, "custom seed is used");
    ins::globe::resetSeedCache();
    ctx.check(ins::globe::seed() == before + 12345, "custom seed persists in the world DB");
    ins::globe::setSeed(0, false);
    ins::globe::resetSeedCache();
    auto level = ll::service::getLevel();
    ctx.check(ins::globe::seed() == level->getLevelSeed64().mValue, "reset goes back to the world seed");
    int variant = ins::globe::mapVariant();
    ctx.check(variant >= 0 && variant < 16, "map variant in range");
    if (saved) {
        cm::game::db::save("cm:instrument:globe_seed", *saved);
        ins::globe::resetSeedCache();
    }
});

CM_REGISTER_SELFTEST("instrument.globe_survival", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(609);
    (void)co_await place(ctx, pos, std::string(ins::kGlobe));
    ctx.setBlock(above(pos, -1), "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(blockAt(ctx, pos).getTypeName() != ins::kGlobe, "a globe without support breaks");
    co_await ctx.ticks(2);
    ctx.check(ins::find(pos) == nullptr, "and is forgotten");
});

CM_REGISTER_SELFTEST("instrument.clock_hands", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.check(ins::clock::hands(0) == std::pair{6, 0}, "time 0 is 6:00");
    ctx.check(ins::clock::hands(6000) == std::pair{0, 0}, "time 6000 is 12:00");
    ctx.check(ins::clock::hands(18500) == std::pair{0, 6}, "time 18500 is 0:30");
    auto  pos   = ctx.testPos(612);
    auto* rec   = co_await place(ctx, pos, std::string(ins::kClock));
    auto  level = ll::service::getLevel();
    int   saved = level->getTime();
    level->setTime(7500); // 13:30
    co_await ctx.ticks(21);
    auto const& b = blockAt(ctx, pos);
    auto [h, m]   = ins::clock::hands(ins::clock::dayTime());
    ctx.check(ins::state(b, "complementaries:hour", -1) == h, "hour hand follows the time");
    ctx.check(ins::state(b, "complementaries:minute", -1) == m, "minute hand follows the time");
    rec = ins::find(pos);
    ctx.check(rec && rec->power == 5, "comparator power is daytime / 1500 (7500 -> 5), got " + std::to_string(rec ? rec->power : -1));
    ctx.check(comparatorRead(ctx, pos) == 5, "comparator reads 5");
    auto text = ins::clock::timeText(ctx.player(), regionOf(ctx));
    ctx.check(text.find(":") != std::string::npos, "time text: " + text);
    level->setTime(saved);
});

CM_REGISTER_SELFTEST("instrument.clock_two_faced", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(615);
    (void)co_await place(ctx, pos, std::string(ins::kClock));
    auto& region = regionOf(ctx);
    auto const& b = region.getBlock(cm::game::blockPos(pos));
    ctx.require(ins::apply(region, cm::game::blockPos(pos), b, ins::withStates(b, {{"complementaries:two_faced", 1}})), "made two-faced");
    co_await ctx.ticks(2);
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:two_faced", -1) == 1, "two-faced state kept");
    ctx.check(ins::find(pos) != nullptr, "still the same tracked clock");
});

CM_REGISTER_SELFTEST("instrument.hourglass_flow", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.check(ins::hourglass::dustOf("minecraft:sand").has_value(), "sand is a dust");
    ctx.check(ins::hourglass::dustOf("minecraft:glowstone_dust")->light == 10, "glowstone dust glows");
    ctx.check(!ins::hourglass::dustOf("minecraft:stone").has_value(), "stone is not a dust");
    auto  pos    = ctx.testPos(618);
    auto* rec    = co_await place(ctx, pos, std::string(ins::kHourglass));
    auto& region = regionOf(ctx);
    {
        auto const& b = blockAt(ctx, pos);
        ins::apply(region, cm::game::blockPos(pos), b, ins::withStringState(b, "minecraft:block_face", "up"));
    }
    co_await ctx.ticks(3);
    rec = ins::find(pos);
    ctx.require(rec != nullptr, "tracked after turning upright");
    ctx.check(ins::stringState(blockAt(ctx, pos), "minecraft:block_face") == "up", "placed upright");
    ctx.check(comparatorRead(ctx, pos) == 1, "empty upright hourglass outputs 1, got " + std::to_string(comparatorRead(ctx, pos)));
    ins::hourglass::insert(*rec, region, cm::game::blockPos(pos), "minecraft:sugar", 0); // 40 ticks
    co_await ctx.ticks(21);
    rec = ins::find(pos);
    ctx.require(rec != nullptr, "tracked");
    ctx.check(rec->progress > 0.3f && rec->progress < 0.7f, "about half way after 20 ticks");
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:dust", -1) == 4, "sugar texture shown");
    int mid = comparatorRead(ctx, pos);
    ctx.check(mid > 1 && mid < 15, "comparator follows the flow (" + std::to_string(mid) + ")");
    co_await ctx.ticks(25);
    ctx.check(rec->progress == 1.f, "finished");
    ctx.check(comparatorRead(ctx, pos) == 15, "full lower bulb outputs 15");
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:level", -1) == 15, "lower bulb full");
    ins::hourglass::flip(*rec, region, cm::game::blockPos(pos));
    co_await ctx.ticks(2);
    ctx.check(ins::stringState(blockAt(ctx, pos), "minecraft:block_face") == "down", "flipped");
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:level", -1) <= 1, "sand now on top");
    co_await ctx.ticks(42);
    ctx.check(rec->progress == 0.f, "flowed back");
    ctx.check(comparatorRead(ctx, pos) == 15, "finished upside down outputs 15");
});

CM_REGISTER_SELFTEST("instrument.hourglass_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos    = ctx.testPos(621);
    auto* rec    = co_await place(ctx, pos, std::string(ins::kHourglass));
    auto& region = regionOf(ctx);
    ins::hourglass::insert(*rec, region, cm::game::blockPos(pos), "minecraft:redstone", 0);
    co_await ctx.ticks(2);
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:dust", -1) == 10, "redstone texture shown");
    ctx.setBlock(pos, "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(ins::find(pos) == nullptr, "forgotten after removal");
    AABB box{Vec3{pos.x - 2.f, pos.y - 2.f, pos.z - 2.f}, Vec3{pos.x + 3.f, pos.y + 3.f, pos.z + 3.f}};
    bool dropped = false;
    for (auto* a : region.getEntities(box, 1.f, ActorType::ItemEntity)) {
        auto& item = static_cast<ItemActor*>(a)->item();
        if (item.getTypeName() == "minecraft:redstone") {
            dropped = true;
            a->remove();
        }
    }
    ctx.check(dropped, "the contents drop");
});

CM_REGISTER_SELFTEST("instrument.wind_vane", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos    = ctx.testPos(624);
    auto* rec    = co_await place(ctx, pos, std::string(ins::kWindVane));
    auto& region = regionOf(ctx);
    co_await ctx.ticks(21);
    int wind = ins::state(blockAt(ctx, pos), "complementaries:wind", -1);
    ctx.check(wind == ins::windvane::weatherStrength(region), "wind follows the weather");
    ctx.check(comparatorRead(ctx, pos) == wind, "comparator reads the wind strength");
    rec = ins::find(pos);
    ctx.require(rec != nullptr, "tracked");
    ins::windvane::charge(*rec, region, cm::game::blockPos(pos));
    co_await ctx.ticks(2);
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:wind", -1) == 3, "wind charged -> 3");
    int yaw = ins::state(blockAt(ctx, pos), "complementaries:yaw", -1);
    co_await ctx.ticks(3);
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:yaw", -1) != yaw, "the vane spins");
    co_await ctx.ticks(45);
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:wind", -1) == ins::windvane::weatherStrength(region), "charge wears off");
});

CM_REGISTER_SELFTEST("instrument.persistence", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(627);
    auto* rec = co_await place(ctx, pos, std::string(ins::kGlobe));
    rec->name    = "earth";
    rec->sheared = true;
    rec->skin    = -1;
    rec->dirty   = true;
    co_await ctx.ticks(2);
    ctx.check(ins::state(blockAt(ctx, pos), "complementaries:skin", -1) == 14, "sheared skin");
    ctx.check(ins::recordCount() >= 1, "records kept");
});
