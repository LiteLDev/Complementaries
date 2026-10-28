// Selftests of the display unit (U12: test positions dx 1200..1299).

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/display/Display.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace {

using cm::selftest::Context;
using namespace cm::feature::display;
namespace game = cm::game;

int stateOf(BlockSource& region, cm::core::GlobalPos const& p, char const* name) {
    return game::intState(region.getBlock(game::blockPos(p)), name).value_or(-1);
}

cm::core::GlobalPos up(cm::core::GlobalPos p, int dy = 1) {
    p.y += dy;
    return p;
}

} // namespace

CM_REGISTER_SELFTEST("display.classify", [](Context& ctx) {
    ctx.check(pedestalTypeOf(makeItem("minecraft:diamond_sword")) == PedestalType::Sword, "swords are swords");
    ctx.check(pedestalTypeOf(makeItem("minecraft:trident")) == PedestalType::Trident, "tridents stand upright");
    ctx.check(pedestalTypeOf(makeItem("minecraft:end_crystal")) == PedestalType::Crystal, "end crystals float");
    ctx.check(pedestalTypeOf(makeItem("minecraft:stone")) == PedestalType::Block, "stone is a block item");
    ctx.check(pedestalTypeOf(makeItem("minecraft:apple")) == PedestalType::Item, "apples are items");
    ctx.check(statuePoseOf(ItemStack()) == StatuePose::Standing, "empty hand: standing");
    ctx.check(statuePoseOf(makeItem("minecraft:iron_pickaxe")) == StatuePose::Tool, "pickaxe: tool pose");
    ctx.check(statuePoseOf(makeItem("minecraft:red_candle")) == StatuePose::Candle, "candles: candle pose");
    ctx.check(statuePoseOf(makeItem("minecraft:golden_sword")) == StatuePose::Sword, "sword pose");
    ctx.check(statuePoseOf(makeItem("minecraft:apple")) == StatuePose::Holding, "anything else: holding");
    auto snbt = itemToSnbt(makeItem("minecraft:diamond", 1));
    auto back = itemFromSnbt(snbt);
    ctx.check(back && itemId(*back) == "minecraft:diamond", "items survive the SNBT round trip: " + snbt);
    ctx.check(canWearOnHatStand(makeItem("minecraft:iron_helmet")), "helmets go on hat stands");
    ctx.check(canWearOnHatStand(makeItem("minecraft:carved_pumpkin")), "carved pumpkins go on hat stands");
    ctx.check(!canWearOnHatStand(makeItem("minecraft:apple")), "apples don't");
});

CM_REGISTER_SELFTEST("display.pedestal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1200);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(p, std::string(kPedestal)), "pedestal placed (behavior pack loaded)");
    co_await ctx.ticks(2);
    auto& region = ctx.player().getDimensionBlockSource();
    auto  pos    = game::blockPos(p);
    ctx.check(stateOf(region, p, "complementaries:item_status") == 1, "a lone pedestal is empty (can hold an item)");
    ctx.require(putItem(region, pos, makeItem("minecraft:diamond", 5)), "an item goes on the pedestal");
    co_await ctx.ticks(2);
    auto shown = displayedItem(p);
    ctx.check(shown && itemId(*shown) == "minecraft:diamond" && shown->mCount == 1, "one diamond is displayed");
    ctx.check(stateOf(region, p, "complementaries:item_status") == 2, "status full");
    ctx.check(comparatorSignal(p) == 15, "comparator reads 15");
    auto* entity = displayEntityAt(region, pos);
    ctx.check(entity != nullptr, "a display entity renders the item");
    if (entity) ctx.check(itemId(entity->getCarriedItem()) == "minecraft:diamond", "the display entity holds the diamond");
    ctx.check(!putItem(region, pos, makeItem("minecraft:apple")), "a full pedestal takes nothing more");
    auto taken = takeItem(region, pos);
    ctx.check(taken && itemId(*taken) == "minecraft:diamond", "the diamond comes back");
    co_await ctx.ticks(2);
    ctx.check(!displayedItem(p), "empty again");
    ctx.check(comparatorSignal(p) == 0, "comparator reads 0");
    ctx.check(displayEntityAt(region, pos) == nullptr, "the display entity is gone");
    ctx.check(stateOf(region, p, "complementaries:item_status") == 1, "status back to empty");
});

CM_REGISTER_SELFTEST("display.pedestal_stack", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1203);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(p, std::string(kPedestal)), "lower pedestal");
    ctx.require(ctx.setBlock(up(p), std::string(kPedestal)), "upper pedestal");
    co_await ctx.ticks(3);
    auto& region = ctx.player().getDimensionBlockSource();
    ctx.check(stateOf(region, p, "complementaries:item_status") == 0, "the lower pedestal can't hold items");
    ctx.check(stateOf(region, p, "complementaries:up") == 1, "the lower pedestal connects up");
    ctx.check(stateOf(region, up(p), "complementaries:down") == 1, "the upper pedestal connects down");
    ctx.check(stateOf(region, up(p), "complementaries:item_status") == 1, "the upper pedestal is empty");
    ctx.check(!putItem(region, game::blockPos(p), makeItem("minecraft:apple")), "no item under another pedestal");
    ctx.check(putItem(region, game::blockPos(up(p)), makeItem("minecraft:apple")), "the top one takes an item");
    co_await ctx.ticks(2);
    ctx.check(stateOf(region, up(p), "complementaries:item_status") == 2, "top full");
    // breaking the top pedestal drops the apple and frees the lower one
    game::clearBlock(region, game::blockPos(up(p)));
    co_await ctx.ticks(3);
    ctx.check(!displayedItem(up(p)), "the broken pedestal forgot its item");
    ctx.check(stateOf(region, p, "complementaries:item_status") == 1, "the lower pedestal can hold items again");
    ctx.check(stateOf(region, p, "complementaries:up") == 0, "and no longer connects up");
});

