// Selftests of the trap unit (dx 1700..1799).

#include <functional>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "feature/trap/Trap.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace {

using cm::selftest::Context;
namespace trap = cm::feature::trap;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

Vec3 centerOf(cm::core::GlobalPos const& pos, float dy = 0.1f) {
    return Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + dy, static_cast<float>(pos.z) + 0.5f};
}

/// Mobs spawned by a test, removed afterwards.
struct Mobs {
    std::vector<int64_t> ids;
    ~Mobs() {
        for (auto id : ids) {
            if (auto* a = trap::actorByUid(id)) a->despawn();
        }
    }
    Actor* spawn(BlockSource& region, std::string const& type, Vec3 const& pos, bool baby = false) {
        auto&  level = region.getLevel();
        Mob*   mob   = nullptr;
        if (baby) {
            ActorDefinitionIdentifier id("minecraft", type.substr(type.find(':') + 1), "minecraft:entity_born");
            mob = level.getSpawner().spawnMob(region, id, nullptr, pos, false, false, false);
        } else {
            ActorDefinitionIdentifier id(type);
            mob = level.getSpawner().spawnMob(region, id, nullptr, pos, false, false, false);
        }
        if (mob) ids.push_back(trap::uid(*mob));
        return mob;
    }
};

/// Item entities of `id` within 2 blocks of `pos`; removes them.
int collectItems(Level& level, Vec3 const& pos, std::string const& id, std::function<void(ItemStack const&)> const& fn = {}) {
    int n = 0;
    for (Actor* a : level.getRuntimeActorList()) {
        if (!a || a->getTypeName() != "minecraft:item") continue;
        if ((a->getPosition() - pos).length() > 2.5) continue;
        auto& item = static_cast<ItemActor*>(a)->item();
        if (trap::itemId(item) != id) continue;
        if (fn) fn(item);
        ++n;
        a->despawn();
    }
    return n;
}

struct ConfigGuard {
    trap::TrapConfig saved = trap::config();
    ~ConfigGuard() { trap::mutableConfig() = saved; }
};

} // namespace

CM_REGISTER_SELFTEST("trap.spikes_damage", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1700);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(trap::kSpikesId), {{"minecraft:block_face", 1}}), "spikes placed");
    Mobs mobs;
    auto* chicken = mobs.spawn(regionOf(ctx), "minecraft:chicken", centerOf(pos));
    ctx.require(chicken != nullptr, "chicken spawned");
    int before = chicken->getHealth();
    co_await ctx.ticks(30);
    chicken = trap::actorByUid(mobs.ids[0]);
    ctx.check(!chicken || chicken->getHealth() < before, "a chicken inside spikes takes damage");
});

CM_REGISTER_SELFTEST("trap.spikes_player", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1702);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(trap::kSpikesId), {{"minecraft:block_face", 1}}), "spikes placed");
    auto& p = ctx.spawnPlayer();
    p.teleport(centerOf(pos, 0.07f), p.getDimensionId());
    co_await ctx.ticks(15);
    ctx.check(p.getHealth() < p.getMaxHealth(), "a survival player standing in spikes takes damage");
});

CM_REGISTER_SELFTEST("trap.tipped_poison", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1704);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(trap::kSpikesId), {{"minecraft:block_face", 1}}), "spikes placed");
    auto& region = regionOf(ctx);
    auto  bp     = cm::game::blockPos(pos);
    ctx.require(trap::spikes::tryAddPotion(region, bp, 25), "poison tips the spikes");
    ctx.check(trap::stateOf(region.getBlock(bp), trap::kTipState) > 0, "tip state set");
    ctx.check(!trap::spikes::tryAddPotion(region, bp, 25), "tipped spikes refuse a second potion");
    auto tip = trap::spikes::tipAt(pos);
    ctx.require(tip && tip->potion == 25 && tip->charges == trap::kMaxCharges, "tip data stored");
    Mobs mobs;
    auto* chicken = mobs.spawn(region, "minecraft:chicken", centerOf(pos));
    ctx.require(chicken != nullptr, "chicken spawned");
    co_await ctx.ticks(5);
    chicken = trap::actorByUid(mobs.ids[0]);
    ctx.check(chicken && chicken->getEffect(19u) != nullptr, "the chicken is poisoned");
    tip = trap::spikes::tipAt(pos);
    ctx.check(tip && tip->charges == trap::kMaxCharges, "harmful potions never use up charges");
    // the block drops a tipped item with the potion when removed
    cm::game::clearBlock(region, bp);
    co_await ctx.ticks(2);
    int potion = -1;
    int n      = collectItems(region.getLevel(), centerOf(pos, 0.5f), std::string(trap::kTippedId), [&](ItemStack const& it) {
        if (auto t = trap::spikes::itemTip(it)) potion = t->potion;
    });
    ctx.check(n == 1 && potion == 25, "removed tipped spikes drop a tipped spike with the potion");
    ctx.check(!trap::spikes::tipAt(pos), "tip data removed with the block");
});

