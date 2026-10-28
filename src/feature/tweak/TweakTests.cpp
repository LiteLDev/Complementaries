// Selftests of U18 tweak (dx 1800..1899).

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BellBlockActor.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/CauldronBlockActor.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
using namespace cm::feature::tweak;
using ScriptFacing = ScriptModuleMinecraft::ScriptFacing;

BlockSource& regionOfPlayer(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

std::string idAt(Context& ctx, cm::core::GlobalPos const& p) {
    return cm::game::blockIdAt(regionOfPlayer(ctx), cm::game::blockPos(p));
}

int stateAt(Context& ctx, cm::core::GlobalPos const& p, std::string_view state) {
    return cm::game::intState(regionOfPlayer(ctx).getBlock(cm::game::blockPos(p)), state).value_or(-1);
}

cm::core::GlobalPos up(cm::core::GlobalPos p, int n = 1) {
    p.y += n;
    return p;
}

/// Puts `id` into hotbar slot 0 (selected) of the test player.
void hold(Context& ctx, std::string const& id, int count = 1, int aux = 0) {
    auto& p = ctx.player();
    p.setSelectedSlot(0);
    ctx.give(p, id.empty() ? ItemStack() : makeItem(id, count, aux), 0);
}

int heldCount(Context& ctx) {
    auto const& item = ctx.player().getInventory().getItem(0);
    return item.isNull() ? 0 : static_cast<int>(item.mCount);
}

/// Uses the held item on a block face through the same entry point as PlayerInteractBlockEvent
/// (simulated players don't go through the network handler that fires the event).
bool useOn(Context& ctx, cm::core::GlobalPos const& pos, ScriptFacing face) {
    auto& p = ctx.player();
    return dispatchUseOn(p, p.getSelectedItem(), cm::game::blockPos(pos), static_cast<int>(face), Vec3{0.5f, 0.5f, 0.5f});
}

bool useInAir(Context& ctx) {
    auto& p = ctx.player();
    return dispatchUse(p, p.getSelectedItem());
}

int countItem(Context& ctx, std::string const& id) {
    auto& inv = ctx.player().getInventory();
    int   n   = 0;
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& it = inv.getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}
} // namespace

CM_REGISTER_SELFTEST("tweak.config", [](Context& ctx) {
    auto const& c = config();
    ctx.check(c.version >= 1, "config section loaded");
    ctx.check(c.gunpowderBurnTicks >= 1 && c.gunpowderSpreadAge >= 1, "gunpowder timing is sane");
    ctx.check(cm::translate("message.supplementaries.compass", "?", "en_US") == "X: {0} Y: {1} Z: {2}", "en_US strings loaded");
    ctx.check(cm::translate("message.supplementaries.compass", "?", "zh_CN") != "?", "zh_CN strings loaded");
});

CM_REGISTER_SELFTEST("tweak.helpers", [](Context& ctx) {
    int aux = 0;
    ctx.check(recolor("minecraft:white_wool", 14, aux) == "minecraft:red_wool", "wool recolours");
    ctx.check(recolor("minecraft:light_blue_stained_glass_pane", 4, aux) == "minecraft:yellow_stained_glass_pane", "panes recolour");
    ctx.check(recolor("minecraft:glass", 15, aux) == "minecraft:black_stained_glass", "glass gets stained");
    ctx.check(recolor("minecraft:hardened_clay", 1, aux) == "minecraft:orange_terracotta", "terracotta gets stained");
    ctx.check(recolor("minecraft:stone", 1, aux).empty(), "stone is not dyeable");
    aux = 0;
    ctx.check(recolor("minecraft:bed", 11, aux) == "minecraft:bed" && aux == 11, "beds change aux");
    ctx.check(nearestDye(0xFFB02E26) == 14 && nearestDye(0xFF1D1D21) == 15, "nearest dye colour");
    ctx.check(plantIndex("minecraft:poppy") == 1 && plantItem(1) == "minecraft:poppy", "plant table");
    ctx.check(plantIndex("minecraft:stone") == 0 && plantCount() == 32, "plant table size");
    ctx.check(clockText(0) == "06:00" && clockText(18000) == "00:00" && clockText(6500) == "12:30", "clock text");
    ctx.check(isChain("minecraft:chain") && isChain("minecraft:iron_chain") && isChain("minecraft:waxed_copper_chain"), "chains");
    ctx.check(!isChain("minecraft:chain_command_block"), "command blocks are no chains");
    ctx.check(stickBlockFor("minecraft:stick") == kStickBlock && stickItemFor(kBlazeRodBlock) == "minecraft:blaze_rod", "stick kinds");
});

