// Selftests of U14 flag (test area dx 1400..1499).

#include "feature/flag/Flag.h"

#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/phys/AABB.h"

#include "game/Blocks.h"
#include "mod/I18n.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
using namespace cm::feature::flag;
namespace game = cm::game;

BlockPos bp(cm::core::GlobalPos const& p, int dx = 0, int dy = 0, int dz = 0) { return {p.x + dx, p.y + dy, p.z + dz}; }
cm::core::GlobalPos gp(cm::core::GlobalPos p, int dx = 0, int dy = 0, int dz = 0) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

int countFlagDrops(BlockSource& region, BlockPos const& pos, std::string const& id, bool withPatterns) {
    int  n = 0;
    AABB box{Vec3{pos.x - 2, pos.y - 2, pos.z - 2}, Vec3{pos.x + 3, pos.y + 3, pos.z + 3}};
    for (auto* a : region.getEntities(box, 1.0f, ActorType::ItemEntity)) {
        if (!a || !a->isType(ActorType::ItemEntity)) continue;
        auto& item = static_cast<ItemActor*>(a)->item();
        if (item.getTypeName() != id) continue;
        if (withPatterns == !readLayers(item).empty()) ++n;
        a->remove();
    }
    return n;
}

/// Looks the simulated player along a horizontal direction (Java 2D facing 0 S, 1 W, 2 N, 3 E).
void look(Player& p, int facing2d, float pitch = 0.f) {
    auto& sp = static_cast<SimulatedPlayer&>(p);
    auto  [dx, dz] = game::facingOffset(facing2d);
    auto  pos      = p.getPosition();
    sp.simulateLookAt(Vec3{pos.x + dx * 10.f, pos.y - pitch, pos.z + dz * 10.f});
}
} // namespace

CM_REGISTER_SELFTEST("flag.codec", [](Context& ctx) {
    auto flag = makeItem("complementaries:flag_red");
    ctx.require(!flag.isNull(), "flag item exists (behavior pack loaded)");
    ctx.check(readLayers(flag).empty(), "a new flag has no patterns");
    Layers layers{{"bo", 15}, {"cre", 4}, {"dragon", 0}};
    writeLayers(flag, layers);
    ctx.check(readLayers(flag) == layers, "patterns read back from the item");
    ctx.check(!flag.getCustomLore().empty(), "the item lists its patterns");
    ctx.check(layersFromJson(layersToJson(layers)) == layers, "JSON round trip");
    ctx.check(layerProperty({"bs", 0}) == 16, "first pattern, white = 16");
    ctx.check(layerProperty({"dragon", 15}) == 43 * 16 + 15, "dragon black = last value");
    ctx.check(layerProperty({"nope", 3}) == 0, "unknown pattern renders nothing");
    ctx.check(layerName("bo", 14) == "Red Bordure", "layer names use Supplementaries wording");
    for (int i = 0; i < 3; ++i) addLayer(flag, {"ts", 1});
    ctx.check(!addLayer(flag, {"ts", 1}), "at most 6 layers");
    writeLayers(flag, {});
    ctx.check(readLayers(flag).empty() && flag.getCustomLore().empty(), "clearing removes patterns and lore");

    // flag_from_banner
    auto banner = makeItem("minecraft:banner", 1, toItemColor(14));
    writeLayers(banner, {{"mc", 0}});
    auto blank = makeItem("complementaries:flag_red");
    ctx.check(copyLayers(banner, blank), "a red banner's patterns copy onto a blank red flag");
    ctx.check(readLayers(blank) == Layers{{"mc", 0}}, "copied patterns");
    auto blue = makeItem("complementaries:flag_blue");
    ctx.check(!copyLayers(banner, blue), "colours must match");
    auto banner2 = makeItem("minecraft:banner", 1, toItemColor(14));
    ctx.check(!copyLayers(banner, banner2), "banner to banner is left to vanilla");
    auto blankBanner = makeItem("minecraft:banner", 1, toItemColor(14));
    ctx.check(copyLayers(blank, blankBanner), "a flag's patterns copy onto a blank banner");
    ctx.check(!copyLayers(banner, blank), "the target must be blank");

    ctx.check(dyeColor("minecraft:red_dye") == 14 && dyeColor("minecraft:ink_sac") == 15, "dye colours");
    ctx.check(cm::translate("cm.flag.pattern.dragon", "?", "en_US") == "Dragon Charge", "pattern names are translated");
    ctx.check(cm::translate("cm.flag.loom.title", "?", "zh_CN") != "?", "zh_CN strings exist");
});

