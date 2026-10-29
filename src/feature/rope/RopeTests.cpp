// Selftests of U11 `rope` (dx 1100..1199).

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/rope/Common.h"
#include "feature/rope/Rope.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
namespace rope = cm::feature::rope;
namespace game = cm::game;
using cm::core::GlobalPos;

GlobalPos up(GlobalPos p, int n = 1) {
    p.y += n;
    return p;
}
GlobalPos east(GlobalPos p, int n = 1) {
    p.x += n;
    return p;
}

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

std::string idAt(Context& ctx, GlobalPos const& p) { return game::blockIdAt(regionOf(ctx), game::blockPos(p)); }

int stateAt(Context& ctx, GlobalPos const& p, std::string_view s) {
    return rope::stateOf(regionOf(ctx).getBlock(game::blockPos(p)), s);
}

/// Clears a column of `h` blocks above `base` (inclusive) and remembers it for cleanup.
void clearColumn(Context& ctx, GlobalPos base, int h) {
    for (int i = 0; i < h; ++i) {
        ctx.cleanupBlock(up(base, i));
        game::clearBlock(regionOf(ctx), game::blockPos(up(base, i)));
    }
}

int countItem(Player& p, std::string_view id) {
    int   n   = 0;
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& s = inv.getItem(i);
        if (!s.isNull() && s.getTypeName() == id) n += s.mCount;
    }
    return n;
}
} // namespace

CM_REGISTER_SELFTEST("rope.registered", [](Context& ctx) {
    for (auto id : {rope::kRope, rope::kPulley, rope::kFlaxCrop, rope::kWildFlax, rope::kFlaxBlock}) {
        ctx.check(game::blockWith(id) != nullptr, std::string(id) + " is registered");
    }
    auto const* r = game::blockWith(rope::kRope);
    ctx.require(r != nullptr, "rope block exists");
    ctx.check(r->hasProperty(BlockProperty::Climbable), "rope is climbable");
    ctx.check(rope::types().rope != nullptr, "rope type resolved");
    ctx.check(rope::config().version >= 1, "rope config loaded");
    ctx.check(cm::translate("gui.supplementaries.pulley_block", "?", "en_US") == "Pulley", "en_US strings");
    ctx.check(cm::translate("gui.supplementaries.pulley_block", "?", "zh_CN") != "?", "zh_CN strings");
    auto arrow = rope::makeStack(rope::kRopeArrow, 1);
    ctx.check(!arrow.isNull(), "rope arrow item exists");
    ctx.check(!rope::makeStack(rope::kFlaxSeeds, 1).isNull(), "flax seeds item exists");
});

CM_REGISTER_SELFTEST("rope.hang_and_connect", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1100);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 6);
    ctx.require(ctx.setBlock(up(base, 4), "minecraft:stone"), "support");
    ctx.require(ctx.setBlock(up(base, 3), std::string(rope::kRope)), "rope 1");
    ctx.require(ctx.setBlock(up(base, 2), std::string(rope::kRope)), "rope 2");
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, up(base, 3)) == rope::kRope && idAt(ctx, up(base, 2)) == rope::kRope, "hanging ropes stay");
    ctx.check(stateAt(ctx, up(base, 3), "complementaries:up") == 1, "top rope connects up");
    ctx.check(stateAt(ctx, up(base, 3), "complementaries:down") == 1, "top rope connects down");
    ctx.check(stateAt(ctx, up(base, 3), "complementaries:distance") == 0, "top rope distance 0");
    ctx.check(stateAt(ctx, up(base, 2), "complementaries:distance") == 0, "lower rope inherits distance");
    ctx.check(stateAt(ctx, up(base, 2), "complementaries:knot") == 0, "a straight rope has no knot");
    // cut the support: the column falls apart
    game::clearBlock(regionOf(ctx), game::blockPos(up(base, 4)));
    co_await ctx.ticks(6);
    ctx.check(idAt(ctx, up(base, 3)) != rope::kRope, "top rope broke without support");
    ctx.check(idAt(ctx, up(base, 2)) != rope::kRope, "lower rope broke without support");
});

