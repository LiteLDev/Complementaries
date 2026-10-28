// Selftests of the tool unit (U16, test area dx 1600..1699).

#include <cmath>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/actor/PaletteColor.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorColor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerItemInUse.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"

#include "feature/tool/Tool.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {

using cm::selftest::Context;
namespace tool = cm::feature::tool;
namespace game = cm::game;

BlockPos bp(cm::core::GlobalPos const& p, int dx = 0, int dy = 0, int dz = 0) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }
cm::core::GlobalPos gp(cm::core::GlobalPos const& p, int dx = 0, int dy = 0, int dz = 0) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

std::string stateString(Block const& b, std::string const& name) {
    for (auto const& [n, v] : tool::blockStates(b)) {
        if (n != name) continue;
        if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
        if (std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
        if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
    }
    return {};
}

/// Holds `item` in the selected slot.
void hold(Context& ctx, Player& p, ItemStack const& item) { ctx.give(p, item, p.getSelectedItemSlot()); }

Mob* spawnMob(Player& near, std::string const& id, Vec3 const& pos) {
    auto* l = tool::level();
    if (!l) return nullptr;
    return l->getSpawner().spawnMob(near.getDimensionBlockSource(), ActorDefinitionIdentifier(id), nullptr, pos, false, true, false);
}

int countItem(Player& p, std::string_view id) {
    int  n   = 0;
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (tool::isItem(inv.getItem(i), id)) n += inv.getItem(i).mCount;
    if (tool::isItem(p.getOffhandSlot(), id)) n += p.getOffhandSlot().mCount;
    return n;
}

} // namespace

CM_REGISTER_SELFTEST("tool.content", [](Context& ctx) {
    for (auto id : {tool::ids::Slingshot, tool::ids::BubbleBlower, tool::ids::Soap, tool::ids::Wrench, tool::ids::Flute,
                    tool::ids::ConfettiPopper, tool::ids::AntiqueInk, tool::ids::EmptySliceMap}) {
        ctx.check(!tool::makeItem(id).isNull(), std::string(id) + " is registered (behavior pack)");
    }
    ctx.check(game::blockWith(tool::ids::BubbleBlock) != nullptr, "bubble_block is registered");
    ctx.check(tool::config().version >= 1 && tool::config().slingshot.chargeTime == 20, "tool config defaults are loaded");
    ctx.check(tool::fmtCode("r7") == "\xc2\xa7r\xc2\xa7" "7", "format codes");
});

CM_REGISTER_SELFTEST("tool.slingshot_power", [](Context& ctx) {
    auto sling = tool::makeItem(tool::ids::Slingshot);
    ctx.check(tool::slingshot::chargeTicks(sling) == 20, "default charge time is 20 ticks");
    ctx.check(tool::slingshot::power(0, sling) == 0.0f, "no power without charging");
    ctx.check(std::abs(tool::slingshot::power(20, sling) - 1.0f) < 1e-4f, "full power at full charge");
    ctx.check(std::abs(tool::slingshot::power(80, sling) - 1.0f) < 1e-4f, "power is capped");
    ctx.check(tool::slingshot::power(10, sling) > 0.3f && tool::slingshot::power(10, sling) < 1.0f, "half charge gives partial power");
});

CM_REGISTER_SELFTEST("tool.slingshot_places_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1600);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p = ctx.player();
    auto  wall = gp(base, 0, 2, 0);
    ctx.require(ctx.setBlock(wall, "minecraft:gold_block"), "wall placed");
    for (int dy = -2; dy <= 2; ++dy)
        for (int dz = -1; dz <= 1; ++dz) ctx.cleanupBlock(gp(wall, -1, dy, dz));
    Vec3 from{static_cast<float>(wall.x) - 3.5f, static_cast<float>(wall.y) + 0.6f, static_cast<float>(wall.z) + 0.5f};
    int  before = tool::slingshot::activeProjectiles();
    ctx.require(tool::slingshot::launch(p, tool::makeItem("minecraft:cobblestone"), from, Vec3{0.9f, 0.1f, 0.0f}, 0, 0), "projectile launched");
    ctx.check(tool::slingshot::activeProjectiles() == before + 1, "projectile is tracked");
    auto& region = p.getDimensionBlockSource();
    bool  placed = false;
    for (int t = 0; t < 60 && !placed; ++t) {
        co_await ctx.ticks(1);
        for (int dy = -2; dy <= 2 && !placed; ++dy)
            for (int dz = -1; dz <= 1 && !placed; ++dz) placed = game::blockIdAt(region, bp(wall, -1, dy, dz)) == "minecraft:cobblestone";
    }
    ctx.check(placed, "the cobblestone was placed against the wall");
    ctx.check(tool::slingshot::activeProjectiles() == before, "projectile finished");
});

