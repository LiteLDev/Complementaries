// Selftests of U13 tableware (test area dx 1300..1399).

#include <cmath>

#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/tableware/Tableware.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
using namespace cm::feature::tableware;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

Block const& blockAt(Context& ctx, cm::core::GlobalPos const& p) {
    return regionOf(ctx).getBlock(cm::game::blockPos(p));
}

int countItem(Player& player, std::string_view id, int aux = -1) {
    auto& inv = player.getInventory();
    int   n   = 0;
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& it = inv.getItem(i);
        if (itemId(it) == id && (aux < 0 || it.getAuxValue() == aux)) n += it.mCount;
    }
    return n;
}

void hold(Player& player, ItemStack const& item) {
    player.setSelectedItem(item);
    player.sendInventory(false);
}

bool use(Context& ctx, cm::core::GlobalPos const& p, FacingID face = FacingID::Up, Vec3 hit = {0.5f, 0.5f, 0.5f}) {
    return handleUse(ctx.player(), cm::game::blockPos(p), face, hit, true);
}

cm::core::GlobalPos above(cm::core::GlobalPos p) {
    ++p.y;
    return p;
}
} // namespace

CM_REGISTER_SELFTEST("tableware.registry", [](Context& ctx) {
    for (auto id : {kGoblet, kUrn, kPlanter, kFlowerBox, kBookPile, kBookPileLying, kPancake, kJarBoat}) {
        ctx.check(make(id) != nullptr, std::string(id) + " is registered");
    }
    ctx.check(!isEmpty(makeItem(kCandy)), "candy item exists");
    ctx.check(!isEmpty(makeItem(kPancake)), "pancake item exists");
    ctx.check(
        make(
            kUrn,
            {
                {"cm:treasure", 1}
    }
        ) != nullptr,
        "treasure urn state exists"
    );
    auto const* fb = make(
        kFlowerBox,
        {
            {"minecraft:cardinal_direction", std::string("east")},
            {"cm:wall",                      1                  }
    }
    );
    ctx.check(fb && direction(*fb) == "east" && state(*fb, "cm:wall") == 1, "flower box direction/wall states");
});

CM_REGISTER_SELFTEST("tableware.goblet", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1300);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(kGoblet)), "goblet placed");
    auto& player   = ctx.player();
    bool  survival = !creative(player);

    hold(player, makeItem("minecraft:potion", 1, 21)); // healing
    ctx.require(use(ctx, pos), "a potion fills the goblet");
    int fluid = state(blockAt(ctx, pos), "cm:fluid");
    ctx.check(fluid >= 6, "potion colour bucket state (got " + std::to_string(fluid) + ")");
    auto data = cm::game::db::load(posKey("goblet", pos.dim, cm::game::blockPos(pos)));
    ctx.check(data && data->value("aux", -1) == 21, "potion kept in the world DB");
    if (survival)
        ctx.check(itemId(player.getSelectedItem()) == "minecraft:glass_bottle", "the potion became a glass bottle");

    hold(player, makeItem("minecraft:glass_bottle"));
    ctx.require(use(ctx, pos), "a glass bottle empties the goblet");
    ctx.check(state(blockAt(ctx, pos), "cm:fluid") == 0, "goblet empty");
    ctx.check(countItem(player, "minecraft:potion", 21) >= 1, "got the healing potion back");

    hold(player, makeItem("minecraft:honey_bottle"));
    ctx.require(use(ctx, pos), "honey fills the goblet");
    ctx.check(state(blockAt(ctx, pos), "cm:fluid") == 2, "honey state");
    hold(player, ItemStack::EMPTY_ITEM());
    ctx.require(use(ctx, pos), "drinking with an empty hand");
    ctx.check(state(blockAt(ctx, pos), "cm:fluid") == 0, "goblet empty after drinking");
    ctx.check(!use(ctx, pos), "an empty goblet with an empty hand does nothing");
    co_return;
});

