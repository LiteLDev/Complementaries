// Selftests of U5 blackboard (dx 500..599).

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/blackboard/Blackboard.h"
#include "feature/blackboard/Editor.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
using namespace cm::feature::blackboard;
namespace game = cm::game;

ItemStack itemOf(std::string_view id, int count = 1) {
    ItemStack s;
    s.reinit(id, count, 0);
    return s;
}

/// A hit point (relative to the block) on the front face over pixel (x, y).
Vec3 hitFor(int front, int x, int y) {
    float fy = (15 - y + 0.5f) / 16.f;
    switch (front & 3) {
    case 0:
        return Vec3{(15 - x + 0.5f) / 16.f, fy, 3.f / 16.f};
    case 2:
        return Vec3{(x + 0.5f) / 16.f, fy, 13.f / 16.f};
    case 1:
        return Vec3{13.f / 16.f, fy, (15 - x + 0.5f) / 16.f};
    default:
        return Vec3{3.f / 16.f, fy, (x + 0.5f) / 16.f};
    }
}

void runCommand(std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return;
    ServerCommandOrigin origin("complementaries-selftest", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
}

ll::coro::CoroTask<BlockPos> placeBoard(Context& ctx, int dx, int dz = 0) {
    auto gp = ctx.testPos(dx, dz);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    ctx.require(ctx.setBlock(gp, std::string(kBlockId)), "blackboard block registered");
    co_return game::blockPos(gp);
}
} // namespace

CM_REGISTER_SELFTEST("blackboard.codec", [](Context& ctx) {
    Board b;
    b.set(3, 4, 1);
    b.set(15, 15, 14);
    auto hex = toHex(b.px);
    ctx.check(hex.size() == 256 && hex[4 * 16 + 3] == '1' && hex[255] == 'e', "hex layout is row-major");
    auto back = parseHex(hex);
    ctx.check(back && *back == b.px, "hex round trip");
    ctx.check(!parseHex("12"), "short hex is rejected");
    ctx.check(byteOfDye(15) == 0 && byteOfDye(0) == 1 && byteOfDye(1) == 15 && byteOfDye(14) == 14, "Java colour bytes");
    for (int d = 0; d < 16; ++d) ctx.check(dyeOfByte(byteOfDye(d)) == d || d == 15, "dye <-> byte");
    ctx.check(chalkOf("minecraft:white_dye", false) == std::uint8_t{1}, "white dye draws white");
    ctx.check(chalkOf("minecraft:coal", false) == std::uint8_t{0}, "coal erases");
    ctx.check(chalkOf("minecraft:black_dye", false) == std::uint8_t{0}, "black dye erases");
    ctx.check(!chalkOf("minecraft:red_dye", false), "red dye needs colored_blackboard");
    ctx.check(chalkOf("minecraft:red_dye", true) == std::uint8_t{14}, "red dye draws red when coloured");
    ctx.check(!chalkOf("minecraft:stick", true), "a stick is no chalk");
    auto top = packHalf(b.px, 0);
    // pixel (3, 4) is index 67: property 11, nibble 1
    ctx.check(top[11] == (1 << 4), "pixels pack 6 per property");
    auto bottom = packHalf(b.px, 1);
    ctx.check(bottom[21] == (14 << 4), "the last pixel lands in the last property");
    ctx.check(colorMask(b.px, 0, 8) == (1u << 1) && colorMask(b.px, 8, 8) == (1u << 14), "colour masks per half");
    ctx.check(b.blank() == false && Board{}.empty(), "blank / empty");
    ctx.check(pixelArt(b.px).size() == 16, "16 lines of tooltip art");
});

CM_REGISTER_SELFTEST("blackboard.item_data", [](Context& ctx) {
    Board b;
    b.set(0, 0, 1);
    b.glow = true;
    auto item = makeBoardItem(b);
    auto back = boardOfItem(item);
    ctx.check(back && *back == b, "the item keeps the board");
    ctx.check(item.getCustomLore().size() == 17, "lore: glowing + 16 lines of art");
    ctx.check(!boardOfItem(makeBoardItem(Board{})), "a blank item carries no board");
    ctx.check(!boardOfItem(itemOf("minecraft:stone")), "other items carry no board");
});