CM_REGISTER_SELFTEST("trap.tipped_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    ctx.check(trap::spikes::isPotionValid(25), "poison is valid");
    ctx.check(!trap::spikes::isPotionValid(21), "healing is not valid with only_allow_harmful_effects");
    ctx.check(!trap::spikes::isPotionValid(0), "water is never valid");
    trap::mutableConfig().bambooSpikes.onlyAllowHarmfulEffects = false;
    ctx.check(trap::spikes::isPotionValid(21), "healing is valid without only_allow_harmful_effects");

    auto item = trap::spikes::tippedItem(25, 7);
    auto tip  = trap::spikes::itemTip(item);
    ctx.check(tip && tip->potion == 25 && tip->charges == 7, "tipped item keeps potion and charges");
    ctx.check(!item.getCustomLore().empty(), "tipped item has a tooltip");
    auto crafted = trap::makeItem(trap::kTippedId, 1, 26);
    auto ctip    = trap::spikes::itemTip(crafted);
    ctx.check(ctip && ctip->potion == 26, "crafted tipped item carries the potion as aux value");

    // finite charges: instant health is used every 20 ticks
    auto pos = ctx.testPos(1706);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(trap::kSpikesId), {{"minecraft:block_face", 1}}), "spikes placed");
    auto& region = regionOf(ctx);
    auto  bp     = cm::game::blockPos(pos);
    ctx.require(trap::spikes::tryAddPotion(region, bp, 21, 2), "healing tips the spikes");
    Mobs  mobs;
    auto* chicken = mobs.spawn(region, "minecraft:chicken", centerOf(pos));
    ctx.require(chicken != nullptr, "chicken spawned");
    for (int i = 0; i < 60 && trap::spikes::tipAt(pos); ++i) co_await ctx.ticks(1);
    ctx.check(!trap::spikes::tipAt(pos), "charges run out");
    ctx.check(trap::stateOf(region.getBlock(bp), trap::kTipState) == 0, "spikes are plain again");
});

CM_REGISTER_SELFTEST("trap.cage_catch_release", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1710);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = regionOf(ctx);
    Mobs  mobs;
    auto* chicken = mobs.spawn(region, "minecraft:chicken", centerOf(pos, 0.f));
    ctx.require(chicken != nullptr, "chicken spawned");
    int slot = ctx.give(p, trap::makeItem(trap::kCageId), 0);
    ctx.require(slot == 0, "cage given");
    p.setSelectedSlot(0);
    co_await ctx.ticks(1);
    ctx.require(trap::cage::tryCatch(p, *chicken), "the chicken is caught");
    co_await ctx.ticks(1);
    ctx.check(trap::actorByUid(mobs.ids[0]) == nullptr, "the chicken left the world");
    ctx.require(trap::cage::isFull(p.getSelectedItem()), "the held cage is full");
    auto at = centerOf(ctx.testPos(1712), 0.f);
    ctx.require(trap::cage::release(p, at), "released");
    ctx.check(!trap::cage::isFull(p.getSelectedItem()), "the cage is empty again");
    co_await ctx.ticks(1);
    int found = 0;
    for (Actor* a : region.getLevel().getRuntimeActorList()) {
        if (a && a->getTypeName() == "minecraft:chicken" && (a->getPosition() - at).length() < 1.5) {
            ++found;
            mobs.ids.push_back(trap::uid(*a));
        }
    }
    ctx.check(found == 1, "the chicken is back where the cage was used");
});

CM_REGISTER_SELFTEST("trap.cage_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    auto        pos = ctx.testPos(1720);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = regionOf(ctx);
    Mobs  mobs;
    auto* cow  = mobs.spawn(region, "minecraft:cow", centerOf(pos, 0.f));
    auto* calf = mobs.spawn(region, "minecraft:cow", centerOf(ctx.testPos(1722), 0.f), true);
    auto* cat  = mobs.spawn(region, "minecraft:cat", centerOf(ctx.testPos(1724), 0.f));
    auto* golem = mobs.spawn(region, "minecraft:iron_golem", centerOf(ctx.testPos(1726), 0.f));
    ctx.require(cow && calf && cat && golem, "mobs spawned");
    co_await ctx.ticks(2);
    ctx.check(!trap::cage::catchFailure(*cow, p).empty(), "adult cows can't be caught");
    ctx.check(calf->isBaby(), "the calf is a baby");
    ctx.check(trap::cage::catchFailure(*calf, p).empty(), "baby cows can be caught");
    auto catFail = trap::cage::catchFailure(*cat, p);
    ctx.check(!catFail.empty() && catFail != "-", "wild cats need taming first");
    trap::mutableConfig().cage.requireTaming = false;
    ctx.check(trap::cage::catchFailure(*cat, p).empty(), "cats can be caught without require_taming");
    ctx.check(!trap::cage::catchFailure(*golem, p).empty(), "iron golems can't be caught");
    trap::mutableConfig().cage.allowAllMobs = true;
    ctx.check(trap::cage::catchFailure(*golem, p).empty(), "allow_all_mobs catches anything");
    trap::mutableConfig().cage.allowAllMobs = false;
    trap::cage::addCommandMob("minecraft:iron_golem");
    ctx.check(trap::cage::catchFailure(*golem, p).empty(), "the cage command adds a temporary type");
    trap::mutableConfig().cage.healthThreshold = 50;
    ctx.check(!trap::cage::catchFailure(*calf, p).empty(), "a healthy calf fails the health threshold");
    ctx.check(trap::cage::catchFailure(p, p) == "-", "players are never caught");
});

