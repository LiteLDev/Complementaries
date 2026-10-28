// Selftests of the storage unit (dx 200..299).

#include <algorithm>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"

#include "feature/storage/Features.h"
#include "feature/storage/View.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace {

using cm::selftest::Context;
using namespace cm::feature::storage;
namespace game = cm::game;

std::vector<Actor*> entitiesNear(Player& p, cm::core::GlobalPos const& pos, float r = 2.5f) {
    Vec3 c{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    return p.getDimensionBlockSource().getEntities(AABB{Vec3{c.x - r, c.y - r, c.z - r}, Vec3{c.x + r, c.y + r, c.z + r}});
}

/// The dropped item near `pos` matching `pred` (nullptr if none); `remove` despawns it.
ItemActor* droppedItem(Player& p, cm::core::GlobalPos const& pos, std::function<bool(ItemStack const&)> const& pred) {
    for (auto* a : entitiesNear(p, pos)) {
        if (!a || a->getTypeName() != "minecraft:item") continue;
        auto* item = static_cast<ItemActor*>(a);
        if (pred(*item->mItem)) return item;
    }
    return nullptr;
}

ContainerComponent* viewContainer(Player& p, cm::core::GlobalPos const& pos) {
    for (auto* a : entitiesNear(p, pos, 3.0f)) {
        if (!a || a->getTypeName() != kViewEntity) continue;
        auto c = a->getEntityContext().tryGetComponent<ContainerComponent>();
        if (c) return &*c;
    }
    return nullptr;
}

cm::core::GlobalPos playerPos(Player& p) {
    auto bp = game::blockPosOf(p.getPosition());
    return {static_cast<int>(p.getDimensionId()), bp.x, bp.y, bp.z};
}

int stateAt(Player& p, cm::core::GlobalPos const& pos, std::string_view state) {
    auto const& b = p.getDimensionBlockSource().getBlock(game::blockPos(pos));
    return game::intState(b, state).value_or(-1);
}

std::string idAt(Player& p, cm::core::GlobalPos const& pos) {
    return game::blockIdAt(p.getDimensionBlockSource(), game::blockPos(pos));
}

} // namespace

CM_REGISTER_SELFTEST("storage.ids", [](Context& ctx) {
    ctx.check(kindOf(std::string_view("complementaries:sack")) == Kind::Sack, "sack id");
    ctx.check(kindOf(std::string_view("complementaries:present_light_blue")) == Kind::Present, "dyed present id");
    ctx.check(kindOf(std::string_view("complementaries:trapped_present")) == Kind::TrappedPresent, "trapped present id");
    ctx.check(kindOf(std::string_view("complementaries:present_nope")) == Kind::None, "unknown colour");
    ctx.check(presentColor("complementaries:trapped_present_red").value_or("?") == "red", "colour of a trapped present");
    ctx.check(presentId(true, "black") == "complementaries:trapped_present_black", "present id from colour");
    ctx.check(presentId(false, "") == "complementaries:present", "undyed present id");
    for (auto id : {"complementaries:sack", "complementaries:present", "complementaries:present_red",
                    "complementaries:trapped_present_black"}) {
        ctx.check(game::blockWith(id) != nullptr, std::string("block registered: ") + id);
    }
    ctx.check(isArrow(makeStack("minecraft:arrow", 3, 18)), "tipped arrows are arrows");
    ctx.check(!isArrow(makeStack("minecraft:stick")), "a stick is no arrow");
    ctx.check(isFood(makeStack("minecraft:bread")), "bread is food");
    ctx.check(!isFood(makeStack("minecraft:stone")), "stone is no food");
    ctx.check(!isFood(makeStack(kLunchBasket)), "the basket is not food for itself");
    ctx.check(!isEmpty(makeStack(kQuiver)) && !isEmpty(makeStack(kLunchBasket)), "items registered");
});

