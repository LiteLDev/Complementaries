// Selftests of U4 `sign` (dx 400..499).

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/sign/Logic.h"
#include "feature/sign/SignData.h"
#include "feature/sign/SignGame.h"
#include "feature/sign/Store.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
namespace sign  = cm::feature::sign;
namespace logic = cm::feature::sign::logic;

std::string keyOf(cm::core::GlobalPos const& p) { return sign::posKey(p.dim, p.x, p.y, p.z); }

Actor* displayOf(std::int64_t id) { return sign::findActor(id); }

bool near(float a, float b) { return std::abs(a - b) < 0.6f; }

ItemStack writtenBook(std::vector<std::string> const& pages) {
    auto    item = sign::makeItem("minecraft:written_book");
    ListTag list;
    for (auto const& p : pages) {
        CompoundTag page;
        page["text"]      = p;
        page["photoname"] = std::string{};
        list.push_back(page.copy());
    }
    CompoundTag tag;
    tag["pages"]  = std::move(list);
    tag["title"]  = std::string("Test");
    tag["author"] = std::string("cm");
    item.mUserData = std::make_unique<CompoundTag>(std::move(tag));
    return item;
}
} // namespace

CM_REGISTER_SELFTEST("sign.data", [](Context& ctx) {
    sign::Arm a;
    a.active  = true;
    a.wood    = 7;
    a.yaw     = 247.4f;
    a.left    = true;
    a.color   = 3;
    a.glowing = true;
    auto b    = sign::unpackArm(sign::packArm(a));
    ctx.check(b.active && b.left && b.wood == 7 && b.color == 3 && b.glowing, "arm flags survive packing");
    ctx.check(std::abs(b.yaw - 247.0f) < 0.01f, "arm yaw packs to whole degrees");
    ctx.check(sign::packArm(sign::Arm{}) == 0, "inactive arm packs to 0");
    auto d = sign::unpackDisplay(sign::packDisplay({sign::DisplayKind::Doormat, 3, 14, true}));
    ctx.check(d.kind == sign::DisplayKind::Doormat && d.facing == 3 && d.color == 14 && d.glowing, "display info packs");
    ctx.check(sign::textWidth("Hello") == 24, "text width of Hello is 24");
    ctx.check(sign::textWidth(sign::fitWidth(std::string(40, 'W'), 90)) <= 90, "fitWidth respects the limit");
    ctx.check(sign::stripFormatting("\xC2\xA7" "cRed\x01") == "Red", "formatting codes are stripped");
    auto lines = sign::wrapText("the quick brown fox jumps over the lazy dog again and again", 60);
    ctx.check(lines.size() >= 3, "long text wraps");
    for (auto const& l : lines) ctx.check(sign::textWidth(l) <= 60, "wrapped line fits: " + l);
    auto k = sign::parsePosKey(sign::posKey(1, -5, 70, 12));
    ctx.check(k && k->dim == 1 && k->x == -5 && k->y == 70 && k->z == 12, "position keys round trip");
    ctx.check(sign::woodOfSignPostItem("complementaries:sign_post_dark_oak") == 5, "sign post item wood");
    ctx.check(sign::woodOfBlock("minecraft:pale_oak_fence") == 11, "longest wood name matches");
    ctx.check(sign::dyeOfItem("minecraft:light_blue_dye") == 3, "dye ids");
    ctx.check(std::abs(sign::yawTowards(0, 0, -10, 0) - 90.0f) < 0.01f, "west is yaw 90");
    ctx.check(std::abs(sign::yawTowards(0, 0, 0, -10) - 180.0f) < 0.01f, "north is yaw 180");
});