CM_REGISTER_SELFTEST("tool.slingshot_use", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1605);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p  = ctx.player();
    auto& sp = static_cast<SimulatedPlayer&>(p);
    hold(ctx, p, tool::makeItem(tool::ids::Slingshot));
    int slot = p.getSelectedItemSlot();
    ctx.give(p, tool::makeItem("minecraft:snowball", 4), (slot + 1) % 9);
    sp.simulateLookAt(p.getPosition() + Vec3{5.0f, 1.6f, 0.0f}, sim::LookDuration::Instant);
    co_await ctx.ticks(1);
    ctx.require(sp.simulateUseItemInSlot(slot), "started using the slingshot");
    co_await ctx.ticks(2);
    ctx.check(tool::isItem(*p.mItemInUse->mItem, tool::ids::Slingshot), "the slingshot is charging (use_modifiers)");
    co_await ctx.ticks(25);
    int before = tool::slingshot::activeProjectiles();
    p.releaseUsingItem();
    co_await ctx.ticks(2);
    ctx.check(countItem(p, "minecraft:snowball") == 3, "one snowball was used as ammo");
    ctx.check(tool::slingshot::activeProjectiles() >= before, "a projectile is flying");
    ctx.check(p.getSelectedItem().getDamageValue() == 1, "the slingshot lost one durability");
});

CM_REGISTER_SELFTEST("tool.slingshot_loyalty", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1610);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p    = ctx.player();
    auto  wall = gp(base, 0, 2, 0);
    ctx.require(ctx.setBlock(wall, "minecraft:gold_block"), "wall placed");
    p.teleport(Vec3{static_cast<float>(wall.x) - 4.5f, static_cast<float>(base.y) + 1.0f, static_cast<float>(wall.z) + 0.5f}, p.getDimensionId());
    co_await ctx.ticks(2);
    Vec3 from{static_cast<float>(wall.x) - 3.0f, static_cast<float>(wall.y) + 0.6f, static_cast<float>(wall.z) + 0.5f};
    ctx.require(tool::slingshot::launch(p, tool::makeItem("minecraft:bone"), from, Vec3{0.8f, 0.1f, 0.0f}, 2, 0), "projectile launched");
    bool back = false;
    for (int t = 0; t < 200 && !back; ++t) {
        co_await ctx.ticks(1);
        back = countItem(p, "minecraft:bone") == 1;
    }
    ctx.check(back, "the loyal projectile came back to the inventory");
    ctx.check(game::blockIdAt(p.getDimensionBlockSource(), bp(wall, -1)) != "minecraft:bone", "nothing was placed");
});