CM_REGISTER_SELFTEST("rope.unsupported_breaks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1102);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 6);
    ctx.require(ctx.setBlock(up(base, 4), std::string(rope::kRope)), "floating rope");
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, up(base, 4)) != rope::kRope, "a rope with no support breaks");
    ctx.check(rope::ropes::distanceAt(regionOf(ctx), game::blockPos(up(base, 4))) == 7, "distance 7 in mid air");
});

CM_REGISTER_SELFTEST("rope.horizontal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1104);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto y3 = up(base, 3);
    for (int i = 0; i < 10; ++i) clearColumn(ctx, east(base, i), 6);
    ctx.require(ctx.setBlock(y3, "minecraft:stone"), "wall");
    for (int i = 1; i <= 9; ++i) ctx.setBlock(east(y3, i), std::string(rope::kRope));
    co_await ctx.ticks(10);
    ctx.check(stateAt(ctx, east(y3, 1), "complementaries:distance") == 0, "rope tied to the wall: distance 0");
    ctx.check(stateAt(ctx, east(y3, 1), "complementaries:west") == 1, "tied rope connects to the wall");
    ctx.check(stateAt(ctx, east(y3, 1), "complementaries:east") == 1, "tied rope connects to the next rope");
    ctx.check(stateAt(ctx, east(y3, 3), "complementaries:distance") == 2, "distance grows along the rope");
    ctx.check(stateAt(ctx, east(y3, 3), "complementaries:knot") == 0, "straight horizontal rope: no knot");
    ctx.check(idAt(ctx, east(y3, 7)) == rope::kRope, "6 blocks out is still supported");
    ctx.check(idAt(ctx, east(y3, 8)) != rope::kRope, "7 blocks out breaks");
    ctx.check(idAt(ctx, east(y3, 9)) != rope::kRope, "8 blocks out breaks");
    // a rope hanging under a horizontal rope
    ctx.setBlock(up(east(y3, 2), -1), std::string(rope::kRope));
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, up(east(y3, 2), -1)) == rope::kRope, "rope hangs under a horizontal rope");
    ctx.check(stateAt(ctx, east(y3, 2), "complementaries:knot") == 1, "junction has a knot");
});

CM_REGISTER_SELFTEST("rope.add_and_remove_down", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1106);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 8);
    ctx.require(ctx.setBlock(up(base, 6), "minecraft:oak_planks"), "support");
    ctx.require(ctx.setBlock(up(base, 5), std::string(rope::kRope)), "rope");
    co_await ctx.ticks(2);
    auto& r = regionOf(ctx);
    auto  a = rope::ropes::addRopeDown(r, game::blockPos(up(base, 5)));
    auto  b = rope::ropes::addRopeDown(r, game::blockPos(up(base, 5)));
    ctx.check(a && a->y == base.y + 4, "first added rope under the column");
    ctx.check(b && b->y == base.y + 3, "second added rope under the first");
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, up(base, 3)) == rope::kRope, "added ropes stay");
    ctx.check(
        game::blockPos(up(base, 3)) == rope::ropes::bottomOf(r, game::blockPos(up(base, 5))),
        "bottom of the column"
    );
    auto removed = rope::ropes::removeRopeDown(r, game::blockPos(up(base, 5)));
    ctx.check(removed && removed->y == base.y + 3, "removes the lowest rope");
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, up(base, 3)) != rope::kRope, "lowest rope is gone");
    ctx.check(idAt(ctx, up(base, 4)) == rope::kRope, "the rest stays");
});

