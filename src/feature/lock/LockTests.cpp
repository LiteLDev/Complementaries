// Selftests of the lock unit (dx 300..399).

#include "feature/lock/Lock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace {

using cm::selftest::Context;
namespace lk = cm::feature::lock;

ItemStack namedKey(std::string const& name) {
    std::string snbt = R"({"Name":"complementaries:key","Count":1b,"Damage":0s,"tag":{"display":{"Name":")" + name + R"("}}})";
    auto        tag  = CompoundTag::fromSnbt(snbt);
    return tag ? ItemStack::fromTag(*tag) : ItemStack{};
}

void clearInventory(Player& p) {
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack{});
    p.setSelectedItem(ItemStack{});
}

lk::Click clickAt(cm::core::GlobalPos const& pos, bool sneaking = false, float yaw = 0.0f, int face = 1) {
    lk::Click c;
    c.pos      = pos;
    c.sneaking = sneaking;
    c.yaw      = yaw;
    c.face     = face;
    return c;
}

Block const& blockAt(Context& ctx, cm::core::GlobalPos const& pos) {
    return ctx.player().getDimensionBlockSource().getBlock(cm::game::blockPos(pos));
}

std::string idAt(Context& ctx, cm::core::GlobalPos const& pos) { return blockAt(ctx, pos).getTypeName(); }

cm::core::GlobalPos up(cm::core::GlobalPos p, int dy = 1) {
    p.y += dy;
    return p;
}
cm::core::GlobalPos east(cm::core::GlobalPos p, int dx = 1) {
    p.x += dx;
    return p;
}

} // namespace

CM_REGISTER_SELFTEST("lock.key_password", [](Context& ctx) {
    auto plain = lk::makeItem(lk::kKeyId);
    ctx.require(!plain.isNull(), "the key item exists");
    ctx.check(lk::keyPassword(plain) == std::optional<std::string>("Key"), "an unnamed key's password is 'Key'");
    auto named = namedKey("vault");
    ctx.check(lk::keyPassword(named) == std::optional<std::string>("vault"), "a renamed key's password is its name");
    ctx.check(!lk::keyPassword(lk::makeItem("minecraft:stick")), "a stick is no key");
    ctx.check(lk::keyStatus(named, std::string("vault")) == lk::KeyStatus::Correct, "matching key");
    ctx.check(lk::keyStatus(named, std::string("other")) == lk::KeyStatus::Incorrect, "wrong key");
    ctx.check(lk::keyStatus(lk::makeItem("minecraft:stick"), std::string("vault")) == lk::KeyStatus::NoKey, "no key");
});

CM_REGISTER_SELFTEST("lock.key_inventory", [](Context& ctx) {
    auto& p = ctx.player();
    clearInventory(p);
    ctx.check(lk::inventoryKeyStatus(p, std::string("pw")) == lk::KeyStatus::NoKey, "empty inventory: no key");
    ctx.check(lk::inventoryKeyStatus(p, std::nullopt) == lk::KeyStatus::Correct, "no password: always correct");
    ctx.give(p, namedKey("other"), 20);
    ctx.check(lk::inventoryKeyStatus(p, std::string("pw")) == lk::KeyStatus::Incorrect, "only a wrong key");
    ctx.give(p, namedKey("pw"), 30);
    ctx.check(lk::inventoryKeyStatus(p, std::string("pw")) == lk::KeyStatus::Correct, "the right key anywhere in the inventory");
    clearInventory(p);
});

CM_REGISTER_SELFTEST("lock.lock_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(300);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(lk::kLockBlockId)), "lock block placed");
    ctx.require(ctx.setBlock(east(pos), "minecraft:redstone_lamp"), "lamp placed");
    lk::setPassword(pos, std::nullopt);
    auto& p = ctx.player();
    clearInventory(p);
    // a key assigns its password, without powering
    ctx.check(lk::interact(p, namedKey("gate"), clickAt(pos)), "the lock block consumes the click");
    ctx.check(lk::password(pos) == std::optional<std::string>("gate"), "the key's name became the password");
    co_await ctx.ticks(2);
    ctx.check(!lk::flag(blockAt(ctx, pos), lk::kPowered), "assigning a key does not power");
    // no key carried: locked
    lk::interact(p, ItemStack{}, clickAt(pos));
    co_await ctx.ticks(2);
    ctx.check(!lk::flag(blockAt(ctx, pos), lk::kPowered), "locked without the key");
    // the right key in the inventory opens it: a 20 tick pulse
    ctx.give(p, namedKey("gate"), 9);
    lk::interact(p, ItemStack{}, clickAt(pos));
    co_await ctx.ticks(4);
    ctx.check(lk::flag(blockAt(ctx, pos), lk::kPowered), "the right key powers the block");
    ctx.check(idAt(ctx, east(pos)) == "minecraft:lit_redstone_lamp", "the lamp next to it is lit");
    co_await ctx.ticks(24);
    ctx.check(!lk::flag(blockAt(ctx, pos), lk::kPowered), "unpowered after 20 ticks");
    // sneaking with the right key clears it
    lk::interact(p, namedKey("gate"), clickAt(pos, true));
    ctx.check(!lk::password(pos), "sneaking with the right key clears the password");
    clearInventory(p);
    lk::setPassword(pos, std::nullopt);
});