CM_REGISTER_SELFTEST("display.player_use", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1206);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(p, std::string(kItemShelf)), "item shelf placed");
    co_await ctx.ticks(2);
    auto& player = ctx.player();
    auto& region = player.getDimensionBlockSource();
    int   slot   = player.getSelectedItemSlot();
    ctx.give(player, makeItem("minecraft:emerald", 3), slot);
    ctx.check(use(player, region, game::blockPos(p)) == UseResult::Placed, "using an emerald places it");
    ctx.check(player.getSelectedItem().mCount == 2, "one emerald was taken from the hand");
    ctx.check(comparatorSignal(p) == 15, "the shelf has an item");
    co_await ctx.ticks(2);
    ctx.check(displayEntityAt(region, game::blockPos(p)) != nullptr, "shelf item is rendered");
    ctx.check(use(player, region, game::blockPos(p)) == UseResult::Taken, "using again takes it back");
    int emeralds = 0;
    auto& inv    = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (itemId(inv.getItem(i)) == "minecraft:emerald") emeralds += inv.getItem(i).mCount;
    }
    ctx.check(emeralds == 3, "all three emeralds are back, got " + std::to_string(emeralds));
    inv.removeAllItems();
    player.sendInventory(false);
    ctx.check(use(player, region, game::blockPos(p)) == UseResult::Pass, "empty hand on an empty shelf does nothing");
});

CM_REGISTER_SELFTEST("display.interact_event", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1209);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(p, std::string(kPedestal)), "pedestal placed");
    co_await ctx.ticks(2);
    auto& sim  = ctx.spawnPlayer();
    int   slot = sim.getSelectedItemSlot();
    ctx.give(sim, makeItem("minecraft:gold_ingot", 2), slot);
    auto pos = game::blockPos(p);
    sim.simulateLookAt(Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 1.0f, static_cast<float>(pos.z) + 0.5f});
    bool used = sim.simulateUseItemInSlotOnBlock(slot, pos, ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    ctx.info(std::string("simulateUseItemInSlotOnBlock returned ") + (used ? "true" : "false"));
    co_await ctx.ticks(3);
    auto shown = displayedItem(p);
    ctx.check(shown && itemId(*shown) == "minecraft:gold_ingot", "a real interaction puts the ingot on the pedestal");
    auto const& above = sim.getDimensionBlockSource().getBlock(game::blockPos(up(p)));
    ctx.check(above.isAir(), "nothing was placed on top of the pedestal");
    takeItem(sim.getDimensionBlockSource(), pos);
});

CM_REGISTER_SELFTEST("display.shelf_support", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1212);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    // Default block_face is not a side: Java gives NORTH, so the support is to the south (z + 1).
    auto support = p;
    support.z += 1;
    ctx.require(ctx.setBlock(support, "minecraft:stone"), "support block");
    ctx.require(ctx.setBlock(p, std::string(kItemShelf)), "item shelf");
    co_await ctx.ticks(2);
    ctx.check(blockFacing(region.getBlock(game::blockPos(p))) == 2, "the shelf faces north");
    ctx.require(putItem(region, game::blockPos(p), makeItem("minecraft:book")), "book on the shelf");
    game::clearBlock(region, game::blockPos(support));
    co_await ctx.ticks(3);
    ctx.check(kindOf(region.getBlock(game::blockPos(p))) == Kind::None, "the shelf popped off without support");
    ctx.check(!displayedItem(p), "its book was dropped");
});

CM_REGISTER_SELFTEST("display.statue", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1215);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(p, std::string(kStatue)), "statue placed");
    co_await ctx.ticks(2);
    auto& region = ctx.player().getDimensionBlockSource();
    auto  pos    = game::blockPos(p);
    ctx.check(displayEntityAt(region, pos) != nullptr, "the statue figure is drawn");
    ctx.require(putItem(region, pos, makeItem("minecraft:candle")), "candle in the statue's hands");
    co_await ctx.ticks(2);
    ctx.check(stateOf(region, p, "complementaries:lit") == 1, "a candle lights the statue");
    auto taken = takeItem(region, pos);
    ctx.check(taken.has_value(), "the candle comes back");
    co_await ctx.ticks(2);
    ctx.check(stateOf(region, p, "complementaries:lit") == 0, "no candle, no light");
    ctx.check(displayEntityAt(region, pos) != nullptr, "the figure stays without an item");
    auto side = p;
    side.x += 1;
    ctx.require(ctx.setBlock(side, "minecraft:redstone_block"), "redstone block next to it");
    co_await ctx.ticks(10);
    ctx.check(stateOf(region, p, "complementaries:powered") == 1, "powered statues wave");
    game::clearBlock(region, game::blockPos(side));
    co_await ctx.ticks(10);
    ctx.check(stateOf(region, p, "complementaries:powered") == 0, "unpowered again");
    game::clearBlock(region, pos);
    co_await ctx.ticks(3);
    ctx.check(displayEntityAt(region, pos) == nullptr, "breaking the statue removes the figure");
});