CM_REGISTER_SELFTEST("tableware.flower_box", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1310);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "support");
    auto        box = above(pos);
    auto const* b   = make(
        kFlowerBox,
        {
            {"minecraft:cardinal_direction", std::string("north")}
    }
    );
    ctx.require(b && place(regionOf(ctx), cm::game::blockPos(box), *b), "flower box placed");
    ctx.cleanupBlock(box);
    auto& player = ctx.player();

    hold(player, makeItem("minecraft:poppy"));
    ctx.require(use(ctx, box, FacingID::Up, {0.9f, 0.3f, 0.5f}), "poppy planted");
    ctx.check(state(blockAt(ctx, box), "cm:flower0") == 2, "left slot (east side when facing north) holds the poppy");
    hold(player, makeItem("minecraft:cornflower"));
    ctx.require(use(ctx, box, FacingID::Up, {0.1f, 0.3f, 0.5f}), "cornflower planted");
    ctx.check(state(blockAt(ctx, box), "cm:flower2") == 11, "right slot holds the cornflower");
    hold(player, makeItem("minecraft:stone"));
    ctx.check(!use(ctx, box, FacingID::Up, {0.5f, 0.3f, 0.5f}), "stone can't be planted");

    hold(player, ItemStack::EMPTY_ITEM());
    int before = countItem(player, "minecraft:poppy");
    ctx.require(use(ctx, box, FacingID::Up, {0.9f, 0.3f, 0.5f}), "take the poppy");
    ctx.check(state(blockAt(ctx, box), "cm:flower0") == 0, "slot emptied");
    ctx.check(countItem(player, "minecraft:poppy") == before + 1, "poppy returned");

    flower_box::onPlaced(player, cm::game::blockPos(box), FacingID::West);
    ctx.check(state(blockAt(ctx, box), "cm:wall") == 1 && direction(blockAt(ctx, box)) == "west", "wall placement");
    ctx.check(state(blockAt(ctx, box), "cm:flower2") == 11, "contents kept when turned");
    co_return;
});

CM_REGISTER_SELFTEST("tableware.book_pile", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1320);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "support");
    auto pile = above(pos);
    ctx.cleanupBlock(pile);
    auto& player = ctx.player();
    player.setSneaking(true);

    hold(player, makeItem("minecraft:book"));
    ctx.require(use(ctx, pos, FacingID::Up), "sneak-place a book");
    auto const& b1 = blockAt(ctx, pile);
    ctx.require(is(b1, kBookPile), "standing book pile placed");
    int v = state(b1, "cm:book0");
    ctx.check(v >= 1 && v <= 6, "plain cover colour");

    hold(player, makeItem("minecraft:enchanted_book"));
    ctx.require(use(ctx, pile, FacingID::North), "add an enchanted book");
    ctx.check(state(blockAt(ctx, pile), "cm:book1") == 7, "enchanted cover");
    hold(player, makeItem("minecraft:written_book"));
    ctx.check(!use(ctx, pile, FacingID::North), "a written book does not fit a standing pile");

    // enchanting power: a table two blocks west of the pile with air in between
    auto table  = pile;
    table.x    -= 2;
    ctx.require(ctx.setBlock(table, "minecraft:enchanting_table"), "enchanting table");
    float power = book_pile::enchantPower(regionOf(ctx), cm::game::blockPos(table));
    float want  = cm::feature::tableware::config().bookPower + cm::feature::tableware::config().enchantedBookPower;
    ctx.check(std::abs(power - want) < 0.01f, "pile power " + std::to_string(power));

    player.setSneaking(false);
    hold(player, ItemStack::EMPTY_ITEM());
    int before = countItem(player, "minecraft:enchanted_book");
    ctx.require(use(ctx, pile, FacingID::North), "take the last book");
    ctx.check(state(blockAt(ctx, pile), "cm:book1") == 0, "one book left");
    ctx.check(countItem(player, "minecraft:enchanted_book") == before + 1, "enchanted book returned");
    hold(player, ItemStack::EMPTY_ITEM()); // the returned book may have landed in the hand
    ctx.require(use(ctx, pile, FacingID::North), "take the first book");
    ctx.check(blockAt(ctx, pile).isAir(), "pile gone with its last book");
    ctx.check(!cm::game::db::load(posKey("books", pile.dim, cm::game::blockPos(pile))), "no stale book data");

    // lying pile
    player.setSneaking(true);
    hold(player, makeItem("minecraft:writable_book"));
    ctx.require(use(ctx, pos, FacingID::Up), "sneak-place a book and quill");
    ctx.check(is(blockAt(ctx, pile), kBookPileLying) && state(blockAt(ctx, pile), "cm:book0") == 2, "lying pile");
    player.setSneaking(false);
    co_return;
});