CM_REGISTER_SELFTEST("sign.post_arms", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(400);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:oak_fence"), "fence placed");
    auto key = keyOf(pos);
    ctx.require(logic::addArm(key, 1, true, 90.0f, false), "first arm added");
    auto* post = sign::Store::get().post(key);
    ctx.require(post && post->up.active && post->up.wood == 1, "upper spruce arm stored");
    ctx.check(cm::game::db::load("cm:sign:post:" + key).has_value(), "post saved in the world DB");
    auto* actor = displayOf(post->entity);
    ctx.require(actor != nullptr, "display entity spawned");
    ctx.check(actor->getTypeName() == sign::kSignPostEntity, "display entity type: " + actor->getTypeName());
    ctx.check(actor->getVariant() == sign::packArm(post->up), "variant carries the upper arm");
    ctx.check(logic::addArm(key, 4, true, 0.0f, true), "second arm goes to the free half");
    post = sign::Store::get().post(key);
    ctx.check(post->down.active && post->down.wood == 4 && post->down.left, "lower acacia arm stored");
    ctx.check(!logic::addArm(key, 0, true, 0.0f, false), "a third arm is refused");
    ctx.check(logic::pickArm(key, 0.8) == true && logic::pickArm(key, 0.2) == false, "click height picks the arm");
    ctx.check(logic::setArmText(key, true, "\xC2\xA7" "cTo the village and beyond, far far away"), "text set");
    post = sign::Store::get().post(key);
    ctx.check(sign::textWidth(post->up.text) <= sign::kSignPostLineWidth, "text cut to 90 px");
    ctx.check(post->up.text.starts_with("To the village"), "formatting removed");
    actor = displayOf(post->entity);
    ctx.require(actor != nullptr, "display entity still there");
    ctx.check(actor->getNameTag().find("To the village") != std::string::npos, "name tag carries the text");
    ctx.check(actor->getMarkVariant() == sign::packArm(post->down), "mark variant carries the lower arm");
    ctx.check(logic::flipArm(key, true), "flip");
    post = sign::Store::get().post(key);
    ctx.check(post->up.left && near(post->up.yaw, 270.0f), "flip mirrors and turns by 180");
    ctx.check(logic::pointArm(key, true, pos.x + 100.5, pos.z + 0.5), "point east");
    ctx.check(near(sign::Store::get().post(key)->up.yaw, 270.0f), "east is yaw 270");
    ctx.check(logic::dyeArm(key, true, 14), "dye red");
    ctx.check(!logic::dyeArm(key, true, 14), "same dye again does nothing");
    ctx.check(!logic::dyeArm(key, false, 14), "an arm without text can't be dyed");
    ctx.check(logic::glowArm(key, true, true), "glow ink");
    ctx.check(logic::waxPost(key, true), "wax");
    ctx.check(!logic::setArmText(key, true, "x"), "a waxed post can't be edited");
    ctx.check(!logic::flipArm(key, true), "a waxed post can't be flipped");
    ctx.check(logic::waxPost(key, false), "unwax");
    co_await ctx.ticks(2);
    auto entity = sign::Store::get().post(key)->entity;
    // breaking the post drops the arms and removes the display
    auto& region = ctx.player().getDimensionBlockSource();
    cm::game::clearBlock(region, cm::game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(sign::Store::get().post(key) == nullptr, "post data removed with the fence");
    ctx.check(!cm::game::db::load("cm:sign:post:" + key).has_value(), "post data erased from the DB");
    ctx.check(displayOf(entity) == nullptr, "display entity removed");
});

CM_REGISTER_SELFTEST("sign.post_needs_post", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(405);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "stone placed");
    ctx.check(!logic::addArm(keyOf(pos), 0, true, 0, false), "no sign post on stone");
    ctx.require(ctx.setBlock(pos, "minecraft:cobblestone_wall"), "wall placed");
    ctx.check(logic::addArm(keyOf(pos), 0, false, 0, false), "sign post on a wall");
    sign::Store::get().removePost(keyOf(pos), false);
});

CM_REGISTER_SELFTEST("sign.post_interact", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(410);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:birch_fence"), "fence placed");
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    co_await ctx.ticks(2);
    auto item = sign::makeItem("complementaries:sign_post_cherry", 4);
    ctx.require(!item.isNull(), "sign post item exists (behavior pack loaded)");
    ctx.give(sp, item, 0);
    sp.simulateUseItemInSlotOnBlock(0, cm::game::blockPos(pos), ScriptModuleMinecraft::ScriptFacing::North, Vec3{0.5f, 0.75f, 0.0f});
    co_await ctx.ticks(4);
    auto* post = sign::Store::get().post(keyOf(pos));
    ctx.require(post && (post->up.active || post->down.active), "using the item on a fence adds an arm");
    ctx.check((post->up.active ? post->up.wood : post->down.wood) == 7, "cherry arm");
    sign::Store::get().removePost(keyOf(pos), false);
});

