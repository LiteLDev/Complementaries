// Selftests of the circuit unit (U8, test area dx 800..899).

#include <string>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputMessage.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/circuit/Circuit.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using cm::selftest::Context;
namespace circuit = cm::feature::circuit;
namespace game    = cm::game;

/// Runs a command as the server; returns false (and logs the output) when it did not succeed.
bool command(std::string const& cmd) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    ServerCommandOrigin origin("complementaries-selftest", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    auto out = ll::command::CommandRegistrar::getServerInstance().executeCommand(cmd, origin);
    if (out.mSuccessCount > 0) return true;
    std::string text;
    for (auto const& m : out.mMessages) {
        text += " " + m.mMessageId;
        for (auto const& p : m.mParams) text += " [" + p + "]";
    }
    cm::log().warn("selftest command '{}' failed:{}", cmd, text);
    return false;
}

std::string at(cm::core::GlobalPos const& p) {
    return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z);
}

cm::core::GlobalPos shift(cm::core::GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

BlockSource& region(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

Block const& blockAt(Context& ctx, cm::core::GlobalPos const& p) { return region(ctx).getBlock(game::blockPos(p)); }

int stateAt(Context& ctx, cm::core::GlobalPos const& p, std::string_view state) {
    return game::intState(blockAt(ctx, p), state).value_or(-1);
}

std::string idAt(Context& ctx, cm::core::GlobalPos const& p) { return game::blockIdAt(region(ctx), game::blockPos(p)); }

/// Waits up to `ticks` ticks for `pred`.
template <class F>
ll::coro::CoroTask<bool> waitFor(int ticks, F pred) {
    for (int i = 0; i < ticks; ++i) {
        if (pred()) co_return true;
        co_await Context::ticks(1);
    }
    co_return pred();
}

constexpr int kEast = 5;
constexpr int kNorth = 2;

} // namespace

CM_REGISTER_SELFTEST("circuit.registered", [](Context& ctx) {
    for (auto id : {circuit::kRelayer, circuit::kSpeaker, circuit::kDisplay, circuit::kIlluminator}) {
        auto const* b = game::blockWith(id);
        ctx.check(b != nullptr, std::string(id) + " is registered");
        if (b) ctx.check(circuit::kindOf(*b) != circuit::Kind::None, std::string(id) + " is recognised");
    }
    auto const* stone = game::blockWith("minecraft:stone");
    ctx.check(stone && circuit::kindOf(*stone) == circuit::Kind::None, "vanilla blocks are not ours");
    ItemStack cart;
    cart.reinit(circuit::kCart, 1, 0);
    ctx.check(!cart.isNull(), "the minecart with dispenser item exists");
});

CM_REGISTER_SELFTEST("circuit.facing_states", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto r = ctx.testPos(800);
    auto d = ctx.testPos(801);
    ctx.require(co_await ctx.ensureLoaded(r), "test area loaded");
    ctx.cleanupBlock(r);
    ctx.cleanupBlock(d);
    command("setblock " + at(r) + " complementaries:relayer [\"minecraft:facing_direction\"=\"east\"]");
    auto n = ctx.testPos(802);
    ctx.cleanupBlock(n);
    command("setblock " + at(d) + " complementaries:crystal_display [\"minecraft:cardinal_direction\"=\"west\"]");
    command("setblock " + at(n) + " complementaries:speaker_block [\"minecraft:cardinal_direction\"=\"north\"]");
    co_await ctx.ticks(1);
    ctx.check(circuit::facingOf(blockAt(ctx, n)) == kNorth, "cardinal_direction=north reads as north, got " + std::to_string(circuit::facingOf(blockAt(ctx, n))));
    ctx.check(game::blockWith(circuit::kDisplay, {{circuit::kFacing4, circuit::facingToCardinal(kEast)}}) != nullptr, "east display state");
    ctx.require(idAt(ctx, r) == circuit::kRelayer, "relayer placed by command");
    ctx.check(circuit::facingOf(blockAt(ctx, r)) == kEast, "facing_direction=east reads as east, got " + std::to_string(circuit::facingOf(blockAt(ctx, r))));
    ctx.check(circuit::facingOf(blockAt(ctx, d)) == 4, "cardinal_direction=west reads as west, got " + std::to_string(circuit::facingOf(blockAt(ctx, d))));
    ctx.check(circuit::tracked(r) && circuit::tracked(d), "placed blocks are tracked");
});