CM_REGISTER_SELFTEST("tweak.blocks_registered", [](Context& ctx) {
    for (auto const* id : {kStickBlock, kBlazeRodBlock, kBreezeRodBlock, kGunpowderBlock, kHangingPotBlock}) {
        ctx.check(cm::game::blockWith(id) != nullptr, std::string("block registered: ") + id);
    }
    auto const* s = cm::game::blockWith(kStickBlock, {{"complementaries:axis_x", 1}, {"complementaries:axis_z", 1}});
    ctx.require(s != nullptr, "stick block with states");
    auto drops = dropsOf(*s);
    ctx.check(drops.size() == 1 && drops[0].mCount == 2, "two sticks drop two items");
    auto const* pot = cm::game::blockWith(kHangingPotBlock, {{"complementaries:plant", 1}});
    ctx.require(pot != nullptr, "hanging pot with a plant");
    ctx.check(dropsOf(*pot).size() == 2, "a planted hanging pot drops the pot and the plant");
    auto const* lit = cm::game::blockWith(kGunpowderBlock, {{"complementaries:burning", 3}});
    ctx.require(lit != nullptr, "burning trail");
    ctx.check(dropsOf(*lit).empty(), "a burning trail drops nothing");
    ctx.check(dropsOf(*cm::game::blockWith(kGunpowderBlock)).size() == 1, "an unlit trail drops gunpowder");
});

CM_REGISTER_SELFTEST("tweak.sticks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1800);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    ctx.require(ctx.setBlock(base, "minecraft:stone"), "base stone");
    ctx.cleanupBlock(up(base));
    hold(ctx, "minecraft:stick", 8);
    co_await ctx.ticks(2);
    useOn(ctx, base, ScriptFacing::Up);
    co_await ctx.ticks(3);
    ctx.require(idAt(ctx, up(base)) == kStickBlock, "a stick block is placed on top (got " + idAt(ctx, up(base)) + ")");
    ctx.check(stateAt(ctx, up(base), "complementaries:axis_y") == 1, "the stick stands along Y");
    ctx.check(heldCount(ctx) == 7, "one stick is used");
    useOn(ctx, up(base), ScriptFacing::North);
    co_await ctx.ticks(3);
    ctx.check(stateAt(ctx, up(base), "complementaries:axis_z") == 1, "a second stick is added along Z");
    useOn(ctx, up(base), ScriptFacing::East);
    co_await ctx.ticks(3);
    ctx.check(stateAt(ctx, up(base), "complementaries:axis_x") == 1, "a third stick is added along X");
    ctx.check(heldCount(ctx) == 5, "three sticks used");
});