CM_REGISTER_SELFTEST("storage.contents", [](Context& ctx) {
    auto uid = newUid();
    ctx.require(uid.size() == 32 && uid != newUid(), "uids are random");
    std::vector<ItemStack> items(6);
    items[0] = makeStack("minecraft:arrow", 12);
    items[3] = makeStack("minecraft:arrow", 2, 18);
    saveContents(uid, items);
    auto back = loadContents(uid, 6);
    ctx.check(back.size() == 6, "size kept");
    ctx.check(isArrow(back[0]) && countOf(back[0]) == 12, "slot 0 round-trips");
    ctx.check(isArrow(back[3]) && back[3].getAuxValue() == 18 && countOf(back[3]) == 2, "tipped arrow aux round-trips");
    ctx.check(isEmpty(back[1]), "empty slots stay empty");
    ctx.check(selectedSlot(back, 1) == 3, "selection skips empty slots");
    ctx.check(selectedSlot(back, 4) == 0, "selection wraps around");
    auto lore = contentLore(back, 3);
    ctx.check(lore.size() == 2 && lore[0].find("> ") != std::string::npos, "selected stack first in the lore");
    std::vector<ItemStack> many(9);
    for (auto& s : many) s = makeStack("minecraft:bread", 1);
    ctx.check(contentLore(many).size() == 6, "five lines then 'and N more'");
    auto shrunk = loadContents(uid, 2);
    ctx.check(shrunk.size() == 3 && countOf(shrunk[2]) == 2, "a smaller container keeps the extra stacks");
    saveContents(uid, std::vector<ItemStack>(6));
    ctx.check(contentsEmpty(loadContents(uid, 6)), "saving empty contents clears the record");
});

CM_REGISTER_SELFTEST("storage.sack_drops_contents", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(200);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    ctx.require(ctx.setBlock(pos, std::string(kSack)), "sack placed");
    auto rec = blockRecordOrNew(pos);
    std::vector<ItemStack> items(9);
    items[4] = makeStack("minecraft:diamond", 5);
    saveContents(rec.uid, items);
    game::clearBlock(p.getDimensionBlockSource(), game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(!loadBlockRecord(pos), "block record removed");
    auto* drop = droppedItem(p, pos, [&](ItemStack const& s) { return itemUid(s) == rec.uid; });
    ctx.require(drop != nullptr, "the sack dropped with its uid");
    ctx.check(itemId(*drop->mItem) == kSack, "the drop is a sack");
    int sacks = 0;
    for (auto* a : entitiesNear(p, pos)) {
        if (a && a->getTypeName() == "minecraft:item" && itemId(*static_cast<ItemActor*>(a)->mItem) == kSack) ++sacks;
    }
    ctx.check(sacks == 1, "exactly one sack dropped (" + std::to_string(sacks) + ")");
    ctx.check(!drop->mItem->getCustomLore().empty(), "the drop lists its contents");
    auto kept = loadContents(rec.uid, 9);
    ctx.check(countOf(kept[4]) == 5, "contents kept for the item");
    // place it again: the block takes the item's uid
    ItemStack item = *drop->mItem;
    drop->despawn();
    ctx.require(ctx.setBlock(pos, std::string(kSack)), "sack placed again");
    onBlockPlaced(pos, item);
    auto again = loadBlockRecord(pos);
    ctx.check(again && again->uid == rec.uid, "placing restores the contents");
    removeQuietly(pos);
    // an empty sack drops a plain, stackable sack
    ctx.require(ctx.setBlock(pos, std::string(kSack)), "empty sack placed");
    game::clearBlock(p.getDimensionBlockSource(), game::blockPos(pos));
    co_await ctx.ticks(3);
    auto* plain = droppedItem(p, pos, [](ItemStack const& s) { return itemId(s) == kSack && itemUid(s).empty(); });
    ctx.check(plain != nullptr, "empty sack drops a plain sack");
    if (plain) plain->despawn();
});