CM_REGISTER_SELFTEST("rope.player_extend_and_pull", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1108);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 8);
    ctx.require(ctx.setBlock(up(base, 5), "minecraft:oak_planks"), "support");
    ctx.require(ctx.setBlock(up(base, 4), std::string(rope::kRope)), "rope");
    co_await ctx.ticks(2);
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    sp.teleport(
        Vec3{static_cast<float>(base.x) + 1.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 0.5f},
        sp.getDimensionId()
    );
    co_await ctx.ticks(2);
    int slot = ctx.give(sp, rope::makeStack(rope::kRope, 3), 0);
    ctx.require(slot == 0, "rope in slot 0");
    bool used = sp.simulateUseItemInSlotOnBlock(
        0,
        game::blockPos(up(base, 4)),
        ScriptModuleMinecraft::ScriptFacing::East,
        Vec3{1.0f, 0.5f, 0.5f}
    );
    ctx.info(std::string("use item on rope: ") + (used ? "ok" : "refused"));
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, up(base, 3)) == rope::kRope, "using a rope on a rope extends it downwards");
    ctx.check(countItem(sp, rope::kRope) == 2, "one rope used");
    // pull it up with an empty hand
    sp.getInventory().setItem(0, ItemStack::EMPTY_ITEM());
    sp.refreshInventory();
    co_await ctx.ticks(1);
    ctx.check(
        rope::ropes::onUse(sp, game::blockPos(up(base, 4)), 5, ItemStack::EMPTY_ITEM()),
        "empty-hand use is handled"
    );
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, up(base, 3)) != rope::kRope, "pulling removes the lowest rope");
    ctx.check(idAt(ctx, up(base, 4)) == rope::kRope, "the clicked rope stays");
    ctx.check(countItem(sp, rope::kRope) == 1, "the pulled rope goes to the player");
});

CM_REGISTER_SELFTEST("rope.bell", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1110);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 8);
    ctx.require(ctx.setBlock(up(base, 6), "minecraft:oak_planks"), "support");
    ctx.require(
        ctx.setBlock(
            up(base, 5),
            "minecraft:bell",
            {
                {"attachment", 1}
    }
        ),
        "bell"
    );
    co_await ctx.ticks(2);
    ctx.require(idAt(ctx, up(base, 5)) == "minecraft:bell", "bell placed");
    ctx.setBlock(up(base, 4), std::string(rope::kRope));
    ctx.setBlock(up(base, 3), std::string(rope::kRope));
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, up(base, 3)) == rope::kRope, "rope hangs under the bell");
    ctx.check(
        rope::ropes::ringConnectedBell(regionOf(ctx), game::blockPos(up(base, 3)), &ctx.player()),
        "the bell rings"
    );
    ctx.check(
        !rope::ropes::ringConnectedBell(regionOf(ctx), game::blockPos(up(base, 6)), &ctx.player()),
        "no bell above planks"
    );
});

CM_REGISTER_SELFTEST("rope.pulley", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1112);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 8);
    auto top = up(base, 6);
    ctx.require(ctx.setBlock(top, std::string(rope::kPulley)), "pulley");
    co_await ctx.ticks(1);
    auto& r  = regionOf(ctx);
    auto  pp = game::blockPos(top);
    rope::pulley::setContent(top, {std::string(rope::kRope), 3});
    ctx.check(rope::turnPulley(r, pp, false), "unwind 1");
    ctx.check(rope::turnPulley(r, pp, false), "unwind 2");
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, up(base, 5)) == rope::kRope && idAt(ctx, up(base, 4)) == rope::kRope, "two ropes lowered");
    ctx.check(rope::pulley::contentAt(top).count == 1, "one rope left on the pulley");
    ctx.check(stateAt(ctx, top, "complementaries:type") == 1, "pulley shows rope");
    // a block hanging at the end is pushed down and pulled up
    ctx.require(ctx.setBlock(up(base, 3), "minecraft:oak_planks"), "hanging block");
    ctx.check(rope::turnPulley(r, pp, false), "unwind pushes the block down");
    co_await ctx.ticks(2);
    ctx.check(
        idAt(ctx, up(base, 3)) == rope::kRope && idAt(ctx, up(base, 2)) == "minecraft:oak_planks",
        "block lowered"
    );
    ctx.cleanupBlock(up(base, 2));
    ctx.check(rope::pulley::contentAt(top).count == 0, "pulley empty");
    ctx.check(stateAt(ctx, top, "complementaries:type") == 0, "pulley shows nothing");
    ctx.check(!rope::turnPulley(r, pp, false), "nothing to unwind");
    ctx.check(rope::turnPulley(r, pp, true), "wind 1");
    co_await ctx.ticks(2);
    ctx.check(
        idAt(ctx, up(base, 3)) == "minecraft:oak_planks" && idAt(ctx, up(base, 2)) != "minecraft:oak_planks",
        "block pulled up"
    );
    ctx.check(rope::pulley::contentAt(top).count == 1, "rope back on the pulley");
    ctx.check(rope::turnPulley(r, pp, true) && rope::turnPulley(r, pp, true), "wind 2 and 3");
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, up(base, 5)) == "minecraft:oak_planks", "block under the pulley");
    ctx.check(!rope::turnPulley(r, pp, true), "no rope left to wind");
    ctx.check(rope::pulley::contentAt(top).count == 3, "all ropes back");
    rope::pulley::setContent(top, {});
});