CM_REGISTER_SELFTEST("lock.netherite_door", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(305);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    std::string id(lk::kNetheriteDoorId);
    ctx.require(ctx.setBlock(pos, id, {{"complementaries:facing", 2}}), "lower half");
    ctx.require(ctx.setBlock(up(pos), id, {{"complementaries:facing", 2}, {"complementaries:upper", 1}}), "upper half");
    lk::setPassword(pos, std::nullopt);
    auto& p = ctx.player();
    clearInventory(p);
    // unlocked: opens by hand (clicking the upper half)
    ctx.check(lk::interact(p, ItemStack{}, clickAt(up(pos))), "the door consumes the click");
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, pos), lk::kOpen) && lk::flag(blockAt(ctx, up(pos)), lk::kOpen), "both halves open");
    // lock it with a key (stored on the lower half)
    lk::interact(p, namedKey("nk"), clickAt(up(pos)));
    ctx.check(lk::password(pos) == std::optional<std::string>("nk"), "password on the lower half");
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, pos), lk::kOpen), "assigning does not toggle");
    lk::interact(p, ItemStack{}, clickAt(pos));
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, pos), lk::kOpen), "locked: stays open without the key");
    ctx.give(p, namedKey("nk"), 5);
    lk::interact(p, ItemStack{}, clickAt(pos));
    co_await ctx.ticks(2);
    ctx.check(!lk::flag(blockAt(ctx, pos), lk::kOpen) && !lk::flag(blockAt(ctx, up(pos)), lk::kOpen), "the key closes it");
    // redstone is ignored
    ctx.require(ctx.setBlock(east(pos), "minecraft:redstone_block"), "redstone block");
    co_await ctx.ticks(6);
    ctx.check(!lk::flag(blockAt(ctx, pos), lk::kOpen), "redstone does not open a netherite door");
    // breaking the lower half removes the upper one
    cm::game::clearBlock(ctx.player().getDimensionBlockSource(), cm::game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, up(pos)) == "minecraft:air", "upper half removed with the lower");
    ctx.check(!lk::password(pos), "lock data erased");
    clearInventory(p);
});

CM_REGISTER_SELFTEST("lock.gold_door", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(310);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    std::string id(lk::kGoldDoorId);
    ctx.require(ctx.setBlock(pos, id, {{"complementaries:facing", 0}}), "lower half");
    ctx.require(ctx.setBlock(up(pos), id, {{"complementaries:facing", 0}, {"complementaries:upper", 1}}), "upper half");
    co_await ctx.ticks(2);
    ctx.check(lk::tracked(pos) && lk::tracked(up(pos)), "gold door halves are tracked for redstone");
    auto& p = ctx.player();
    ctx.check(lk::interact(p, ItemStack{}, clickAt(pos)), "opens by hand when unpowered");
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, pos), lk::kOpen), "open");
    // powering locks it in its state
    ctx.require(ctx.setBlock(east(up(pos)), "minecraft:redstone_block"), "redstone block by the upper half");
    co_await ctx.ticks(6);
    ctx.check(lk::flag(blockAt(ctx, pos), lk::kPowered) && lk::flag(blockAt(ctx, up(pos)), lk::kPowered), "both halves powered");
    ctx.check(lk::flag(blockAt(ctx, pos), lk::kOpen), "redstone does not close it");
    ctx.check(!lk::interact(p, ItemStack{}, clickAt(pos)), "a powered gold door passes the click on");
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, pos), lk::kOpen), "still open");
    cm::game::clearBlock(ctx.player().getDimensionBlockSource(), cm::game::blockPos(east(up(pos))));
    co_await ctx.ticks(6);
    ctx.check(!lk::flag(blockAt(ctx, pos), lk::kPowered), "unpowered again");
    lk::interact(p, ItemStack{}, clickAt(up(pos)));
    co_await ctx.ticks(2);
    ctx.check(!lk::flag(blockAt(ctx, pos), lk::kOpen), "closes by hand again");
    // breaking the upper half removes the lower one
    cm::game::clearBlock(ctx.player().getDimensionBlockSource(), cm::game::blockPos(up(pos)));
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, pos) == "minecraft:air", "lower half removed with the upper");
});