CM_REGISTER_SELFTEST("storage.sack_falls", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto ground = ctx.testPos(205);
    ctx.require(co_await ctx.ensureLoaded(ground), "test area loaded");
    auto& p   = ctx.player();
    auto  top = ground;
    top.y += 3;
    game::clearBlock(p.getDimensionBlockSource(), game::blockPos(ground)); // usually air already
    ctx.require(game::replaceable(p.getDimensionBlockSource(), game::blockPos(ground)), "ground cleared");
    ctx.require(ctx.setBlock(top, std::string(kSack)), "floating sack placed");
    auto rec = blockRecordOrNew(top);
    co_await ctx.ticks(2);
    auto landed = checkFall(top);
    ctx.check(landed == ground, "the sack falls to the ground (" + landed.key() + ")");
    ctx.check(idAt(p, ground) == kSack, "sack block at the ground");
    ctx.check(idAt(p, top) != kSack, "nothing left above");
    auto moved = loadBlockRecord(ground);
    ctx.check(moved && moved->uid == rec.uid, "the record moved with the sack");
    removeQuietly(ground);
});

CM_REGISTER_SELFTEST("storage.sack_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(210);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    ctx.require(ctx.setBlock(pos, std::string(kSack)), "sack placed");
    auto rec = blockRecordOrNew(pos);
    std::vector<ItemStack> items(9);
    items[0] = makeStack("minecraft:apple", 3);
    saveContents(rec.uid, items);
    ctx.require(openSack(p, pos), "the sack screen opens");
    co_await ctx.ticks(2);
    ctx.check(stateAt(p, pos, kStateOpen) == 1, "open state while viewed");
    ctx.require(view::viewed(rec.uid), "a view exists");
    auto* cc = viewContainer(p, pos);
    ctx.require(cc != nullptr, "the view entity holds a container");
    ctx.check(countOf(cc->mContainer->getItem(0)) == 3, "the view shows the contents");
    cc->mContainer->setItem(1, makeStack("minecraft:gold_ingot", 7));
    cc->mContainer->setItem(2, makeStack("minecraft:white_shulker_box", 1));
    co_await ctx.ticks(2);
    auto now = loadContents(rec.uid, 9);
    ctx.check(countOf(now[1]) == 7, "changes are written through");
    ctx.check(isEmpty(now[2]) && isEmpty(cc->mContainer->getItem(2)), "a shulker box is refused");
    int boxes = 0;
    forEachSlot(p, [&](int, ItemStack const& s) {
        if (itemId(s) == "minecraft:white_shulker_box") ++boxes;
        return false;
    });
    ctx.check(boxes == 1, "the refused item went back to the player");
    view::close(rec.uid);
    co_await ctx.ticks(2);
    ctx.check(!view::viewed(rec.uid), "the view is gone");
    ctx.check(stateAt(p, pos, kStateOpen) == 0, "closed state after the screen");
    ctx.check(viewContainer(p, pos) == nullptr, "the view entity is removed");
    removeQuietly(pos);
});

CM_REGISTER_SELFTEST("storage.overencumbered", [](Context& ctx) {
    auto& p = ctx.player();
    p.removeEffect(2);
    ctx.give(p, makeStack(kSack, 2), 3);
    overencumberedTick(p);
    ctx.check(p.getEffect(2u) == nullptr, "two sacks are fine");
    ctx.give(p, makeStack(kSack, 3), 4);
    ctx.check(sackCount(p) == 5, "sacks are counted per item");
    overencumberedTick(p);
    auto const* eff = p.getEffect(2u);
    ctx.require(eff != nullptr, "five sacks slow the player down");
    ctx.check(eff->mAmplifier == 1, "amplifier (5 - 1) / 2 - 1 = 1");
    p.removeEffect(2);
});