CM_REGISTER_SELFTEST("tool.bubble_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1615);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    auto  a = gp(base, 0, 3, 0), b = gp(base, 2, 3, 0);
    ctx.cleanupBlock(a);
    ctx.cleanupBlock(b);
    ctx.require(tool::bubble::placeBubble(a.dim, bp(a), 4), "bubble placed");
    ctx.check(game::blockIdAt(region, bp(a)) == tool::ids::BubbleBlock, "bubble block is in the world");
    ctx.check(tool::bubble::isTracked(a.dim, bp(a)), "bubble is tracked");
    co_await ctx.ticks(8);
    ctx.check(game::blockIdAt(region, bp(a)) == "minecraft:air", "bubble popped after its lifetime");
    ctx.check(!tool::bubble::isTracked(a.dim, bp(a)), "popped bubble is forgotten");
    // standing on a bubble pops it shortly after
    ctx.require(tool::bubble::placeBubble(b.dim, bp(b), 20 * 60), "second bubble placed");
    auto& p = ctx.player();
    p.teleport(Vec3{static_cast<float>(b.x) + 0.5f, static_cast<float>(b.y) + 1.0f, static_cast<float>(b.z) + 0.5f}, p.getDimensionId());
    bool popped = false;
    for (int t = 0; t < 40 && !popped; ++t) {
        co_await ctx.ticks(1);
        popped = game::blockIdAt(region, bp(b)) == "minecraft:air";
    }
    ctx.check(popped, "a bubble stood on pops");
});

CM_REGISTER_SELFTEST("tool.bubble_blower", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1620);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p  = ctx.player();
    auto& sp = static_cast<SimulatedPlayer&>(p);
    auto  ground = gp(base, 0, 1, 0);
    ctx.require(ctx.setBlock(ground, "minecraft:stone"), "ground placed");
    ctx.cleanupBlock(gp(ground, 0, 1, 0));
    hold(ctx, p, tool::makeItem(tool::ids::BubbleBlower));
    ctx.require(tool::bubble::useOnBlock(p, bp(ground), 1), "used on a block");
    auto& region = p.getDimensionBlockSource();
    ctx.check(game::blockIdAt(region, bp(ground, 0, 1)) == tool::ids::BubbleBlock, "bubble block placed on the clicked face");
    ctx.check(p.getSelectedItem().getDamageValue() == tool::config().bubbleBlower.bubbleBlockCost, "placing costs charges");
    tool::bubble::pop(ground.dim, bp(ground, 0, 1));
    // blowing uses charges while the use button is held
    int before = p.getSelectedItem().getDamageValue();
    ctx.require(sp.simulateUseItemInSlot(p.getSelectedItemSlot()), "started blowing");
    co_await ctx.ticks(30);
    ctx.check(p.getSelectedItem().getDamageValue() > before, "blowing uses charges");
    p.stopUsingItem();
    // refill with soap in the other hand
    p.setOffhandSlot(tool::makeItem(tool::ids::Soap, 2));
    ctx.check(tool::bubble::tryRefill(p), "refilled with soap");
    ctx.check(p.getSelectedItem().getDamageValue() == 0, "fully charged again");
    ctx.check(p.getOffhandSlot().mCount == 1, "one soap used");
});

CM_REGISTER_SELFTEST("tool.soap_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1625);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    struct Case {
        char const* from;
        char const* to;
    };
    Case cases[] = {
        {"minecraft:red_wool", "minecraft:white_wool"},
        {"minecraft:light_blue_stained_glass", "minecraft:glass"},
        {"minecraft:lime_terracotta", "minecraft:hardened_clay"},
        {"minecraft:cyan_carpet", "minecraft:white_carpet"},
    };
    int dx = 0;
    for (auto const& c : cases) {
        auto pos = gp(base, dx++, 1, 0);
        ctx.require(ctx.setBlock(pos, c.from), std::string("placed ") + c.from);
        ctx.check(tool::soap::washBlock(region, bp(pos)), std::string("washed ") + c.from);
        ctx.check(game::blockIdAt(region, bp(pos)) == c.to, std::string(c.from) + " -> " + c.to);
    }
    auto white = gp(base, dx++, 1, 0);
    ctx.require(ctx.setBlock(white, "minecraft:white_wool"), "placed white wool");
    ctx.check(!tool::soap::washBlock(region, bp(white)), "white wool is already clean");
    auto piston = gp(base, dx++, 1, 0);
    ctx.require(ctx.setBlock(piston, "minecraft:sticky_piston", {{"facing_direction", 2}}), "placed a sticky piston");
    ctx.check(tool::soap::washBlock(region, bp(piston)), "washed the sticky piston");
    auto const& washed = region.getBlock(bp(piston));
    ctx.check(washed.getTypeName() == "minecraft:piston", "sticky piston -> piston");
    ctx.check(stateString(washed, "facing_direction") == "2", "the piston keeps its facing");
});