CM_REGISTER_SELFTEST("rope.pulley_chain_and_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1114);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 8);
    auto top = up(base, 6);
    ctx.require(ctx.setBlock(top, std::string(rope::kPulley)), "pulley");
    co_await ctx.ticks(1);
    auto& r = regionOf(ctx);
    rope::pulley::setContent(top, {rope::chainId(), 2});
    ctx.check(rope::turnPulley(r, game::blockPos(top), false), "lower a chain");
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, up(base, 5)) == rope::chainId(), "chain under the pulley");
    ctx.check(stateAt(ctx, top, "complementaries:type") == 2, "pulley shows chain");
    // breaking the pulley drops (and forgets) its content
    game::clearBlock(r, game::blockPos(top));
    co_await ctx.ticks(3);
    ctx.check(rope::pulley::contentAt(top).count == 0, "content forgotten");
});

CM_REGISTER_SELFTEST("rope.flax_growth", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1116);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 4);
    auto soil = up(base, -1);
    ctx.require(
        ctx.setBlock(
            soil,
            "minecraft:farmland",
            {
                {"moisturized_amount", 7}
    }
        ),
        "farmland"
    );
    ctx.require(ctx.setBlock(base, std::string(rope::kFlaxCrop)), "crop");
    co_await ctx.ticks(2);
    auto& r   = regionOf(ctx);
    auto  pos = game::blockPos(base);
    ctx.check(rope::flax::tracked() >= 1, "crop tracked for random ticks");
    for (int i = 0; i < 4; ++i) rope::flax::growthTick(r, pos, true);
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, base, "complementaries:age") == 4, "age 4 after 4 growth ticks");
    ctx.check(
        idAt(ctx, up(base)) == rope::kFlaxCrop && stateAt(ctx, up(base), "complementaries:upper") == 1,
        "upper half grew"
    );
    ctx.check(stateAt(ctx, up(base), "complementaries:age") == 4, "upper half has the same age");
    for (int i = 0; i < 5; ++i) rope::flax::growthTick(r, pos, true);
    co_await ctx.ticks(1);
    ctx.check(stateAt(ctx, base, "complementaries:age") == 7, "age capped at 7");
    // breaking the lower half removes the upper one
    game::clearBlock(r, pos);
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, up(base)) != rope::kFlaxCrop, "upper half removed with the lower one");
    game::clearBlock(r, game::blockPos(soil));
});