CM_REGISTER_SELFTEST("display.comparator", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1218);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(p, std::string(kStatue)), "statue placed");
    co_await ctx.ticks(2);
    auto& region = ctx.player().getDimensionBlockSource();
    auto  pos    = game::blockPos(p);
    auto const& block = region.getBlock(pos);
    ctx.check(block.getBlockType().hasComparatorSignal(), "display blocks report a comparator signal");
    putItem(region, pos, makeItem("minecraft:stick"));
    ctx.check(region.getBlock(pos).getBlockType().getComparatorSignal(region, pos, region.getBlock(pos), 0) == 15, "15 with an item");
    takeItem(region, pos);
    ctx.check(region.getBlock(pos).getBlockType().getComparatorSignal(region, pos, region.getBlock(pos), 0) == 0, "0 when empty");
    ctx.check(!game::blockWith("minecraft:stone")->getBlockType().hasComparatorSignal(), "vanilla blocks are untouched");
});

CM_REGISTER_SELFTEST("display.enchanting", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1221);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto ped = p;
    ped.x += 2;
    ctx.require(ctx.setBlock(p, "minecraft:enchanting_table"), "enchanting table");
    ctx.require(ctx.setBlock(ped, std::string(kPedestal)), "pedestal two blocks away");
    co_await ctx.ticks(2);
    auto& region = ctx.player().getDimensionBlockSource();
    Vec3  at{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
    auto  before = EnchantUtils::getBookCasePositions(region, at).size();
    ctx.require(putItem(region, game::blockPos(ped), makeItem("minecraft:end_crystal")), "end crystal on the pedestal");
    auto after = EnchantUtils::getBookCasePositions(region, at).size();
    ctx.check(after == before + 3, "an end crystal counts as 3 bookshelves (" + std::to_string(before) + " -> " + std::to_string(after) + ")");
    takeItem(region, game::blockPos(ped));
});

CM_REGISTER_SELFTEST("display.hat_stand", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1224);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto  ground = up(p, -1);
    auto& player = ctx.player();
    auto& region = player.getDimensionBlockSource();
    ctx.require(ctx.setBlock(ground, "minecraft:stone"), "floor");
    ctx.setBlock(p, "minecraft:air");
    Actor* stand = placeHatStand(&player, region, game::blockPos(ground), 1, 0.0f);
    ctx.require(stand != nullptr, "hat stand placed on the floor");
    auto id = stand->getOrCreateUniqueID();
    ctx.check(placeHatStand(&player, region, game::blockPos(ground), 1, 0.0f) == nullptr, "no second stand in the same spot");
    ctx.check(placeHatStand(&player, region, game::blockPos(ground), 0, 0.0f) == nullptr, "never on the bottom face");
    co_await ctx.ticks(2);
    int slot = player.getSelectedItemSlot();
    ctx.give(player, makeItem("minecraft:iron_helmet"), slot);
    ctx.check(useHatStand(player, *stand), "helmet equipped");
    ctx.check(itemId(stand->getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == "minecraft:iron_helmet", "the stand wears the helmet");
    ctx.check(player.getSelectedItem().isNull(), "the hand is empty after the swap");
    ctx.give(player, makeItem("minecraft:apple"), slot);
    ctx.check(!useHatStand(player, *stand), "apples are refused");
    player.getInventory().removeAllItems();
    player.sendInventory(false);
    ctx.check(useHatStand(player, *stand), "empty hand takes the helmet");
    ctx.check(itemId(player.getSelectedItem()) == "minecraft:iron_helmet", "the helmet is back in the hand");
    ctx.check(stand->getArmor(SharedTypes::Legacy::ArmorSlot::Head).isNull(), "the stand is bare");
    hurtHatStand(*stand, &player, false, static_cast<int>(SharedTypes::Legacy::ActorDamageCause::EntityAttack));
    co_await ctx.ticks(10);
    auto* again = player.getLevel().fetchEntity(id, false);
    ctx.check(again != nullptr, "one punch only wobbles the stand");
    if (again) {
        // a late punch wobbles again, the next one inside the 5-tick window breaks it
        hurtHatStand(*again, &player, false, static_cast<int>(SharedTypes::Legacy::ActorDamageCause::EntityAttack));
        hurtHatStand(*again, &player, false, static_cast<int>(SharedTypes::Legacy::ActorDamageCause::EntityAttack));
    }
    co_await ctx.ticks(2);
    ctx.check(player.getLevel().fetchEntity(id, false) == nullptr, "two quick punches break it");
});