CM_REGISTER_SELFTEST("blackboard.draw", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos    = co_await placeBoard(ctx, 500);
    auto& player = ctx.player();
    auto& region = player.getDimensionBlockSource();
    int   front  = frontOf(region.getBlock(pos));
    ctx.require(front >= 0, "the block is a blackboard");
    auto dye = itemOf("minecraft:white_dye", 4);
    ctx.check(use(player, dye, pos, faceIdOf(front), hitFor(front, 3, 4)), "drawing consumes the use");
    auto gp = game::globalPos(region, pos);
    ctx.check(boardAt(gp).get(3, 4) == 1, "the pixel under the cursor is white");
    ctx.check(dye.mCount == 4, "chalk is not consumed");
    co_await ctx.ticks(2);
    auto displays = displaysAt(region, pos);
    ctx.check(displays.size() == 2, "two display entities render the drawing");
    for (auto* d : displays) {
        int colors = displayProperty(*d, "complementaries:colors");
        int half   = displayProperty(*d, "complementaries:half");
        if (half == 0) {
            ctx.check(colors == 2, "the top half uses white");
            ctx.check(displayProperty(*d, "complementaries:p11") == (1 << 4), "pixel (3, 4) is in property 11");
        } else {
            ctx.check(half == 1 && colors == 0, "the bottom half is blank");
        }
    }
    // the absolute hit position works too
    auto abs = hitFor(front, 10, 12);
    abs      = Vec3{abs.x + pos.x, abs.y + pos.y, abs.z + pos.z};
    use(player, dye, pos, faceIdOf(front), abs);
    ctx.check(boardAt(gp).get(10, 12) == 1, "absolute hit positions map to the same pixel");
    // another face opens the editor instead of drawing
    auto red = itemOf("minecraft:red_dye");
    use(player, red, pos, faceIdOf(front), hitFor(front, 0, 0));
    ctx.check(boardAt(gp).get(0, 0) == 0, "coloured dyes do not draw by default");
    auto coal = itemOf("minecraft:coal");
    use(player, coal, pos, faceIdOf(front), hitFor(front, 3, 4));
    use(player, coal, pos, faceIdOf(front), abs);
    ctx.check(boardAt(gp).blank(), "coal erases");
    co_await ctx.ticks(2);
    ctx.check(displaysAt(region, pos).empty(), "a blank board has no display entities");
    closeAllEditors();
});

CM_REGISTER_SELFTEST("blackboard.wax_glow", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos    = co_await placeBoard(ctx, 502);
    auto& player = ctx.player();
    auto& region = player.getDimensionBlockSource();
    int   front  = frontOf(region.getBlock(pos));
    auto  gp     = game::globalPos(region, pos);
    auto  dye    = itemOf("minecraft:white_dye");
    use(player, dye, pos, faceIdOf(front), hitFor(front, 1, 1));
    auto ink = itemOf("minecraft:glow_ink_sac", 2);
    ctx.check(use(player, ink, pos, faceIdOf(front) ^ 1, hitFor(front, 0, 0)), "glow ink works on any face");
    ctx.check(boardAt(gp).glow, "the board glows");
    ctx.check(player.isCreative() || ink.mCount == 1, "one glow ink sac is used");
    auto comb = itemOf("minecraft:honeycomb", 2);
    use(player, comb, pos, faceIdOf(front), hitFor(front, 0, 0));
    ctx.check(boardAt(gp).waxed, "honeycomb waxes the board");
    use(player, dye, pos, faceIdOf(front), hitFor(front, 5, 5));
    ctx.check(boardAt(gp).get(5, 5) == 0, "a waxed board cannot be drawn on");
    auto brush = itemOf("minecraft:brush");
    use(player, brush, pos, faceIdOf(front), hitFor(front, 0, 0));
    ctx.check(boardAt(gp).get(1, 1) == 1, "a brush does not clear a waxed board");
    co_await ctx.ticks(1);
    closeAllEditors();
});

CM_REGISTER_SELFTEST("blackboard.brush", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos    = co_await placeBoard(ctx, 504);
    auto& player = ctx.player();
    auto& region = player.getDimensionBlockSource();
    int   front  = frontOf(region.getBlock(pos));
    auto  gp     = game::globalPos(region, pos);
    auto  dye    = itemOf("minecraft:white_dye");
    use(player, dye, pos, faceIdOf(front), hitFor(front, 7, 7));
    auto brush = itemOf("minecraft:brush");
    use(player, brush, pos, faceIdOf(front), hitFor(front, 0, 0));
    ctx.check(boardAt(gp).blank(), "a brush clears the drawing");
    co_await ctx.ticks(1);
});

CM_REGISTER_SELFTEST("blackboard.drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos    = co_await placeBoard(ctx, 506);
    auto& region = ctx.player().getDimensionBlockSource();
    Board b;
    b.set(2, 2, 1);
    b.waxed = true;
    setBoard(region, pos, b);
    runCommand(std::format("setblock {} {} {} air destroy", pos.x, pos.y, pos.z));
    co_await ctx.ticks(2);
    bool found = false;
    AABB box{
        Vec3{static_cast<float>(pos.x) - 2, static_cast<float>(pos.y) - 2, static_cast<float>(pos.z) - 2},
        Vec3{static_cast<float>(pos.x) + 3, static_cast<float>(pos.y) + 3, static_cast<float>(pos.z) + 3}
    };
    for (auto* a : region.getEntities(box, 1.f)) {
        if (!a || a->getTypeName() != "minecraft:item") continue;
        auto& stack = static_cast<ItemActor*>(a)->item();
        if (auto data = boardOfItem(stack); data && *data == b) found = true;
        if (stack.getTypeName() == kBlockId) a->remove();
    }
    ctx.check(found, "the dropped item keeps the drawing");
    ctx.check(boardAt(game::globalPos(region, pos)).empty(), "the broken board's data is gone");
    ctx.check(displaysAt(region, pos).empty(), "the display entities are gone");
});