CM_REGISTER_SELFTEST("rope.flax_blocked_and_bonemeal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1118);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 4);
    auto soil = up(base, -1);
    ctx.require(
        ctx.setBlock(
            soil,
            "minecraft:farmland",
            {
                {"moisturized_amount", 7}
    }
        ),
        "farmland"
    );
    ctx.require(ctx.setBlock(base, std::string(rope::kFlaxCrop)), "crop");
    ctx.require(ctx.setBlock(up(base), "minecraft:stone"), "blocked above");
    co_await ctx.ticks(1);
    auto& r   = regionOf(ctx);
    auto  pos = game::blockPos(base);
    for (int i = 0; i < 6; ++i) rope::flax::growthTick(r, pos, true);
    ctx.check(stateAt(ctx, base, "complementaries:age") == 3, "a blocked crop stops at age 3");
    game::clearBlock(r, game::blockPos(up(base)));
    ctx.check(rope::flax::boneMeal(r, pos), "bone meal grows it");
    co_await ctx.ticks(1);
    ctx.check(stateAt(ctx, base, "complementaries:age") >= 5, "bone meal adds 2..5 ages");
    ctx.check(idAt(ctx, up(base)) == rope::kFlaxCrop, "bone meal grows the upper half");
    game::clearBlock(r, game::blockPos(up(base)));
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, base) != rope::kFlaxCrop, "breaking the upper half breaks the lower half");
    game::clearBlock(r, game::blockPos(soil));
});

CM_REGISTER_SELFTEST("rope.rope_arrow", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1120);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    for (int i = 0; i < 3; ++i) clearColumn(ctx, east(base, i), 8);
    auto wall = up(base, 5);
    ctx.require(ctx.setBlock(wall, "minecraft:stone"), "wall");
    auto& sp = ctx.player();
    sp.teleport(
        Vec3{static_cast<float>(base.x) + 2.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 0.5f},
        sp.getDimensionId()
    );
    co_await ctx.ticks(2);
    int   before = countItem(sp, "minecraft:arrow");
    auto* arrow  = rope::arrow::shoot(sp, 3, 0.2f, false);
    ctx.require(arrow != nullptr, "rope arrow spawned");
    ctx.check(rope::arrow::chargesOf(*arrow) == 3, "arrow carries 3 charges");
    auto uid = arrow->getOrCreateUniqueID();
    // simulate the hit on the east face of the wall
    rope::arrow::onHitBlock(*arrow, game::blockPos(wall), 5);
    arrow = nullptr; // may be picked up from now on
    for (int i = 1; i <= 5; ++i) ctx.cleanupBlock(up(east(base), i));
    co_await ctx.ticks(5);
    ctx.check(idAt(ctx, east(wall)) == rope::kRope, "rope placed on the hit face");
    ctx.check(
        idAt(ctx, up(east(wall), -1)) == rope::kRope && idAt(ctx, up(east(wall), -2)) == rope::kRope,
        "ropes added below"
    );
    ctx.check(idAt(ctx, up(east(wall), -3)) != rope::kRope, "no rope beyond the charges");
    // a player walking over it picks up a plain arrow (no charge left)
    if (auto* a = sp.getLevel().fetchEntity(uid, false)) {
        ctx.check(rope::arrow::chargesOf(*a) == 0, "charges used up");
        sp.teleport(a->getPosition(), sp.getDimensionId());
    }
    co_await ctx.ticks(3);
    ctx.check(countItem(sp, "minecraft:arrow") == before + 1, "empty rope arrow gives an arrow back");
});