CM_REGISTER_SELFTEST("tableware.pancake", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1330);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(kPancake)), "pancake placed");
    auto& player = ctx.player();

    hold(player, makeItem(kPancake, 4));
    ctx.require(use(ctx, pos), "stack a pancake");
    ctx.check(state(blockAt(ctx, pos), "cm:pancakes") == 1, "two pancakes");
    hold(player, makeItem("minecraft:honey_bottle"));
    ctx.require(use(ctx, pos), "add honey");
    ctx.check(state(blockAt(ctx, pos), "cm:topping") == 1, "honey topping");
    ctx.check(!use(ctx, pos), "no second topping");

    auto mode = player.getPlayerGameType();
    player.setPlayerGameType(GameType::Creative);
    hold(player, ItemStack::EMPTY_ITEM());
    ctx.require(use(ctx, pos), "eat one");
    ctx.check(state(blockAt(ctx, pos), "cm:pancakes") == 0, "one pancake left");
    ctx.require(use(ctx, pos), "eat the last one");
    ctx.check(blockAt(ctx, pos).isAir(), "plate empty");
    player.setPlayerGameType(mode);
    co_return;
});

CM_REGISTER_SELFTEST("tableware.planter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1340);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(kPlanter)), "planter placed");
    auto top = above(pos);
    ctx.cleanupBlock(top);
    auto& region = regionOf(ctx);
    for (auto id : {"minecraft:wheat", "minecraft:oak_sapling", "minecraft:poppy"}) {
        auto const* plant = make(id);
        ctx.require(plant != nullptr, std::string(id) + " exists");
        ctx.check(
            plant->getBlockType().mayPlace(region, cm::game::blockPos(top)),
            std::string(id) + " may be placed on a planter"
        );
    }
    {
        // which position mayPlaceOn receives: probe with vanilla farmland
        auto farm = ctx.testPos(1341);
        ctx.require(ctx.setBlock(farm, "minecraft:farmland"), "farmland");
        auto const& wheat = make("minecraft:wheat")->getBlockType();
        ctx.check(wheat.mayPlaceOn(region, cm::game::blockPos(farm)), "mayPlaceOn receives the support block");
        ctx.check(
            !wheat.mayPlaceOn(region, cm::game::blockPos(above(farm))),
            "mayPlaceOn is not given the plant position"
        );
    }
    ctx.require(ctx.setBlock(top, "minecraft:wheat"), "wheat planted");
    co_await ctx.ticks(5);
    ctx.check(cm::game::blockId(blockAt(ctx, top)) == "minecraft:wheat", "wheat survives on the planter");
    ctx.check(planter::supports(region, cm::game::blockPos(pos)), "planter supports plants");
});

CM_REGISTER_SELFTEST("tableware.urn", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1350);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(
        ctx.setBlock(
            pos,
            std::string(kUrn),
            {
                {"cm:treasure", 1}
    }
        ),
        "treasure urn placed"
    );
    ctx.check(state(blockAt(ctx, pos), "cm:treasure") == 1, "treasure state");
    auto& cfg                = cm::config::mut<TablewareConfig>();
    auto  keep               = cfg.urnEntitySpawnChance;
    cfg.urnEntitySpawnChance = 1.0f;
    int before               = urn::releases();
    cm::game::clearBlock(regionOf(ctx), cm::game::blockPos(pos));
    cfg.urnEntitySpawnChance = keep;
    ctx.check(urn::releases() == before + 1, "a broken treasure urn releases a mob");
    co_await ctx.ticks(2);
});

CM_REGISTER_SELFTEST("tableware.event_path", [](Context& ctx) -> ll::coro::CoroTask<> {
    // The real interaction path (PlayerInteractBlockEvent + deferred apply) with a simulated player.
    auto pos = ctx.testPos(1360);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(kGoblet)), "goblet placed");
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    sp.simulateSetItem(makeItem("minecraft:honey_bottle"), true, 0);
    co_await ctx.ticks(1);
    sp.simulateUseItemInSlotOnBlock(
        0,
        cm::game::blockPos(pos),
        ScriptModuleMinecraft::ScriptFacing::Up,
        Vec3{0.5f, 0.9f, 0.5f}
    );
    co_await ctx.ticks(3);
    ctx.check(state(blockAt(ctx, pos), "cm:fluid") == 2, "goblet filled through the interaction event");
});