CM_REGISTER_SELFTEST("tweak.gunpowder", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(1810);
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    auto b = ctx.testPos(1811);
    auto c = ctx.testPos(1812);
    for (auto p : {a, b, c}) {
        ctx.require(ctx.setBlock(p, "minecraft:stone"), "base stone");
        ctx.cleanupBlock(up(p));
    }
    hold(ctx, "minecraft:gunpowder", 4);
    co_await ctx.ticks(2);
    useOn(ctx, a, ScriptFacing::Up);
    co_await ctx.ticks(3);
    ctx.require(idAt(ctx, up(a)) == kGunpowderBlock, "a trail is placed (got " + idAt(ctx, up(a)) + ")");
    ctx.check(heldCount(ctx) == 3, "one gunpowder is used");
    auto& region = regionOfPlayer(ctx);
    ctx.require(placeTrail(region, cm::game::blockPos(up(b))), "second trail");
    ctx.require(placeTrail(region, cm::game::blockPos(up(c))), "third trail");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, up(a), "complementaries:east") == 1, "first trail connects east");
    ctx.check(stateAt(ctx, up(b), "complementaries:west") == 1 && stateAt(ctx, up(b), "complementaries:east") == 1, "middle trail connects both ways");
    ctx.check(stateAt(ctx, up(a), "complementaries:north") == 0, "no connection to nothing");

    // light with flint and steel; the fire runs along and the trails burn out
    hold(ctx, "minecraft:flint_and_steel");
    co_await ctx.ticks(1);
    useOn(ctx, up(a), ScriptFacing::Up);
    co_await ctx.ticks(3);
    ctx.check(stateAt(ctx, up(a), "complementaries:burning") > 0, "flint and steel lights the trail");
    co_await ctx.ticks(config().gunpowderBurnTicks * 12 + 10);
    ctx.check(idAt(ctx, up(a)) == "minecraft:air", "the first trail burnt out");
    ctx.check(idAt(ctx, up(c)) == "minecraft:air", "the fire ran to the last trail");
});

CM_REGISTER_SELFTEST("tweak.gunpowder_tnt", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(1815);
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    auto t = ctx.testPos(1816);
    auto camp = ctx.testPos(1815, 1);
    ctx.require(ctx.setBlock(a, "minecraft:stone") && ctx.setBlock(t, "minecraft:stone") && ctx.setBlock(camp, "minecraft:stone"), "base");
    ctx.require(ctx.setBlock(up(t), "minecraft:tnt"), "tnt");
    ctx.require(ctx.setBlock(up(camp), "minecraft:campfire", {{"extinguished", 1}}), "unlit campfire");
    ctx.cleanupBlock(up(a));
    auto& region = regionOfPlayer(ctx);
    ctx.require(placeTrail(region, cm::game::blockPos(up(a))), "trail");
    co_await ctx.ticks(2);
    ctx.check(stateAt(ctx, up(a), "complementaries:east") == 1, "the trail connects to TNT");
    igniteTrail(region, cm::game::blockPos(up(a)));
    co_await ctx.ticks(config().gunpowderBurnTicks * (config().gunpowderSpreadAge + 1) + 4);
    ctx.check(idAt(ctx, up(t)) == "minecraft:air", "the TNT was lit (block gone)");
    ctx.check(stateAt(ctx, up(camp), "extinguished") == 0, "the campfire was lit");
    runCommand("kill @e[type=tnt]");
    co_await ctx.ticks(2);
});

CM_REGISTER_SELFTEST("tweak.hanging_pot", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto top = up(ctx.testPos(1820), 3);
    ctx.require(co_await ctx.ensureLoaded(top), "test area loaded");
    ctx.require(ctx.setBlock(top, "minecraft:stone"), "ceiling");
    auto pot = up(top, -1);
    ctx.cleanupBlock(pot);
    hold(ctx, "minecraft:flower_pot", 2);
    co_await ctx.ticks(2);
    useOn(ctx, top, ScriptFacing::Down);
    co_await ctx.ticks(3);
    ctx.require(idAt(ctx, pot) == kHangingPotBlock, "a hanging pot is placed below (got " + idAt(ctx, pot) + ")");
    ctx.check(heldCount(ctx) == 1, "one flower pot used");
    hold(ctx, "minecraft:poppy", 1);
    co_await ctx.ticks(1);
    useOn(ctx, pot, ScriptFacing::North);
    co_await ctx.ticks(3);
    ctx.check(potPlant(regionOfPlayer(ctx).getBlock(cm::game::blockPos(pot))) == plantIndex("minecraft:poppy"), "the poppy is planted");
    ctx.check(heldCount(ctx) == 0, "the poppy is used");
    hold(ctx, "");
    co_await ctx.ticks(1);
    useOn(ctx, pot, ScriptFacing::North);
    co_await ctx.ticks(3);
    ctx.check(potPlant(regionOfPlayer(ctx).getBlock(cm::game::blockPos(pot))) == 0, "the plant is taken out");
    ctx.check(countItem(ctx, "minecraft:poppy") == 1, "the poppy is back in the inventory");
    // the pot pops off when its ceiling goes
    cm::game::clearBlock(regionOfPlayer(ctx), cm::game::blockPos(top));
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, pot) == "minecraft:air", "the pot falls without a ceiling");
});