CM_REGISTER_SELFTEST("lock.trapdoors", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gold = ctx.testPos(315);
    auto neth = ctx.testPos(318);
    ctx.require(co_await ctx.ensureLoaded(gold), "test area loaded");
    ctx.require(ctx.setBlock(gold, std::string(lk::kGoldTrapdoorId)), "gold trapdoor");
    ctx.require(ctx.setBlock(neth, std::string(lk::kNetheriteTrapdoorId)), "netherite trapdoor");
    lk::setPassword(neth, std::nullopt);
    auto& p = ctx.player();
    clearInventory(p);
    lk::interact(p, ItemStack{}, clickAt(gold));
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, gold), lk::kOpen), "gold trapdoor opens by hand");
    ctx.require(ctx.setBlock(east(gold), "minecraft:redstone_block"), "redstone block");
    co_await ctx.ticks(6);
    ctx.check(lk::flag(blockAt(ctx, gold), lk::kPowered) && lk::flag(blockAt(ctx, gold), lk::kOpen), "powered, still open");
    ctx.check(!lk::interact(p, ItemStack{}, clickAt(gold)), "powered gold trapdoor cannot be used");
    // netherite: key locked
    lk::interact(p, namedKey("t"), clickAt(neth));
    ctx.check(lk::password(neth) == std::optional<std::string>("t"), "netherite trapdoor got the password");
    lk::interact(p, namedKey("wrong"), clickAt(neth));
    co_await ctx.ticks(2);
    ctx.check(!lk::flag(blockAt(ctx, neth), lk::kOpen), "a wrong key held, no right key carried: locked");
    ctx.give(p, namedKey("t"), 3);
    lk::interact(p, ItemStack{}, clickAt(neth));
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, neth), lk::kOpen), "the right key opens it");
    clearInventory(p);
    lk::setPassword(neth, std::nullopt);
});

CM_REGISTER_SELFTEST("lock.gates", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto iron = ctx.testPos(322);
    auto gold = ctx.testPos(326);
    ctx.require(co_await ctx.ensureLoaded(iron), "test area loaded");
    std::string ironId(lk::kIronGateId), goldId(lk::kGoldGateId);
    ctx.require(ctx.setBlock(iron, ironId, {{"complementaries:facing", 2}}), "iron gate");
    ctx.require(ctx.setBlock(up(iron), ironId, {{"complementaries:facing", 2}}), "iron gate on top");
    ctx.require(ctx.setBlock(gold, goldId, {{"complementaries:facing", 2}}), "gold gate");
    auto& p = ctx.player();
    // hand use opens both stacked gates, turned away from the player (yaw 0 = looking south)
    ctx.check(lk::interact(p, ItemStack{}, clickAt(iron, false, 0.0f)), "iron gates open by hand by default");
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, iron), lk::kOpen) && lk::flag(blockAt(ctx, up(iron)), lk::kOpen), "double opening");
    ctx.check(lk::state(blockAt(ctx, iron), lk::kFacing) == 0, "opened toward the player's look (south)");
    lk::interact(p, ItemStack{}, clickAt(up(iron), false, 0.0f));
    co_await ctx.ticks(2);
    ctx.check(!lk::flag(blockAt(ctx, iron), lk::kOpen) && !lk::flag(blockAt(ctx, up(iron)), lk::kOpen), "both close");
    // the lower gate is in a wall (a gate above it)
    co_await ctx.ticks(2);
    ctx.check(lk::flag(blockAt(ctx, iron), lk::kInWall), "a gate below another one is in_wall");
    // redstone opens iron gates and gold gates (door-like gates off)
    ctx.require(ctx.setBlock(east(gold), "minecraft:redstone_block"), "redstone block by the gold gate");
    co_await ctx.ticks(6);
    ctx.check(lk::flag(blockAt(ctx, gold), lk::kPowered) && lk::flag(blockAt(ctx, gold), lk::kOpen), "redstone opens a gold gate");
    cm::game::clearBlock(ctx.player().getDimensionBlockSource(), cm::game::blockPos(east(gold)));
    co_await ctx.ticks(6);
    ctx.check(!lk::flag(blockAt(ctx, gold), lk::kOpen), "and closes it");
    ctx.check(lk::gateOpenFacing(0.0f, 2) == 0 && lk::gateOpenFacing(180.0f, 0) == 2, "gates open away from the player");
    ctx.check(lk::gateOpenFacing(-90.0f, 1) == 3 && lk::gateOpenFacing(90.0f, 3) == 1, "east / west");
});