CM_REGISTER_SELFTEST("tool.soap_items_and_sheep", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto helmet = tool::makeItem("minecraft:leather_helmet");
    tool::setIntData(helmet, "customColor", static_cast<int>(0xFFAA2020));
    ctx.check(tool::soap::washItem(helmet), "dyed leather is washable");
    ctx.check(!helmet.mUserData || !helmet.mUserData->contains("customColor"), "leather dye removed");
    auto wool = tool::makeItem("minecraft:red_wool", 5);
    ctx.check(tool::soap::washItem(wool) && tool::isItem(wool, "minecraft:white_wool") && wool.mCount == 5, "a stack of red wool -> white wool");
    auto box = tool::makeItem("minecraft:red_shulker_box");
    tool::setIntData(box, "cm_probe", 7);
    ctx.check(tool::soap::washItem(box) && tool::isItem(box, "minecraft:undyed_shulker_box"), "shulker box undyed");
    ctx.check(tool::intData(box, "cm_probe") == 7, "the shulker box keeps its data");
    auto stone = tool::makeItem("minecraft:stone");
    ctx.check(!tool::soap::washItem(stone), "stone is not washable");

    auto base = ctx.testPos(1630);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p     = ctx.player();
    auto* sheep = spawnMob(p, "minecraft:sheep", Vec3{static_cast<float>(base.x) + 0.5f, static_cast<float>(base.y) + 1.0f, static_cast<float>(base.z) + 0.5f});
    ctx.require(sheep != nullptr, "sheep spawned");
    ActorColor::setColor(*sheep->mEntityData->mData, SharedTypes::Legacy::PaletteColor::Red);
    hold(ctx, p, tool::makeItem(tool::ids::Soap, 3));
    ctx.check(tool::soap::useOnEntity(p, *sheep), "soap used on the sheep");
    ctx.check(sheep->mEntityData->getInt8(static_cast<ushort>(ActorDataIDs::ColorIndex)) == 0, "the sheep is white");
    ctx.check(p.getSelectedItem().mCount == 2, "one soap used");
    ctx.check(!tool::soap::useOnEntity(p, *sheep), "a white sheep is already clean");
    sheep->remove();
});

CM_REGISTER_SELFTEST("tool.wrench_states", [](Context& ctx) {
    auto check = [&](char const* id, tool::States const& states, int face, bool ccw, std::string const& state, std::string const& expected) {
        auto const* b = tool::blockWithStates(id, states);
        if (!b) return ctx.check(false, std::string("block ") + id);
        auto const* r = tool::wrench::rotated(*b, face, ccw);
        if (!r) return ctx.check(false, std::string("rotated ") + id);
        ctx.check(stateString(*r, state) == expected, std::string(id) + " " + state + " -> " + expected + " (got " + stateString(*r, state) + ")");
    };
    check("minecraft:furnace", {{"minecraft:cardinal_direction", std::string("north")}}, 1, false, "minecraft:cardinal_direction", "east");
    check("minecraft:furnace", {{"minecraft:cardinal_direction", std::string("north")}}, 1, true, "minecraft:cardinal_direction", "west");
    check("minecraft:oak_log", {{"pillar_axis", std::string("y")}}, 2, false, "pillar_axis", "x");
    check("minecraft:oak_log", {{"pillar_axis", std::string("y")}}, 4, false, "pillar_axis", "z");
    check("minecraft:oak_stairs", {{"weirdo_direction", 0}}, 1, false, "weirdo_direction", "2"); // east -> south
    check("minecraft:standing_sign", {{"ground_sign_direction", 0}}, 1, false, "ground_sign_direction", "4");
    check("minecraft:dispenser", {{"facing_direction", 2}}, 5, false, "facing_direction", "0"); // north, clockwise seen from the east -> down
    auto const* log = tool::blockWithStates("minecraft:oak_log", {{"pillar_axis", std::string("y")}});
    ctx.check(log && tool::wrench::rotated(*log, 1, false) == nullptr, "a log clicked on its end does not turn");
    auto const* bed = tool::blockWithStates("minecraft:bed", {});
    ctx.check(!bed || tool::wrench::rotated(*bed, 1, false) == nullptr, "beds are not rotated");
});