CM_REGISTER_SELFTEST("circuit.relayer", [](Context& ctx) -> ll::coro::CoroTask<> {
    // [redstone block] -> [relayer facing east] -> [lamp]; a block at the side does nothing
    auto src   = ctx.testPos(810);
    auto rel   = ctx.testPos(811);
    auto lamp  = ctx.testPos(812);
    auto side  = shift(rel, 0, 0, 1);
    ctx.require(co_await ctx.ensureLoaded(rel), "test area loaded");
    ctx.require(ctx.setBlock(rel, std::string(circuit::kRelayer), {{"minecraft:facing_direction", kEast}}), "relayer placed");
    ctx.require(ctx.setBlock(lamp, "minecraft:redstone_lamp"), "lamp placed");
    ctx.cleanupBlock(side);
    co_await ctx.ticks(4);
    ctx.check(stateAt(ctx, rel, circuit::kPowered) == 0, "unpowered relayer is off");
    ctx.require(ctx.setBlock(side, "minecraft:redstone_block"), "side power placed");
    co_await ctx.ticks(4);
    ctx.check(stateAt(ctx, rel, circuit::kPowered) == 0, "side input is ignored");
    game::clearBlock(region(ctx), game::blockPos(side));
    ctx.require(ctx.setBlock(src, "minecraft:redstone_block"), "back power placed");
    bool on = co_await waitFor(10, [&] { return stateAt(ctx, rel, circuit::kPowered) == 1; });
    ctx.check(on, "relayer turns on from behind");
    bool lit = co_await waitFor(10, [&] { return idAt(ctx, lamp) == "minecraft:lit_redstone_lamp"; });
    ctx.check(lit, "relayer output lights the lamp in front, got " + idAt(ctx, lamp));
    game::clearBlock(region(ctx), game::blockPos(src));
    bool off = co_await waitFor(10, [&] { return stateAt(ctx, rel, circuit::kPowered) == 0; });
    ctx.check(off, "relayer turns off without input");
    bool dark = co_await waitFor(20, [&] { return idAt(ctx, lamp) == "minecraft:redstone_lamp"; });
    ctx.check(dark, "the lamp goes out");
});

CM_REGISTER_SELFTEST("circuit.relayer_facings", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a relayer of each facing, powered from behind, with lamps on its other five sides: only the
    // lamp in front lights up
    int dirs[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(870)), "test area loaded");
    for (int f = 0; f < 6; ++f) {
        auto rel = shift(ctx.testPos(870 + f * 4), 0, 3, 0);
        ctx.setBlock(rel, std::string(circuit::kRelayer), {{"minecraft:facing_direction", f}});
        for (int i = 0; i < 6; ++i) {
            ctx.setBlock(shift(rel, dirs[i][0], dirs[i][1], dirs[i][2]), (i ^ 1) == f ? "minecraft:redstone_block" : "minecraft:redstone_lamp");
        }
    }
    co_await ctx.ticks(10);
    for (int f = 0; f < 6; ++f) {
        auto        rel = shift(ctx.testPos(870 + f * 4), 0, 3, 0);
        std::string lit;
        for (int i = 0; i < 6; ++i) {
            if (idAt(ctx, shift(rel, dirs[i][0], dirs[i][1], dirs[i][2])) == "minecraft:lit_redstone_lamp") lit += std::to_string(i) + " ";
        }
        ctx.check(
            stateAt(ctx, rel, circuit::kPowered) == 1 && lit == std::to_string(f) + " ",
            "facing " + std::to_string(f) + " lights only its front, lit: " + lit
        );
    }
});

CM_REGISTER_SELFTEST("circuit.relayer_repeater", [](Context& ctx) -> ll::coro::CoroTask<> {
    // [redstone block][repeater][relayer facing east], once per repeater direction: the repeater that
    // carries the power towards the relayer turns it on, the reversed one does not
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(885)), "test area loaded");
    int on = 0;
    for (int i = 0; i < 2; ++i) {
        auto src = shift(ctx.testPos(884), 0, 0, i * 2);
        auto rep = shift(src, 1, 0, 0);
        auto rel = shift(src, 2, 0, 0);
        ctx.cleanupBlock(rep);
        ctx.require(ctx.setBlock(rel, std::string(circuit::kRelayer), {{"minecraft:facing_direction", kEast}}), "relayer placed");
        command("setblock " + at(rep) + " unpowered_repeater [\"minecraft:cardinal_direction\"=\"" + (i ? "west" : "east") + "\"]");
        ctx.require(ctx.setBlock(src, "minecraft:redstone_block"), "power placed");
    }
    co_await ctx.ticks(12);
    for (int i = 0; i < 2; ++i) on += stateAt(ctx, shift(ctx.testPos(886), 0, 0, i * 2), circuit::kPowered) == 1;
    ctx.check(on == 1, "exactly one relayer is powered through its repeater, got " + std::to_string(on));
});