CM_REGISTER_SELFTEST("lock.placement_rules", [](Context& ctx) {
    lk::Click c;
    c.face = 2; // north face of a block -> facing north
    c.hitY = 0.7f;
    auto [f1, top1] = lk::trapdoorPlacement(c, 0);
    ctx.check(f1 == 2 && top1, "side click: facing the clicked face, upper half from the click height");
    c.face = 1; // top face
    auto [f2, top2] = lk::trapdoorPlacement(c, 0);
    ctx.check(f2 == 2 && !top2, "top click: facing the player, bottom half");
    c.face = 0;
    auto [f3, top3] = lk::trapdoorPlacement(c, 3);
    ctx.check(f3 == 1 && top3, "bottom click: top half");
});

CM_REGISTER_SELFTEST("lock.safe", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(330);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(lk::kSafeId)), "safe");
    auto& p = ctx.player();
    clearInventory(p);
    lk::placeSafe(p, pos, lk::makeItem(lk::kSafeId));
    auto data = lk::safeData(pos);
    ctx.check(data.owner.has_value() && data.ownerName == std::optional<std::string>(p.getRealName()), "the placer owns the safe");
    ctx.check(lk::safeItemCount(pos) == 0, "a storage with 27 empty slots exists");
    // key assignment
    ctx.check(lk::interact(p, namedKey("vault"), clickAt(pos)), "the safe consumes clicks");
    ctx.check(lk::safeData(pos).password == std::optional<std::string>("vault"), "the held key assigned its password");
    ctx.check(lk::safeLocked(lk::safeData(pos)), "locked");
    ctx.check(lk::safeTitle(lk::safeData(pos)) == "Safe key: vault", "title shows the key");
    ctx.check(!lk::safeCanOpen(p, pos, false), "cannot open without the key");
    ctx.give(p, namedKey("vault"), 7);
    ctx.check(lk::safeCanOpen(p, pos, false), "the key in the inventory opens it");
    // contents survive breaking: the item carries them
    ctx.check(lk::safeSetItem(pos, 3, lk::makeItem("minecraft:diamond", 5)), "item put in the storage");
    ctx.check(lk::safeItemCount(pos) == 1, "one stack stored");
    auto item = lk::safeItem(pos);
    auto back = lk::safeItemData(item);
    ctx.check(back.password == std::optional<std::string>("vault") && back.owner == data.owner, "item keeps owner and password");
    // placing the item again restores the contents
    auto pos2 = ctx.testPos(333);
    ctx.require(ctx.setBlock(pos2, std::string(lk::kSafeId)), "second safe");
    lk::placeSafe(p, pos2, item);
    ctx.check(lk::safeItemCount(pos2) == 1, "contents restored from the item");
    ctx.check(lk::safeData(pos2).password == std::optional<std::string>("vault"), "password restored");
    // breaking removes the storage
    cm::game::clearBlock(p.getDimensionBlockSource(), cm::game::blockPos(pos2));
    co_await ctx.ticks(3);
    ctx.check(lk::safeItemCount(pos2) == -1, "storage removed with the block");
    // sneaking with the right key clears the safe
    lk::interact(p, namedKey("vault"), clickAt(pos, true));
    ctx.check(!lk::safeData(pos).password && !lk::safeData(pos).owner, "sneak + right key clears password and owner");
    cm::game::clearBlock(p.getDimensionBlockSource(), cm::game::blockPos(pos));
    co_await ctx.ticks(3);
    clearInventory(p);
});

CM_REGISTER_SELFTEST("lock.place_door_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(340);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto  ground = up(pos, -1);
    auto& p      = ctx.player();
    clearInventory(p);
    int slot = ctx.give(p, lk::makeItem(lk::kGoldDoorId), 0);
    ctx.require(slot == 0, "door item given");
    ctx.cleanupBlock(pos);
    ctx.cleanupBlock(up(pos));
    auto* sp = static_cast<SimulatedPlayer*>(&p);
    bool  ok = sp->simulateUseItemInSlotOnBlock(0, cm::game::blockPos(ground), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    co_await ctx.ticks(4);
    ctx.info(std::string("use returned ") + (ok ? "true" : "false") + ", block " + idAt(ctx, pos));
    ctx.check(idAt(ctx, pos) == lk::kGoldDoorId, "the item placed the lower half");
    ctx.check(idAt(ctx, up(pos)) == lk::kGoldDoorId && lk::flag(blockAt(ctx, up(pos)), lk::kUpper), "the mod added the upper half");
    ctx.check(lk::state(blockAt(ctx, pos), lk::kFacing) == lk::state(blockAt(ctx, up(pos)), lk::kFacing), "halves share the facing");
    clearInventory(p);
});