CM_REGISTER_SELFTEST("tool.wrench_use", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1640);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p   = ctx.player();
    auto& sp  = static_cast<SimulatedPlayer&>(p);
    auto  pos = gp(base, 0, 1, 0);
    ctx.require(ctx.setBlock(pos, "minecraft:furnace", {}), "furnace placed");
    auto& region = p.getDimensionBlockSource();
    auto  before = stateString(region.getBlock(bp(pos)), "minecraft:cardinal_direction");
    hold(ctx, p, tool::makeItem(tool::ids::Wrench));
    p.teleport(Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 1.0f, static_cast<float>(pos.z) - 1.5f}, p.getDimensionId());
    co_await ctx.ticks(2);
    // through the real interaction path (PlayerInteractBlockEvent, deferred to the next tick)
    sp.simulateUseItemInSlotOnBlock(p.getSelectedItemSlot(), bp(pos), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    co_await ctx.ticks(3);
    auto after = stateString(region.getBlock(bp(pos)), "minecraft:cardinal_direction");
    ctx.check(game::blockIdAt(region, bp(pos)) == "minecraft:furnace", "still a furnace");
    ctx.check(!before.empty() && after != before, "the furnace turned (" + before + " -> " + after + ")");
    ctx.check(p.getSelectedItem().getDamageValue() == 1, "the wrench lost one durability");
});

CM_REGISTER_SELFTEST("tool.flute", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1645);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p = ctx.player();
    Vec3  at{static_cast<float>(base.x) + 0.5f, static_cast<float>(base.y) + 1.0f, static_cast<float>(base.z) + 0.5f};
    auto* wolf = spawnMob(p, "minecraft:wolf", at + Vec3{20.0f, 0, 0});
    ctx.require(wolf != nullptr, "wolf spawned");
    wolf->setOwner(p.getOrCreateUniqueID());
    SynchedActorDataAccess::setActorFlag(wolf->getEntityContext(), ActorFlags::Tamed, true);
    ctx.require(wolf->isTame(), "the wolf is tame");
    auto* parrot = spawnMob(p, "minecraft:parrot", at + Vec3{3.0f, 0, 3.0f});
    p.teleport(at, p.getDimensionId());
    hold(ctx, p, tool::makeItem(tool::ids::Flute));
    ctx.check(tool::flute::useOnEntity(p, *wolf), "flute used on the wolf");
    ctx.check(!tool::stringData(p.getSelectedItem(), "cm_flute_pet").empty(), "the flute is bound");
    ctx.check(!p.getSelectedItem().getCustomLore().empty(), "the bound pet is in the lore");
    co_await ctx.ticks(1);
    ctx.check(tool::flute::useInAir(p), "flute played");
    co_await ctx.ticks(2);
    ctx.check(wolf->getPosition().distanceTo(p.getPosition()) < 4.0f, "the bound wolf came to the player");
    ctx.check(p.getSelectedItem().getDamageValue() == 1, "the flute lost one durability");
    // unbound flute: parrots dance
    if (parrot) {
        auto unbound = tool::makeItem(tool::ids::Flute);
        tool::flute::callPets(p, unbound);
        ctx.check(parrot->getStatusFlag(ActorFlags::Dancing), "the parrot dances");
        parrot->remove();
    }
    wolf->remove();
});