CM_REGISTER_SELFTEST("flag.place_cloth", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1400);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    ctx.require(ctx.setBlock(base, "minecraft:gold_block"), "support block");
    ctx.cleanupBlock(gp(base, 0, 1));
    p.teleport(Vec3{base.x + 0.5f, base.y + 1.f, base.z + 3.5f}, p.getDimensionId());
    co_await ctx.ticks(2);
    look(p, 2, 3.f); // look north, the cloth extends north

    auto item = makeItem("complementaries:flag_blue");
    writeLayers(item, {{"cr", 0}, {"glb", 4}});
    int slot = ctx.give(p, item, 0);
    p.setSelectedSlot(slot);
    auto& sp = static_cast<SimulatedPlayer&>(p);
    sp.simulateUseItemInSlotOnBlock(slot, bp(base), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);

    auto flagPos = bp(base, 0, 1);
    ctx.require(game::blockIdAt(region, flagPos) == "complementaries:flag_blue", "the flag is placed on top");
    auto data = loadFlag(gp(base, 0, 1));
    ctx.require(data.has_value(), "flag data is stored");
    ctx.check(data->color == 11, "colour stored");
    ctx.check(data->layers == Layers({{"cr", 0}, {"glb", 4}}), "patterns stored from the item");
    ctx.check(data->facing == 0, "facing towards the player (south)");
    ctx.check(findCloth(region, flagPos) != nullptr, "the cloth entity stands in the flag");

    // a command-placed flag gets a blank cloth too
    auto other = gp(base, 2, 1);
    ctx.require(ctx.setBlock(other, "complementaries:flag_red"), "set a flag by id");
    co_await ctx.ticks(2);
    ctx.check(findCloth(region, bp(other)) != nullptr, "command-placed flags get a cloth");
    ctx.check(loadFlag(other).has_value() && loadFlag(other)->layers.empty(), "with no patterns");
    game::clearBlock(region, bp(other));
    co_await ctx.ticks(2);
    ctx.check(findCloth(region, bp(other)) == nullptr, "the cloth goes with its flag");
    ctx.check(!loadFlag(other).has_value(), "flag data is erased");
    countFlagDrops(region, bp(other), "complementaries:flag_red", false);
});

CM_REGISTER_SELFTEST("flag.break_drop", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1410, 0);
    pos.y += 1;
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    ctx.require(ctx.setBlock(pos, "complementaries:flag_green"), "flag block");
    co_await ctx.ticks(2);
    FlagData d{13, 1, {{"bo", 15}, {"mc", 4}}, "Banner of Tests"};
    saveFlag(pos, d);
    syncCloth(region, bp(pos), d);
    co_await ctx.ticks(1);
    auto level = ll::service::getLevel();
    BlockChangeContext cc{};
    level->destroyBlock(region, bp(pos), true, cc);
    co_await ctx.ticks(3);
    ctx.check(game::blockIdAt(region, bp(pos)) == "minecraft:air", "flag broken");
    ctx.check(countFlagDrops(region, bp(pos), "complementaries:flag_green", true) == 1, "the drop keeps its patterns");
    ctx.check(findCloth(region, bp(pos)) == nullptr, "cloth removed");
});