CM_REGISTER_SELFTEST("rope.rope_arrow_items", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1122);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 8);
    auto& sp = ctx.player();
    // crafted rope arrows carry their charges in the aux value until normalized
    auto crafted = rope::makeStack(rope::kRopeArrow, 1, 5);
    int  slot    = ctx.give(sp, crafted, 0);
    ctx.require(slot == 0, "rope arrow given");
    rope::arrow::normalize(sp);
    auto const& n = sp.getInventory().getItem(0);
    ctx.check(n.getAuxValue() == 0 && n.getDamageValue() == rope::kArrowCharges - 5, "aux 5 becomes 5 charges");
    // winding a rope column back into the arrow
    ctx.require(ctx.setBlock(up(base, 6), "minecraft:stone"), "support");
    ctx.setBlock(up(base, 5), std::string(rope::kRope));
    ctx.setBlock(up(base, 4), std::string(rope::kRope));
    ctx.setBlock(up(base, 3), std::string(rope::kRope));
    co_await ctx.ticks(2);
    sp.teleport(
        Vec3{static_cast<float>(base.x) + 1.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 0.5f},
        sp.getDimensionId()
    );
    co_await ctx.ticks(2);
    static_cast<SimulatedPlayer&>(sp).simulateUseItemInSlotOnBlock(
        0,
        game::blockPos(up(base, 4)),
        ScriptModuleMinecraft::ScriptFacing::East,
        Vec3{1.0f, 0.5f, 0.5f}
    );
    co_await ctx.ticks(4);
    ctx.check(
        idAt(ctx, up(base, 3)) != rope::kRope && idAt(ctx, up(base, 4)) != rope::kRope,
        "ropes wound into the arrow"
    );
    ctx.check(idAt(ctx, up(base, 5)) == rope::kRope, "ropes above the clicked one stay");
    ctx.check(sp.getInventory().getItem(0).getDamageValue() == rope::kArrowCharges - 7, "arrow gained 2 charges");
});

CM_REGISTER_SELFTEST("rope.climbable", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1124);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 8);
    ctx.require(ctx.setBlock(up(base, 6), "minecraft:stone"), "support");
    for (int i = 0; i < 6; ++i) ctx.setBlock(up(base, i), std::string(rope::kRope));
    co_await ctx.ticks(3);
    auto& sp = ctx.player();
    sp.teleport(
        Vec3{static_cast<float>(base.x) + 0.5f, static_cast<float>(base.y) + 3.0f, static_cast<float>(base.z) + 0.5f},
        sp.getDimensionId()
    );
    co_await ctx.ticks(10);
    ctx.check(sp.onClimbableBlock(), "a player inside a rope is climbing");
    ctx.check(sp.getPosition().y > static_cast<float>(base.y) + 1.0f, "the rope slows the fall");
});

CM_REGISTER_SELFTEST("rope.flax_block_fall", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1126);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearColumn(ctx, base, 2);
    ctx.require(ctx.setBlock(up(base, -1), std::string(rope::kFlaxBlock)), "flax block");
    ctx.check(rope::isFlaxBlock(regionOf(ctx).getBlock(game::blockPos(up(base, -1)))), "flax block type");
});

CM_REGISTER_SELFTEST("rope.wild_flax_feature", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1140);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& r = regionOf(ctx);
    // a grass patch to grow on
    for (int dx = -5; dx <= 5; ++dx) {
        for (int dz = -5; dz <= 5; ++dz) {
            GlobalPos g{base.dim, base.x + dx, base.y - 1, base.z + dz};
            ctx.setBlock(g, "minecraft:grass_block");
            for (int dy = 1; dy <= 3; ++dy) {
                ctx.cleanupBlock(up(g, dy));
                game::clearBlock(r, game::blockPos(up(g, dy)));
            }
        }
    }
    co_await ctx.ticks(1);
    auto level = ll::service::getLevel();
    ServerCommandOrigin
        origin("complementaries-selftest", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(
        "place feature complementaries:wild_flax_feature " + std::to_string(base.x) + " " + std::to_string(base.y) + " "
            + std::to_string(base.z),
        origin
    );
    co_await ctx.ticks(2);
    int n = 0;
    for (int dx = -5; dx <= 5; ++dx) {
        for (int dz = -5; dz <= 5; ++dz) {
            for (int dy = -1; dy <= 3; ++dy) {
                GlobalPos g{base.dim, base.x + dx, base.y + dy, base.z + dz};
                if (idAt(ctx, g) == rope::kWildFlax) ++n;
            }
        }
    }
    ctx.info("wild flax plants: " + std::to_string(n));
    ctx.check(n >= 3, "the wild flax patch places several plants");
});