CM_REGISTER_SELFTEST("sign.notice_board", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(420);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(sign::kNoticeBoard), {{std::string(sign::kFacingState), 0}}), "notice board placed");
    auto key = keyOf(pos);
    auto book = writtenBook({"First page of the notice", "Second page", "Third"});
    ctx.check(sign::bookPages(book).size() == 3, "book pages read");
    ctx.require(logic::boardInsert(key, book), "book put on the board");
    ctx.check(!logic::boardInsert(key, book), "a full board refuses a second item");
    co_await ctx.ticks(2);
    auto& region = ctx.player().getDimensionBlockSource();
    auto  bpos   = cm::game::blockPos(pos);
    ctx.check(cm::game::intState(region.getBlock(bpos), sign::kHasBookState) == 1, "has_book state set");
    auto* board = sign::Store::get().board(key);
    ctx.require(board != nullptr, "board stored");
    auto* actor = displayOf(board->entity);
    ctx.require(actor != nullptr, "text display spawned");
    ctx.check(actor->getNameTag().find("First page") != std::string::npos, "page 1 shown");
    ctx.check(sign::unpackDisplay(actor->getVariant()).facing == 0, "display knows the facing");
    ctx.check(logic::boardSignal(key) == 1, "comparator value is the page number");
    ctx.check(logic::boardNextPage(key), "page turned");
    actor = displayOf(sign::Store::get().board(key)->entity);
    ctx.check(actor && actor->getNameTag().find("Second page") != std::string::npos, "page 2 shown");
    // redstone: a rising edge turns the page
    auto side = pos;
    side.x += 1;
    ctx.require(ctx.setBlock(side, "minecraft:redstone_block"), "redstone block placed");
    co_await ctx.ticks(4);
    ctx.check(sign::Store::get().board(key)->page == 2, "redstone turned to page 3");
    ctx.require(ctx.setBlock(side, "minecraft:air"), "redstone removed");
    co_await ctx.ticks(4);
    ctx.check(sign::Store::get().board(key)->page == 2, "falling edge does nothing");
    ctx.check(logic::boardDye(key, 11), "board text dyed");
    auto taken = logic::boardTake(key);
    ctx.check(sign::itemId(taken) == "minecraft:written_book", "book taken back");
    co_await ctx.ticks(2);
    ctx.check(cm::game::intState(region.getBlock(bpos), sign::kHasBookState) == 0, "has_book cleared");
    ctx.check(logic::boardSignal(key) == 0, "empty board outputs 0");
    ctx.check(!sign::isBoardItem("minecraft:stone") && sign::isBoardItem("minecraft:filled_map"), "accepted items");
    sign::Store::get().removeBoard(key, false);
});

CM_REGISTER_SELFTEST("sign.doormat", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(430);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(sign::kDoormat), {{std::string(sign::kFacingState), 1}}), "doormat placed");
    auto key = keyOf(pos);
    ctx.require(logic::setMatLines(key, {"Welcome", "", "Home"}), "doormat text set");
    auto* mat = sign::Store::get().mat(key);
    ctx.require(mat && mat->lines[0] == "Welcome" && mat->lines[2] == "Home", "lines stored");
    auto* actor = displayOf(mat->entity);
    ctx.require(actor != nullptr, "doormat text display spawned");
    ctx.check(actor->getNameTag().find("Welcome") != std::string::npos, "name tag has the text");
    ctx.check(sign::unpackDisplay(actor->getVariant()).kind == sign::DisplayKind::Doormat, "display kind doormat");
    ctx.check(logic::matDye(key, 1), "doormat dyed");
    ctx.check(logic::matWax(key, true), "doormat waxed");
    ctx.check(!logic::setMatLines(key, {"x", "", ""}), "a waxed doormat can't be edited");
    auto entity = sign::Store::get().mat(key)->entity;
    auto& region = ctx.player().getDimensionBlockSource();
    cm::game::clearBlock(region, cm::game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(sign::Store::get().mat(key) == nullptr, "doormat data removed with the block");
    ctx.check(displayOf(entity) == nullptr, "doormat display removed");
});

CM_REGISTER_SELFTEST("sign.way_sign", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(440);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.cleanupBlock(pos);
    logic::WayTarget village{"Village", pos.x - 200, pos.z};
    logic::WayTarget outpost{"Pillager Outpost", pos.x, pos.z + 300};
    ctx.require(logic::buildWaySign(pos.dim, pos.x, pos.y, pos.z, village, outpost), "way sign built");
    auto& region = ctx.player().getDimensionBlockSource();
    ctx.check(cm::game::blockIdAt(region, cm::game::blockPos(pos)) == "minecraft:spruce_fence", "fence placed");
    auto* post = sign::Store::get().post(keyOf(pos));
    ctx.require(post && post->up.active && post->down.active, "two arms");
    ctx.check(near(post->up.yaw, 90.0f), "first arm points west to the village");
    ctx.check(near(post->down.yaw, 0.0f), "second arm points south to the outpost");
    ctx.check(post->up.text.starts_with("Village"), "village text: " + post->up.text);
    ctx.check(post->up.text.find("200m") != std::string::npos, "distance text");
    sign::Store::get().removePost(keyOf(pos), false);
    co_return;
});