CM_REGISTER_SELFTEST("tweak.bell_chain", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto ceiling = up(ctx.testPos(1830), 5);
    ctx.require(co_await ctx.ensureLoaded(ceiling), "test area loaded");
    auto& region = regionOfPlayer(ctx);
    std::string chain = cm::game::blockWith("minecraft:iron_chain") ? "minecraft:iron_chain" : "minecraft:chain";
    auto bell = ctx.testPos(1830);
    ctx.require(ctx.setBlock(up(bell, -1), "minecraft:stone"), "floor");
    ctx.require(ctx.setBlock(bell, "minecraft:bell"), "bell");
    ctx.require(ctx.setBlock(up(bell, 1), chain), "chain 1");
    ctx.require(ctx.setBlock(up(bell, 2), chain), "chain 2");
    co_await ctx.ticks(2);
    ctx.require(idAt(ctx, bell) == "minecraft:bell", "the bell stays");
    ctx.check(ringThroughChain(region, cm::game::blockPos(up(bell, 2)), &ctx.player()), "the chain finds the bell");
    auto* actor = region.getBlockEntity(cm::game::blockPos(bell));
    ctx.check(actor && static_cast<BellBlockActor*>(actor)->mRinging, "the bell rings");
    ctx.check(!ringThroughChain(region, cm::game::blockPos(up(bell, 4)), nullptr), "no bell through air");
});

CM_REGISTER_SELFTEST("tweak.throw_brick", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto glass = ctx.testPos(1840);
    ctx.require(co_await ctx.ensureLoaded(glass), "test area loaded");
    ctx.require(ctx.setBlock(glass, "minecraft:glass"), "glass");
    auto& region = regionOfPlayer(ctx);
    ActorDefinitionIdentifier id("complementaries", "throwable_brick", "");
    // thrown sideways at the glass (the test player stands above it)
    Vec3   from{glass.x + 4.5f, glass.y + 0.5f, glass.z + 0.5f};
    Actor* brick = region.getLevel().getSpawner().spawnProjectile(region, id, nullptr, from, Vec3{-1.f, 0.f, 0.f});
    ctx.require(brick != nullptr, "a thrown brick spawns");
    auto brickId = brick->getOrCreateUniqueID();
    for (int i = 0; i < 40 && idAt(ctx, glass) == "minecraft:glass"; ++i) {
        co_await ctx.ticks(1);
        if (i == 2) {
            if (auto* b = region.getLevel().fetchEntity(brickId, false)) {
                auto const& at = b->getPosition();
                ctx.info("brick at " + std::to_string(at.x) + " " + std::to_string(at.y) + " " + std::to_string(at.z));
            } else {
                ctx.info("brick gone");
            }
        }
    }
    ctx.check(idAt(ctx, glass) == "minecraft:air", "the brick broke the glass");

    // a player throws bricks and slimeballs from the hand
    hold(ctx, "minecraft:brick", 3);
    co_await ctx.ticks(1);
    useInAir(ctx);
    co_await ctx.ticks(6);
    ctx.check(heldCount(ctx) == 2, "throwing uses a brick");
    hold(ctx, "minecraft:slime_ball", 3);
    co_await ctx.ticks(1);
    useInAir(ctx);
    co_await ctx.ticks(2);
    ctx.check(heldCount(ctx) == 2, "throwing uses a slimeball");
});