CM_REGISTER_SELFTEST("flag.pole", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1420);
    pos.y += 1;
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    if (!game::blockWith("complementaries:stick")) {
        ctx.info("no complementaries:stick block (sticks unit not present): pole move skipped");
        co_return;
    }
    ctx.require(ctx.setBlock(pos, "complementaries:stick"), "pole bottom");
    ctx.require(ctx.setBlock(gp(pos, 0, 1), "complementaries:flag_white"), "flag in the pole");
    ctx.require(ctx.setBlock(gp(pos, 0, 2), "complementaries:stick"), "pole top");
    co_await ctx.ticks(2);
    ctx.check(moveFlagAlongPole(region, bp(pos, 0, 1), true), "the flag moves up");
    co_await ctx.ticks(2);
    ctx.check(game::blockIdAt(region, bp(pos, 0, 2)) == "complementaries:flag_white", "flag on top");
    ctx.check(isStick(game::blockIdAt(region, bp(pos, 0, 1))), "stick below it");
    ctx.check(findCloth(region, bp(pos, 0, 2)) != nullptr, "the cloth moved with it");
    ctx.check(!moveFlagAlongPole(region, bp(pos, 0, 2), true), "no stick above: it stays");
    ctx.check(moveFlagAlongPole(region, bp(pos, 0, 2), false), "and moves down again");
    co_await ctx.ticks(2);
});

CM_REGISTER_SELFTEST("flag.awning_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto wall = ctx.testPos(1430);
    wall.y += 2;
    ctx.require(co_await ctx.ensureLoaded(wall), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    ctx.require(ctx.setBlock(wall, "minecraft:stone"), "wall");
    auto aw = gp(wall, 0, 0, -1); // north of the wall
    ctx.cleanupBlock(aw);
    // looking south at the wall, clicking its north face in the upper half
    std::array<int, 6> looking{South, Down, East, West, Up, North};
    auto s = awningPlacement(region, bp(aw), North, 0.75, looking);
    ctx.require(s.has_value(), "an awning can hang on the wall");
    ctx.check(s->facing2d == 2 && !s->bottom && !s->slanted, "facing north, upper half, flat");
    auto sb = awningPlacement(region, bp(aw), North, 0.25, looking);
    ctx.check(sb && sb->bottom, "a lower-half click makes a bottom awning");
    ctx.require(place(region, bp(aw), "complementaries:awning_red", {{"complementaries:facing", 2}, {"complementaries:bottom", 0}, {"complementaries:slanted", 1}}), "place it slanted");
    auto st = awningState(region.getBlock(bp(aw)));
    ctx.check(st && st->slanted && st->facing2d == 2, "states read back");
    // a second one in front continues the slant one block lower? (top-half click on the slanted one)
    auto front = gp(aw, 0, 0, -1);
    ctx.cleanupBlock(front);
    std::array<int, 6> lookN{North, Down, East, West, Up, South};
    auto s2 = awningPlacement(region, bp(front), North, 0.75, lookN);
    ctx.check(s2 && s2->slanted && s2->facing2d == 2, "continuing a slanted awning keeps the slant");
    auto n = awningNormal(*st, 69.44395478041653);
    ctx.check(n[2] < -0.3 && n[1] > 0.9, "slanted north normal tilts to the front (north)");
    auto flat = awningNormal({2, false, false}, 69.44);
    ctx.check(flat[1] == 1.0, "flat awnings bounce straight up");
    auto r = reflect({0.1, -0.8, 0}, flat);
    ctx.check(std::abs(r[1] - 0.8) < 1e-9 && std::abs(r[0] - 0.1) < 1e-9, "reflection");
    // support removed: the awning breaks with a drop
    game::clearBlock(region, bp(wall));
    co_await ctx.ticks(3);
    ctx.check(game::blockIdAt(region, bp(aw)) == "minecraft:air", "an unsupported awning breaks");
    AABB box{Vec3{aw.x - 2, aw.y - 3, aw.z - 2}, Vec3{aw.x + 3, aw.y + 3, aw.z + 3}};
    int  drops = 0;
    for (auto* a : region.getEntities(box, 1.0f, ActorType::ItemEntity)) {
        if (a && a->isType(ActorType::ItemEntity) && static_cast<ItemActor*>(a)->item().getTypeName() == "complementaries:awning_red") {
            ++drops;
            a->remove();
        }
    }
    ctx.check(drops == 1, "and drops itself");
});