CM_REGISTER_SELFTEST("storage.present", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(215);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& giver = ctx.player();
    auto& taker = ctx.spawnPlayer();
    ctx.require(ctx.setBlock(pos, "complementaries:present"), "present placed");
    ctx.give(giver, makeStack("minecraft:diamond", 2), giver.getSelectedItemSlot());
    applyPresentForm(giver, pos, PresentAction::Put, taker.getRealName(), "Happy birthday", true);
    auto rec = loadBlockRecord(pos);
    ctx.require(rec.has_value(), "record written");
    ctx.check(rec->packed && stateAt(giver, pos, kStatePacked) == 1, "packed");
    ctx.check(rec->sender == giver.getRealName() && rec->recipient == taker.getRealName(), "sender and recipient");
    ctx.check(countOf(loadContents(rec->uid, 1)[0]) == 2, "the gift is inside");
    ctx.check(isEmpty(giver.getSelectedItem()), "the gift left the giver's hand");
    ctx.check(!canOpenPresent(giver, *rec), "only the recipient opens it");
    ctx.check(canOpenPresent(taker, *rec), "the recipient opens it");
    interactBlock(giver, pos);
    ctx.check(stateAt(giver, pos, kStatePacked) == 1, "still packed after a stranger's click");
    // the packed present travels as an item
    game::clearBlock(giver.getDimensionBlockSource(), game::blockPos(pos));
    co_await ctx.ticks(3);
    auto* drop = droppedItem(giver, pos, [&](ItemStack const& s) { return itemUid(s) == rec->uid; });
    ctx.require(drop != nullptr, "the packed present dropped");
    ItemStack item = *drop->mItem;
    drop->despawn();
    ctx.check(tagInt(item, "packed") == 1 && tagString(item, "recipient") == taker.getRealName(), "item keeps the data");
    ctx.check(!item.getCustomLore().empty(), "item shows To/From");
    ctx.require(ctx.setBlock(pos, "complementaries:present"), "placed again");
    onBlockPlaced(pos, item);
    ctx.check(stateAt(giver, pos, kStatePacked) == 1, "placed packed");
    interactBlock(taker, pos);
    ctx.check(stateAt(giver, pos, kStatePacked) == 0, "the recipient unpacked it");
    ctx.check(dyePresent(giver, pos, "red"), "dyeing works");
    ctx.check(idAt(giver, pos) == "complementaries:present_red", "now a red present");
    auto after = loadBlockRecord(pos);
    ctx.check(after && after->uid == rec->uid, "dyeing keeps the gift");
    removeQuietly(pos);
});

CM_REGISTER_SELFTEST("storage.trapped_present", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(220);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    ctx.require(ctx.setBlock(pos, "complementaries:trapped_present"), "trapped present placed");
    ctx.give(p, makeStack("minecraft:arrow", 4), p.getSelectedItemSlot());
    applyPresentForm(p, pos, PresentAction::Put, "ignored", "", true);
    auto rec = loadBlockRecord(pos);
    ctx.require(rec && rec->packed, "packed");
    ctx.check(rec->recipient.empty(), "trapped presents have no recipient");
    triggerTrappedPresent(pos);
    co_await ctx.ticks(2);
    ctx.check(stateAt(p, pos, kStatePacked) == 0, "opened");
    ctx.check(countOf(loadContents(rec->uid, 1)[0]) < 4, "the content was activated");
    bool shot = false;
    for (auto* a : entitiesNear(p, pos, 6.0f)) {
        if (a && (a->getTypeName() == "minecraft:arrow" || a->getTypeName() == "minecraft:item")) {
            shot = true;
            a->despawn();
        }
    }
    ctx.check(shot, "an arrow (or the item) came out");
    removeQuietly(pos);
});