CM_REGISTER_SELFTEST("tweak.bottle_xp", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto table = ctx.testPos(1850);
    ctx.require(co_await ctx.ensureLoaded(table), "test area loaded");
    ctx.require(ctx.setBlock(table, "minecraft:enchanting_table"), "enchanting table");
    auto& p = ctx.player();
    p.addExperience(30);
    co_await ctx.ticks(1);
    long long before = totalXp(p);
    ctx.require(before >= config().bottleXpCost, "the player has experience (" + std::to_string(before) + ")");
    hold(ctx, "minecraft:glass_bottle", 2);
    co_await ctx.ticks(1);
    useOn(ctx, table, ScriptFacing::Up);
    co_await ctx.ticks(3);
    ctx.check(countItem(ctx, "minecraft:experience_bottle") == 1, "a bottle o' enchanting");
    ctx.check(heldCount(ctx) == 1, "one glass bottle used");
    ctx.check(totalXp(p) == before - config().bottleXpCost, "the experience is paid (" + std::to_string(totalXp(p)) + ")");
});

CM_REGISTER_SELFTEST("tweak.cauldron_dye", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1860);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:cauldron", {{"fill_level", 6}}), "water cauldron");
    co_await ctx.ticks(1);
    auto& region = regionOfPlayer(ctx);
    auto* actor  = region.getBlockEntity(cm::game::blockPos(pos));
    ctx.require(actor != nullptr, "the cauldron has a block entity");
    auto* cauldron            = static_cast<CauldronBlockActor*>(actor);
    cauldron->mHasCustomColor = true;
    cauldron->mCustomColor    = static_cast<int>(0xFFB02E26);
    hold(ctx, "minecraft:white_wool", 4);
    co_await ctx.ticks(1);
    useOn(ctx, pos, ScriptFacing::Up);
    co_await ctx.ticks(3);
    auto const& held = ctx.player().getInventory().getItem(0);
    ctx.check(!held.isNull() && held.getTypeName() == "minecraft:red_wool", "the wool turns red (got " + itemId(held) + ")");
    ctx.check(heldCount(ctx) == 4, "the whole stack is dyed");
    ctx.check(stateAt(ctx, pos, "fill_level") == 4, "one bottle of water is used");
});

CM_REGISTER_SELFTEST("tweak.zombie_horse", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1870);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOfPlayer(ctx);
    auto& cfg    = cm::config::mut<TweakConfig>();
    int   saved  = cfg.zombieHorseFleshCount;
    cfg.zombieHorseFleshCount = 2;
    Mob* horse = region.getLevel().getSpawner().spawnMob(
        region,
        ActorDefinitionIdentifier("minecraft:skeleton_horse"),
        nullptr,
        Vec3{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f},
        false,
        false,
        false
    );
    if (!horse) {
        cfg.zombieHorseFleshCount = saved;
        ctx.fail("a skeleton horse spawns");
    }
    auto horseId = horse->getOrCreateUniqueID();
    hold(ctx, "minecraft:rotten_flesh", 4);
    co_await ctx.ticks(1);
    bool first = zombieHorseInteract(ctx.player(), *horse);
    co_await ctx.ticks(1);
    horse = static_cast<Mob*>(region.getLevel().fetchEntity(horseId, false));
    bool second = horse && zombieHorseInteract(ctx.player(), *horse);
    co_await ctx.ticks(3);
    cfg.zombieHorseFleshCount = saved;
    ctx.check(first && second, "the skeleton horse eats rotten flesh");
    ctx.check(heldCount(ctx) == 2, "two rotten flesh used");
    ctx.check(region.getLevel().fetchEntity(horseId, false) == nullptr, "the skeleton horse is gone");
    int zombies = 0;
    for (auto* a : region.getLevel().getRuntimeActorList()) {
        if (a && a->getTypeName() == "minecraft:zombie_horse" && a->getPosition().distanceTo(Vec3{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f}) < 4.f) {
            ++zombies;
            a->remove();
        }
    }
    ctx.check(zombies == 1, "a zombie horse took its place");
});