CM_REGISTER_SELFTEST("flag.awning_use", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto wall = ctx.testPos(1440);
    wall.y += 1;
    ctx.require(co_await ctx.ensureLoaded(wall), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    ctx.require(ctx.setBlock(wall, "minecraft:stone"), "wall");
    auto aw = gp(wall, 0, 0, -1);
    ctx.cleanupBlock(aw);
    p.teleport(Vec3{wall.x + 0.5f, wall.y - 0.f, wall.z - 3.5f}, p.getDimensionId());
    co_await ctx.ticks(2);
    look(p, 0, 0.f); // look south at the wall
    int slot = ctx.give(p, makeItem("complementaries:awning_blue", 4), 0);
    p.setSelectedSlot(slot);
    auto& sp = static_cast<SimulatedPlayer&>(p);
    sp.simulateUseItemInSlotOnBlock(slot, bp(wall), ScriptModuleMinecraft::ScriptFacing::North, Vec3{0.5f, 0.8f, 0.f});
    co_await ctx.ticks(3);
    auto s = awningState(region.getBlock(bp(aw)));
    ctx.require(s.has_value(), "using an awning on a wall places it");
    ctx.check(s->facing2d == 2 && !s->bottom, "facing away from the wall, upper half");
    ctx.check(p.getInventory().getItem(slot).mCount == 3, "one awning used");
    // empty hand toggles the slant
    // empty hand tilts it (the real click is covered by the client scenario: simulated players
    // cannot use an empty hand on a block)
    ctx.check(toggleAwningSlant(region, bp(aw)), "toggle");
    co_await ctx.ticks(1);
    auto s2 = awningState(region.getBlock(bp(aw)));
    ctx.check(s2 && s2->slanted, "an empty hand tilts the awning");
});

CM_REGISTER_SELFTEST("flag.awning_bounce", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1450);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    ctx.require(ctx.setBlock(gp(pos, 0, 1), "minecraft:stone"), "support");
    ctx.require(ctx.setBlock(gp(pos, 1, 1), "complementaries:awning_white", {{"complementaries:facing", 3}, {"complementaries:bottom", 1}, {"complementaries:slanted", 0}}), "flat bottom awning east of the support");
    p.teleport(Vec3{pos.x + 1.5f, pos.y + 9.f, pos.z + 0.5f}, p.getDimensionId());
    float lowest = 1000.f, peak = -1000.f;
    bool  landed = false;
    for (int i = 0; i < 60; ++i) {
        co_await ctx.ticks(1);
        float y = p.getFeetPos().y;
        if (!landed) {
            lowest = std::min(lowest, y);
            if (y < pos.y + 2.2f) landed = true;
        } else {
            peak = std::max(peak, y);
        }
    }
    ctx.check(landed, "the player fell onto the awning");
    ctx.check(peak > pos.y + 2.5f, "and bounced up (peak " + std::to_string(peak - pos.y) + ")");
    ctx.check(p.getHealth() == p.getMaxHealth(), "no fall damage");
});

CM_REGISTER_SELFTEST("flag.bunting", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1460);
    pos.y += 3;
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "ceiling / wall block");
    auto under = gp(pos, 0, -1);
    auto side  = gp(pos, 1, 0);
    ctx.cleanupBlock(under);
    ctx.cleanupBlock(side);
    std::array<int, 6> lookUp{Up, East, North, South, West, Down};
    auto c = buntingPlacement(region, bp(under), 14, lookUp);
    ctx.require(c.has_value() && c->id == "complementaries:bunting_red", "a ceiling bunting under the block");
    ctx.check(c->state == 1, "axis x when looking east");
    std::array<int, 6> lookW{West, Down, North, South, Up, East};
    auto w = buntingPlacement(region, bp(side), 14, lookW);
    ctx.require(w.has_value() && w->id == "complementaries:bunting_wall_red", "a wall bunting on the side");
    ctx.check(w->state == 3, "facing east, away from the wall");
    ctx.check(!buntingPlacement(region, bp(pos, 3, 0), 14, lookW).has_value(), "no support: nothing placed");
    ctx.require(place(region, bp(under), c->id, {{"complementaries:axis", c->state}}), "place ceiling");
    ctx.require(place(region, bp(side), w->id, {{"complementaries:facing", w->state}}), "place wall");
    co_await ctx.ticks(1);
    game::clearBlock(region, bp(pos));
    co_await ctx.ticks(3);
    ctx.check(game::blockIdAt(region, bp(under)) == "minecraft:air", "the ceiling bunting falls with its block");
    ctx.check(game::blockIdAt(region, bp(side)) == "minecraft:air", "the wall bunting too");
    AABB box{Vec3{pos.x - 2, pos.y - 4, pos.z - 2}, Vec3{pos.x + 3, pos.y + 3, pos.z + 3}};
    int  drops = 0;
    for (auto* a : region.getEntities(box, 1.0f, ActorType::ItemEntity)) {
        if (a && a->isType(ActorType::ItemEntity) && static_cast<ItemActor*>(a)->item().getTypeName() == "complementaries:bunting_red") {
            drops += static_cast<ItemActor*>(a)->item().mCount;
            a->remove();
        }
    }
    ctx.check(drops == 2, "both drop a bunting (" + std::to_string(drops) + ")");
});