CM_REGISTER_SELFTEST("storage.quiver", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await ctx.ticks(1);
    auto quiver = makeStack(kQuiver);
    auto uid    = ensureUid(quiver);
    std::vector<ItemStack> items(static_cast<size_t>(quiverSlots()));
    items[0] = makeStack("minecraft:arrow", 10);
    items[2] = makeStack("minecraft:arrow", 3, 18);
    saveContents(uid, items);
    ctx.give(p, quiver, 9);
    p.setOffhandSlot(ItemStack());
    ctx.give(p, makeStack("minecraft:bow"), p.getSelectedItemSlot());
    quiverTick(p);
    ctx.check(isArrow(p.getOffhandSlot()) && countOf(p.getOffhandSlot()) == 10, "selected arrows lent to the off-hand");
    ctx.check(isEmpty(loadContents(uid, quiverSlots())[0]), "lent arrows left the quiver");
    // shooting takes arrows from the lent stack
    ItemStack off = p.getOffhandSlot();
    off.remove(3);
    p.setOffhandSlot(off);
    cycleSelection(p, 9);
    ctx.check(tagInt(slotItem(p, 9), "sel", -1) == 2, "cycling selects the tipped arrows");
    ctx.check(countOf(loadContents(uid, quiverSlots())[0]) == 7, "unused arrows returned on cycling");
    quiverTick(p);
    ctx.check(countOf(p.getOffhandSlot()) == 3 && p.getOffhandSlot().getAuxValue() == 18, "the new selection is lent");
    p.setSelectedItem(makeStack("minecraft:stick"));
    quiverTick(p);
    ctx.check(isEmpty(p.getOffhandSlot()), "switching away returns the arrows");
    auto back = loadContents(uid, quiverSlots());
    ctx.check(countOf(back[0]) == 7 && countOf(back[2]) == 3, "the quiver holds everything again");
    auto lore = slotItem(p, 9).getCustomLore();
    ctx.check(lore.size() == 2, "the lore lists both stacks");
    // the screen
    ctx.require(openItemScreen(p, 9), "the quiver screen opens");
    co_await ctx.ticks(2);
    auto* cc = viewContainer(p, playerPos(p));
    ctx.require(cc != nullptr, "the view entity is next to the player");
    cc->mContainer->setItem(5, makeStack("minecraft:stick", 1));
    cc->mContainer->setItem(4, makeStack("minecraft:arrow", 5, 26));
    co_await ctx.ticks(2);
    auto now = loadContents(uid, quiverSlots());
    ctx.check(countOf(now[4]) == 5 && isEmpty(now[5]), "arrows accepted, sticks refused");
    view::close(uid);
    co_await ctx.ticks(1);
    auto sq = skeletonQuiver();
    ctx.check(!contentsEmpty(loadContents(itemUid(sq), quiverSlots())), "a skeleton's quiver has arrows");
    eraseContents(itemUid(sq));
    eraseContents(uid);
});

CM_REGISTER_SELFTEST("storage.lunch_basket", [](Context& ctx) {
    auto& p      = ctx.player();
    auto  basket = makeStack(kLunchBasket);
    auto  uid    = ensureUid(basket);
    std::vector<ItemStack> items(static_cast<size_t>(basketSlots()));
    items[1] = makeStack("minecraft:bread", 3);
    items[2] = makeStack("minecraft:mushroom_stew", 1);
    saveContents(uid, items);
    int slot = p.getSelectedItemSlot();
    ctx.give(p, basket, slot);
    ctx.check(basketCanEat(p, slot) == (p.isHungry() || p.isCreative()), "bread needs hunger");
    ItemStack held = slotItem(p, slot);
    ctx.require(basketEat(p, held), "eating from the basket");
    ctx.check(countOf(loadContents(uid, basketSlots())[1]) == 2, "one bread eaten");
    setTagInt(held, "sel", 2);
    ctx.require(basketEat(p, held), "eating the stew");
    ctx.check(isEmpty(loadContents(uid, basketSlots())[2]), "stew eaten");
    int bowls = 0;
    forEachSlot(p, [&](int, ItemStack const& s) {
        if (itemId(s) == "minecraft:bowl") ++bowls;
        return false;
    });
    ctx.check(bowls == 1, "the bowl went to the inventory");
    ctx.check(itemId(held) == kLunchBasket, "the basket stays");
    eraseContents(uid);
});