CM_REGISTER_SELFTEST("circuit.relayer_chain", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto src = ctx.testPos(820);
    ctx.require(co_await ctx.ensureLoaded(src), "test area loaded");
    for (int i = 1; i <= 3; ++i) {
        ctx.require(ctx.setBlock(ctx.testPos(820 + i), std::string(circuit::kRelayer), {{"minecraft:facing_direction", kEast}}), "relayer placed");
    }
    // a relayer facing sideways next to the chain must not take its power
    auto sideways = shift(ctx.testPos(822), 0, 0, 1);
    ctx.require(ctx.setBlock(sideways, std::string(circuit::kRelayer), {{"minecraft:facing_direction", kNorth}}), "side relayer placed");
    auto lamp = ctx.testPos(824);
    ctx.require(ctx.setBlock(lamp, "minecraft:redstone_lamp"), "lamp placed");
    co_await ctx.ticks(2);
    ctx.require(ctx.setBlock(src, "minecraft:redstone_block"), "power placed");
    bool on = co_await waitFor(10, [&] { return stateAt(ctx, ctx.testPos(823), circuit::kPowered) == 1; });
    ctx.check(on, "the last relayer of the chain turns on");
    ctx.check(stateAt(ctx, sideways, circuit::kPowered) == 0, "a relayer beside the chain stays off");
    bool lit = co_await waitFor(10, [&] { return idAt(ctx, lamp) == "minecraft:lit_redstone_lamp"; });
    ctx.check(lit, "the chain lights the lamp");
});

CM_REGISTER_SELFTEST("circuit.illuminator", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto ill = ctx.testPos(830);
    auto src = ctx.testPos(831);
    ctx.require(co_await ctx.ensureLoaded(ill), "test area loaded");
    ctx.require(ctx.setBlock(ill, std::string(circuit::kIlluminator)), "illuminator placed");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, ill, circuit::kPower) == 0, "dark without power");
    ctx.require(ctx.setBlock(src, "minecraft:redstone_block"), "power placed");
    bool on = co_await waitFor(10, [&] { return stateAt(ctx, ill, circuit::kPower) == 15; });
    ctx.check(on, "power 15 next to a redstone block, got " + std::to_string(stateAt(ctx, ill, circuit::kPower)));
    game::clearBlock(region(ctx), game::blockPos(src));
    bool off = co_await waitFor(10, [&] { return stateAt(ctx, ill, circuit::kPower) == 0; });
    ctx.check(off, "dark again without power");
});

CM_REGISTER_SELFTEST("circuit.display", [](Context& ctx) -> ll::coro::CoroTask<> {
    // two displays facing north side by side: seen from the north, the west one is on the right
    auto east = ctx.testPos(841);
    auto west = ctx.testPos(840);
    ctx.require(co_await ctx.ensureLoaded(east), "test area loaded");
    ctx.require(ctx.setBlock(east, std::string(circuit::kDisplay), {{"minecraft:cardinal_direction", circuit::facingToCardinal(kNorth)}}), "display placed");
    co_await ctx.ticks(2);
    ctx.check(circuit::facingOf(blockAt(ctx, east)) == kNorth, "display faces north");
    ctx.check(stateAt(ctx, east, circuit::kChain) == 0, "a lone display is alone");
    auto src = shift(east, 0, 0, 1);
    ctx.require(ctx.setBlock(src, "minecraft:redstone_block"), "power placed");
    bool on = co_await waitFor(10, [&] { return stateAt(ctx, east, circuit::kPower) == 15; });
    ctx.check(on, "the display shows 15");
    ctx.require(ctx.setBlock(west, std::string(circuit::kDisplay), {{"minecraft:cardinal_direction", circuit::facingToCardinal(kNorth)}}), "second display placed");
    bool paired = co_await waitFor(10, [&] {
        return stateAt(ctx, east, circuit::kChain) == 1 && stateAt(ctx, west, circuit::kChain) == 2;
    });
    ctx.check(paired, "left display = tens, right display = units (got " + std::to_string(stateAt(ctx, east, circuit::kChain)) + ", " + std::to_string(stateAt(ctx, west, circuit::kChain)) + ")");
    auto d = circuit::displayDigits(15, circuit::Chain::Tens);
    ctx.check(d.big == 9, "a chained display shows one digit clamped to 9");
});