// Builds a scene for the client scenario's screenshots (kept after the test): flags in the four
// facings with patterns, flat/slanted awnings in both halves and buntings, in front of a wall.
// Viewpoint: "SELFTEST INFO flag.showcase: view <x> <y> <z>" looking south.
CM_REGISTER_SELFTEST("flag.showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1490);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    int   x = base.x, y = base.y, z = base.z;
    for (int dx = -6; dx <= 6; ++dx)
        for (int dy = 0; dy < 5; ++dy) place(region, {x + dx, y + dy, z + 4}, "minecraft:stone_bricks");
    for (int dx = -6; dx <= 6; ++dx)
        for (int dz = -8; dz <= 3; ++dz)
            for (int dy = 0; dy < 5; ++dy) game::clearBlock(region, {x + dx, y + dy, z + dz});
    Layers patterns[4] = {
        {{"bo", 15}, {"cre", 5}},
        {{"cr", 0}, {"mc", 14}},
        {{"ts", 11}, {"bs", 4}, {"dragon", 15}},
        {{"ss", 0}, {"bri", 12}},
    };
    int colors[4] = {14, 11, 4, 13};
    for (int f = 0; f < 4; ++f) {
        BlockPos p{x - 5 + f * 3, y, z - 2};
        place(region, p, "complementaries:flag_" + std::string(kColorNames[colors[f]]));
        co_await ctx.ticks(2);
        FlagData d{colors[f], f, patterns[f], ""};
        saveFlag(game::globalPos(region, p), d);
        ctx.check(syncCloth(region, p, d) != nullptr, "cloth " + std::to_string(f));
    }
    // awnings on the wall (facing north, towards the viewer)
    struct A { int dx, dy, bottom, slanted; std::string_view id; };
    for (auto const& a : {A{-5, 3, 0, 0, "awning_red"}, A{-4, 3, 0, 1, "awning_blue"}, A{-3, 2, 1, 0, "awning"}, A{-2, 2, 1, 1, "awning_lime"}})
        place(region, {x + a.dx, y + a.dy, z + 3}, "complementaries:" + std::string(a.id), {{"complementaries:facing", 2}, {"complementaries:bottom", a.bottom}, {"complementaries:slanted", a.slanted}});
    for (int dx = 0; dx < 3; ++dx) place(region, {x + dx, y + 3, z + 3}, "complementaries:bunting_wall_" + std::string(kColorNames[dx * 5]), {{"complementaries:facing", 2}});
    place(region, {x + 4, y + 4, z + 1}, "minecraft:stone_bricks");
    place(region, {x + 4, y + 3, z + 1}, "complementaries:bunting_pink", {{"complementaries:axis", 0}});
    place(region, {x + 5, y + 4, z + 1}, "minecraft:stone_bricks");
    place(region, {x + 5, y + 3, z + 1}, "complementaries:bunting_cyan", {{"complementaries:axis", 1}});
    place(region, {x + 3, y, z - 4}, "minecraft:loom");
    co_await ctx.ticks(2);
    ctx.info("view " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z - 7));
    ctx.info("awning " + std::to_string(x - 5) + " " + std::to_string(y + 3) + " " + std::to_string(z + 3));
    ctx.info("loom " + std::to_string(x + 3) + " " + std::to_string(y) + " " + std::to_string(z - 4));
});