CM_REGISTER_SELFTEST("blackboard.place_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto gp = ctx.testPos(508);
    ctx.require(co_await ctx.ensureLoaded(gp), "test area loaded");
    ctx.require(ctx.setBlock(gp, "minecraft:stone"), "floor block");
    auto  above = cm::core::GlobalPos{gp.dim, gp.x, gp.y + 1, gp.z};
    ctx.cleanupBlock(above);
    auto& player = static_cast<SimulatedPlayer&>(ctx.player());
    Board b;
    b.set(4, 9, 1);
    auto item = makeBoardItem(b);
    int  slot = ctx.give(player, item, 0);
    ctx.require(slot == 0, "item given");
    player.setSelectedSlot(0);
    co_await ctx.ticks(2);
    player.simulateUseItemInSlotOnBlock(0, game::blockPos(gp), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(2);
    auto& region = player.getDimensionBlockSource();
    ctx.require(isBlackboard(region.getBlock(game::blockPos(above))), "the item placed a blackboard");
    ctx.check(boardAt(above) == b, "the placed board has the item's drawing");
});

CM_REGISTER_SELFTEST("blackboard.pick", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos    = co_await placeBoard(ctx, 510);
    auto& region = ctx.player().getDimensionBlockSource();
    Board b;
    b.set(8, 8, 1);
    setBoard(region, pos, b);
    auto picked = region.getBlock(pos).asItemInstance(region, pos, true);
    auto data   = boardOfItem(picked);
    ctx.check(data && *data == b, "pick block with data keeps the drawing");
    co_await ctx.ticks(1);
});

CM_REGISTER_SELFTEST("blackboard.connected", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a 2 wide x 2 high wall: placed along x (all boards face the same way)
    auto base = ctx.testPos(512);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    for (int dx = 0; dx < 2; ++dx)
        for (int dy = 0; dy < 2; ++dy)
            ctx.require(ctx.setBlock({base.dim, base.x + dx, base.y + dy, base.z}, std::string(kBlockId)), "board placed");
    co_await ctx.ticks(1);
    int front = frontOf(region.getBlock(game::blockPos(base)));
    if (front == 1 || front == 3) {
        ctx.info("default facing is along x: the wall is not flat for this facing, skipping");
        co_return;
    }
    auto canvas = connectedBoards(region, game::blockPos(base), true);
    ctx.check(canvas.cols == 2 && canvas.rows == 2, std::format("2x2 canvas (got {}x{})", canvas.cols, canvas.rows));
    int filled = 0;
    for (auto const& c : canvas.cells) filled += c ? 1 : 0;
    ctx.check(filled == 4, "every cell has a board");
    // row 0 is the top row
    if (canvas.cols == 2 && canvas.rows == 2) {
        ctx.check(canvas.at(0, 0)->y == base.y + 1, "row 0 is on top");
        // column 0 is on the viewer's left: east (+x) for a north-facing board
        int leftX = front == 0 ? base.x + 1 : base.x;
        ctx.check(canvas.at(0, 0)->x == leftX, "column 0 is on the viewer's left");
    }
    auto single = connectedBoards(region, game::blockPos(base), false);
    ctx.check(single.cols == 1 && single.rows == 1, "connected_gui off: one board");
});

CM_REGISTER_SELFTEST("blackboard.submit", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos    = co_await placeBoard(ctx, 516);
    auto& player = ctx.player();
    auto& region = player.getDimensionBlockSource();
    auto  gp     = game::globalPos(region, pos);
    Pixels px{};
    px[17] = 1;
    ctx.check(!submitPixels(&player, false, gp, px).empty(), "players without a session are refused");
    ctx.check(submitPixels(nullptr, true, gp, px).empty(), "admins may set pixels");
    ctx.check(boardAt(gp).px == px, "pixels applied");
    Pixels colored{};
    colored[0] = 14;
    // a session: open the editor (the form is shown to the simulated player) and submit
    ctx.check(openEditor(player, pos), "the editor opens");
    ctx.check(editorOf(gp).has_value(), "the board is being edited");
    ctx.check(!submitPixels(&player, false, gp, colored).empty(), "coloured pixels are refused by default");
    Pixels next{};
    next[200] = 1;
    ctx.check(submitPixels(&player, false, gp, next).empty(), "the editor may submit");
    ctx.check(boardAt(gp).px == next, "submitted pixels applied");
    closeAllEditors();
    ctx.check(!editorOf(gp).has_value(), "sessions closed");
    co_await ctx.ticks(1);
});