CM_REGISTER_SELFTEST("circuit.speaker", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto spk = ctx.testPos(850);
    auto src = shift(spk, 0, 0, 1);
    ctx.require(co_await ctx.ensureLoaded(spk), "test area loaded");
    ctx.require(ctx.setBlock(spk, std::string(circuit::kSpeaker)), "speaker placed");
    circuit::SpeakerData data;
    data.message = "selftest says hi";
    data.mode    = static_cast<int>(circuit::SpeakerMode::Chat);
    data.volume  = 16;
    circuit::setSpeakerData(spk, data);
    ctx.check(circuit::speakerData(spk).message == data.message, "speaker data reads back");
    ctx.check(circuit::speak(region(ctx), game::blockPos(spk)) >= 1, "the test player hears the speaker");
    ctx.require(ctx.setBlock(src, "minecraft:redstone_block"), "power placed");
    bool on = co_await waitFor(10, [&] { return stateAt(ctx, spk, circuit::kPowered) == 1; });
    ctx.check(on, "the speaker is powered");
    // breaking the speaker drops its data
    game::clearBlock(region(ctx), game::blockPos(spk));
    co_await ctx.ticks(1);
    ctx.check(circuit::speakerData(spk).message.empty(), "data is removed with the block");
});

CM_REGISTER_SELFTEST("circuit.dispenser_minecart", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto rail = ctx.testPos(860);
    ctx.require(co_await ctx.ensureLoaded(rail), "test area loaded");
    auto below = shift(rail, 0, -1, 0);
    ctx.require(ctx.setBlock(below, "minecraft:stone"), "rail support");
    ctx.require(ctx.setBlock(rail, "minecraft:activator_rail", {{"rail_direction", 1}}), "activator rail placed");
    ctx.cleanupBlock(shift(rail, 0, 0, -1));
    // summoning right after the area loads can fail while the chunk is not ticking yet: retry
    bool summoned = co_await waitFor(40, [&] { return command("summon complementaries:dispenser_minecart " + at(rail)); });
    ctx.require(summoned, "summon command succeeds");
    // the cart is looked up again after every tick (no actor pointers across ticks)
    std::int64_t cartId   = 0;
    auto         findCart = [&]() -> Actor* {
        for (auto* a : ll::service::getLevel()->getRuntimeActorList()) {
            if (!a || a->getTypeName() != circuit::kCart) continue;
            if (cartId ? a->getOrCreateUniqueID().rawID == cartId
                       : a->getPosition().distanceTo(Vec3{rail.x + 0.5f, rail.y + 0.0f, rail.z + 0.5f}) < 3.f) {
                cartId = a->getOrCreateUniqueID().rawID;
                return a;
            }
        }
        return nullptr;
    };
    auto slot0 = [&]() -> int {
        auto* cart = findCart();
        if (!cart) return -1;
        auto container = cart->getEntityContext().tryGetComponent<ContainerComponent>();
        return container ? static_cast<int>(container->mContainer->getItem(0).mCount) : -1;
    };
    co_await waitFor(20, [&] { return findCart() != nullptr; });
    {
        auto* cart = findCart();
        ctx.require(cart != nullptr, "the cart was summoned");
        auto container = cart->getEntityContext().tryGetComponent<ContainerComponent>();
        ctx.require(container.has_value(), "the cart has an inventory");
        Container& inv = *container->mContainer;
        ctx.check(inv.getContainerSize() == 9, "9 slots, got " + std::to_string(inv.getContainerSize()));
        ItemStack arrows;
        arrows.reinit("minecraft:arrow", 4, 0);
        inv.setItem(0, arrows);
        ctx.check(circuit::dispenseFromCart(*cart), "the cart fires");
        ctx.check(inv.getItem(0).mCount == 3, "one arrow used, left " + std::to_string(static_cast<int>(inv.getItem(0).mCount)));
    }
    // a powered activator rail fires it by itself
    ctx.require(ctx.setBlock(shift(rail, 0, 0, -1), "minecraft:redstone_block"), "rail power placed");
    bool fired = co_await waitFor(20, [&] { int n = slot0(); return n >= 0 && n <= 2; });
    ctx.check(fired, "the cart fires on a powered activator rail");
    if (auto* cart = findCart()) cart->remove();
});