CM_REGISTER_SELFTEST("trap.cage_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1730);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = regionOf(ctx);
    Mobs  mobs;
    auto* chicken = mobs.spawn(region, "minecraft:chicken", centerOf(ctx.testPos(1732), 0.f));
    ctx.require(chicken != nullptr, "chicken spawned");
    ctx.give(p, trap::makeItem(trap::kCageId), 0);
    p.setSelectedSlot(0);
    co_await ctx.ticks(1);
    ctx.require(trap::cage::tryCatch(p, *chicken), "the chicken is caught");
    auto data = *trap::userData(p.getSelectedItem());

    ctx.require(ctx.setBlock(pos, std::string(trap::kCageId)), "cage placed");
    auto bp = cm::game::blockPos(pos);
    trap::cage::onPlaced(region, bp, data);
    co_await ctx.ticks(2);
    ctx.check(trap::stateOf(region.getBlock(bp), trap::kFullState) == 1, "the cage block is full");
    ctx.check(trap::cage::contents(pos) != nullptr, "the mob is stored with the block");
    auto* display = trap::cage::displayAt(pos);
    ctx.require(display != nullptr, "the mob is shown inside the cage");
    ctx.check(display->getTypeName() == "minecraft:chicken", "the display is the caught chicken");
    ctx.check(SynchedActorDataAccess::getActorFlag(display->getEntityContext(), ActorFlags::Noai), "the display has no AI");
    int64_t displayId = trap::uid(*display);
    float   health    = static_cast<float>(display->getHealth());
    display->hurtByCause(4.f);
    ctx.check(static_cast<float>(display->getHealth()) == health, "the display is invulnerable");

    cm::game::clearBlock(region, bp);
    co_await ctx.ticks(2);
    ctx.check(trap::actorByUid(displayId) == nullptr, "the display goes with the cage");
    ctx.check(trap::cage::contents(pos) == nullptr, "the stored mob goes with the cage");
    bool full = false;
    int  n    = collectItems(region.getLevel(), centerOf(pos, 0.5f), std::string(trap::kCageId), [&](ItemStack const& it) {
        full = trap::cage::isFull(it);
    });
    ctx.check(n == 1 && full, "the broken cage drops a full cage");
});

CM_REGISTER_SELFTEST("trap.fodder_settle", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1740);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    auto  bp     = cm::game::blockPos(pos);
    BlockPos up{bp.x, bp.y + 1, bp.z};
    ctx.require(ctx.setBlock(pos, std::string(trap::kFodderId), {{std::string(trap::kEatenState), 5}}), "fodder placed");
    auto upPos = pos;
    upPos.y += 1;
    ctx.require(ctx.setBlock(upPos, std::string(trap::kFodderId), {{std::string(trap::kEatenState), 0}}), "fodder above");
    co_await ctx.ticks(2);
    ctx.check(trap::fodder::layersAt(region, bp) == 8, "the lower fodder is filled");
    ctx.check(trap::fodder::layersAt(region, up) == 3, "the upper fodder gave its layers");

    auto& p = ctx.player();
    ctx.give(p, trap::makeItem("minecraft:iron_hoe"), 0);
    p.setSelectedSlot(0);
    trap::fodder::useHoe(p, up);
    ctx.check(trap::fodder::layersAt(region, up) == 2, "a hoe removes one layer");
});

CM_REGISTER_SELFTEST("trap.fodder_eat", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1750);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    auto  bp     = cm::game::blockPos(pos);
    ctx.require(ctx.setBlock(pos, std::string(trap::kFodderId), {{std::string(trap::kEatenState), 6}}), "fodder placed");
    Mobs  mobs;
    auto* cow = mobs.spawn(region, "minecraft:cow", centerOf(pos, 0.3f));
    ctx.require(cow != nullptr, "cow spawned");
    co_await ctx.ticks(2);
    trap::fodder::forceSearch(*cow);
    bool ate = false;
    for (int i = 0; i < 200 && !ate; ++i) {
        co_await ctx.ticks(1);
        ate = trap::fodder::layersAt(region, bp) < 2;
    }
    ctx.check(ate, "the cow eats a layer of fodder");
    cow = trap::actorByUid(mobs.ids[0]);
    ctx.check(cow && cow->isInLove(), "the cow is in love after eating");
});