CM_REGISTER_SELFTEST("tool.confetti_and_ink", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1650);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p = ctx.player();
    hold(ctx, p, tool::makeItem(tool::ids::ConfettiPopper, 2));
    ctx.check(tool::confetti::useInAir(p), "popper used");
    ctx.check(p.getSelectedItem().mCount == 1, "the popper is consumed");

    // antique ink on a sign, then soap
    auto sign = gp(base, 0, 1, 0);
    ctx.require(ctx.setBlock(sign, "minecraft:standing_sign"), "sign placed");
    co_await ctx.ticks(1);
    auto& region = p.getDimensionBlockSource();
    hold(ctx, p, tool::makeItem(tool::ids::AntiqueInk, 2));
    ctx.check(tool::antique::useOnBlock(p, bp(sign)), "antique ink used on the sign");
    ctx.check(tool::antique::isAntiqueSign(sign.dim, bp(sign)), "the sign is antique");
    auto color = tool::antique::signColor(region, bp(sign));
    ctx.check(color && *color != 0xFF000000u, "the sign text is sepia");
    ctx.check(p.getSelectedItem().mCount == 1, "one ink used");
    ctx.check(tool::soap::washBlock(region, bp(sign)), "soap washes the sign");
    ctx.check(!tool::antique::isAntiqueSign(sign.dim, bp(sign)), "the sign is no longer antique");
    color = tool::antique::signColor(region, bp(sign));
    ctx.check(color && *color == 0xFF000000u, "the sign text is black again");

    // antique ink on a book in the other hand
    p.setOffhandSlot(tool::makeItem("minecraft:written_book"));
    ctx.check(tool::antique::useInAir(p), "antique ink used on the book");
    ctx.check(tool::antique::isAntiqueItem(p.getOffhandSlot()), "the book is antique");
    ItemStack book = p.getOffhandSlot();
    ctx.check(tool::soap::washItem(book) && !tool::antique::isAntiqueItem(book), "soap removes the antique flag");
    p.setOffhandSlot(ItemStack::EMPTY_ITEM());
});

CM_REGISTER_SELFTEST("tool.slice_map", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1655);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p     = ctx.player();
    auto  gold  = gp(base, 0, 1, 0);
    auto  roof  = gp(base, 0, 6, 2);
    ctx.require(ctx.setBlock(gold, "minecraft:gold_block"), "gold placed");
    ctx.require(ctx.setBlock(roof, "minecraft:obsidian"), "roof placed");
    int  y   = base.y + 4; // below the roof, above the gold
    auto map = tool::slicemap::create(base.dim, bp(base), y);
    ctx.require(!map.isNull(), "slice map created");
    ctx.check(tool::slicemap::sliceHeight(map) == y, "the map knows its height");
    auto id = tool::slicemap::mapIdOf(map);
    ctx.require(id != 0, "the map has an id");
    int  x0 = static_cast<int>(std::floor((base.x + 64.0) / 128)) * 128 - 64;
    int  z0 = static_cast<int>(std::floor((base.z + 64.0) / 128)) * 128 - 64;
    auto goldPx   = tool::slicemap::pixel(id, gold.x - x0, gold.z - z0);
    auto groundPx = tool::slicemap::pixel(id, gold.x - x0 + 1, gold.z - z0);
    auto roofPx   = tool::slicemap::pixel(id, roof.x - x0, roof.z - z0);
    ctx.check(goldPx && groundPx && *goldPx != *groundPx, "the gold block shows on the slice");
    ctx.check(roofPx && groundPx && *roofPx == *groundPx, "the roof above the slice is cut away");
    // the player's empty slice map becomes a filled one
    hold(ctx, p, tool::makeItem(tool::ids::EmptySliceMap));
    ctx.check(tool::slicemap::useInAir(p), "empty slice map used");
    ctx.check(tool::slicemap::sliceHeight(p.getSelectedItem()).has_value(), "the player holds a slice map");
});

CM_REGISTER_SELFTEST("tool.stasis", [](Context& ctx) {
    auto sling = tool::makeItem(tool::ids::Slingshot);
    tool::setStasisLevel(sling, 1);
    ctx.check(tool::stasisLevel(sling) == 1, "stasis stored");
    ctx.check(!sling.getCustomLore().empty(), "stasis shows in the lore");
    tool::setStasisLevel(sling, 0);
    ctx.check(tool::stasisLevel(sling) == 0 && sling.getCustomLore().empty(), "stasis removed");
});
