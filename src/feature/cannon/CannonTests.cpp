// Selftests of U15 `cannon` (dx 1500..1599).

#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"

#include "feature/cannon/Cannon.h"
#include "feature/cannon/CannonConfig.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {

using cm::core::GlobalPos;
using cm::selftest::Context;
namespace cannon = cm::feature::cannon;

constexpr int kFaceUp = 1;

GlobalPos up(GlobalPos p, int dy) {
    p.y += dy;
    return p;
}
GlobalPos east(GlobalPos p, int dx) {
    p.x += dx;
    return p;
}
Vec3 centerOf(GlobalPos const& p) { return {p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

std::string blockAt(GlobalPos const& p) {
    auto* region = cannon::regionOf(p.dim);
    return region ? cm::game::blockIdAt(*region, cm::game::blockPos(p)) : std::string();
}

std::vector<Actor*> entitiesNear(GlobalPos const& p, float r, std::string_view type) {
    std::vector<Actor*> out;
    auto                level = ll::service::getLevel();
    if (!level) return out;
    Vec3 c = centerOf(p);
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || static_cast<int>(a->getDimensionId()) != p.dim) continue;
        Vec3 const& q = a->getPosition();
        if (std::abs(q.x - c.x) > r || std::abs(q.y - c.y) > r || std::abs(q.z - c.z) > r) continue;
        if (type.empty() || a->getTypeName() == type) out.push_back(a);
    }
    return out;
}

void removeNear(GlobalPos const& p, float r, std::string_view type) {
    for (auto* a : entitiesNear(p, r, type)) {
        if (!a->isPlayer()) a->remove();
    }
}

/// Places a cannon standing on the floor `dy` blocks above the test surface.
GlobalPos placeCannon(Context& ctx, GlobalPos base, int dy = 2) {
    GlobalPos pos = up(base, dy);
    ctx.require(cm::game::blockWith(cannon::kCannon) != nullptr, "the cannon block is registered (behavior pack loaded)");
    ctx.require(ctx.setBlock(pos, std::string(cannon::kCannon), {{"minecraft:block_face", kFaceUp}}), "place the cannon");
    return pos;
}

/// Restores the timing options the tests shorten.
struct FastCannon {
    int fuse, cooldown;
    FastCannon() {
        auto& c  = cm::config::mut<cannon::CannonConfig>();
        fuse     = c.fuseTime;
        cooldown = c.cooldown;
        c.fuseTime = 5;
        c.cooldown = 0;
    }
    ~FastCannon() {
        auto& c    = cm::config::mut<cannon::CannonConfig>();
        c.fuseTime = fuse;
        c.cooldown = cooldown;
    }
};

} // namespace

CM_REGISTER_SELFTEST("cannon.place_load_and_break", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1500);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto  pos = placeCannon(ctx, base);
    auto& c   = cannon::Cannons::get();
    co_await ctx.ticks(3);
    ctx.check(c.face(pos) == kFaceUp, "block_face reads back as up");
    auto* d = c.at(pos);
    ctx.require(d != nullptr, "the cannon has a record");
    ctx.check(c.insert(pos, cannon::makeItem("minecraft:gunpowder", 10)) == 10, "10 gunpowder go into the fuel slot");
    ctx.check(c.insert(pos, cannon::makeItem("minecraft:gunpowder", 64)) == 54, "fuel is capped at 64");
    ctx.check(c.insert(pos, cannon::makeItem(cannon::kCannonball, 3)) == 3, "3 cannonballs go into the projectile slot");
    ctx.check(c.insert(pos, cannon::makeItem("minecraft:arrow", 3)) == 0, "a different projectile does not fit");
    ctx.check(d->fuel == 64 && d->projectile.mCount == 3, "contents read back");
    ctx.check(!entitiesNear(pos, 0.6f, cannon::kBarrel).empty(), "a barrel entity stands on the cannon");
    c.setAim(pos, 400.0f, 120.0f, 9);
    ctx.check(std::abs(d->yaw - 40.0f) < 0.01f && d->pitch == 90.0f && d->power == 4, "aim is wrapped / clamped");

    // breaking the cannon drops its contents and removes the barrel (player out of pick-up range)
    ctx.player().teleport(centerOf(east(pos, 6)), pos.dim);
    co_await ctx.ticks(1);
    cm::game::clearBlock(*cannon::regionOf(pos.dim), cm::game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(!c.all().contains(pos), "the record is gone");
    ctx.check(entitiesNear(pos, 0.6f, cannon::kBarrel).empty(), "the barrel is gone");
    std::string seen;
    for (auto* a : entitiesNear(pos, 3.0f, "")) seen += a->getTypeName() + " ";
    ctx.info("entities after breaking: " + seen);
    auto drops = entitiesNear(pos, 3.0f, "minecraft:item").size();
    ctx.check(drops >= 2, "the contents were dropped (" + std::to_string(drops) + " stacks)");
    removeNear(pos, 3.0f, "minecraft:item");
});

CM_REGISTER_SELFTEST("cannon.interact_loads_from_hand", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1505);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto  pos = placeCannon(ctx, base);
    auto& p   = ctx.player();
    co_await ctx.ticks(2);
    int slot = p.getSelectedItemSlot();
    ctx.require(ctx.give(p, cannon::makeItem("minecraft:gunpowder", 5), slot) == slot, "give gunpowder to the selected slot");
    cannon::Cannons::get().interact(p, pos);
    auto* d = cannon::Cannons::get().at(pos);
    ctx.require(d != nullptr, "cannon record");
    ctx.check(d->fuel == 5, "the gunpowder was loaded");
    ctx.check(p.getSelectedItem().isNull(), "the hand is empty");
    ctx.give(p, cannon::makeItem("minecraft:snowball", 4), slot);
    cannon::Cannons::get().interact(p, pos);
    ctx.check(!d->projectile.isNull() && d->projectile.getTypeName() == "minecraft:snowball", "the snowballs were loaded");
});

CM_REGISTER_SELFTEST("cannon.cannonball_breaks_wall", [](Context& ctx) -> ll::coro::CoroTask<> {
    FastCannon fast;
    auto       base = ctx.testPos(1510);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto  pos = placeCannon(ctx, base);
    auto  dirt = east(pos, 4), stop = east(pos, 7);
    ctx.require(ctx.setBlock(dirt, "minecraft:dirt"), "dirt wall");
    ctx.require(ctx.setBlock(stop, "minecraft:obsidian"), "obsidian backstop");
    auto& c = cannon::Cannons::get();
    co_await ctx.ticks(2);
    c.insert(pos, cannon::makeItem("minecraft:gunpowder", 8));
    c.insert(pos, cannon::makeItem(cannon::kCannonball, 1));
    c.setAim(pos, -90.0f, 0.0f, 4); // east
    bool blocked = false;
    auto path    = c.predict(pos, &blocked);
    ctx.check(blocked && !path.empty() && std::abs(path.back().x - dirt.x) < 0.5, "the predicted path stops at the dirt wall");
    ctx.require(c.fireNow(pos), "fire");
    auto* d = c.at(pos);
    ctx.check(d && d->fuel == 4 && d->projectile.isNull(), "4 gunpowder and the ball were used");
    for (int i = 0; i < 60 && blockAt(dirt) == "minecraft:dirt"; ++i) co_await ctx.ticks(1);
    ctx.check(blockAt(dirt) != "minecraft:dirt", "the cannonball broke the dirt");
    co_await ctx.ticks(20);
    ctx.check(blockAt(stop) == "minecraft:obsidian", "obsidian stops the ball");
    removeNear(pos, 10.0f, "minecraft:item");
    removeNear(pos, 10.0f, cannon::kCannonball);
});

CM_REGISTER_SELFTEST("cannon.redstone_fires", [](Context& ctx) -> ll::coro::CoroTask<> {
    FastCannon fast;
    auto       base = ctx.testPos(1520);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto  pos = placeCannon(ctx, base);
    auto& c   = cannon::Cannons::get();
    co_await ctx.ticks(2);
    c.insert(pos, cannon::makeItem("minecraft:gunpowder", 1));
    c.insert(pos, cannon::makeItem("minecraft:snowball", 1));
    c.setAim(pos, 0.0f, 60.0f, 1);
    ctx.require(ctx.setBlock(east(pos, 1), "minecraft:redstone_block"), "power the cannon");
    auto* d = c.at(pos);
    for (int i = 0; i < 40 && d && d->fuel > 0; ++i) {
        co_await ctx.ticks(1);
        d = c.at(pos);
    }
    ctx.check(d && d->fuel == 0 && d->projectile.isNull(), "a redstone signal fired the cannon");
    co_await ctx.ticks(30);
    removeNear(pos, 12.0f, "minecraft:snowball");
});

CM_REGISTER_SELFTEST("cannon.bomb_breaks_weak_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1530);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto dirt  = up(base, 4);
    auto stone = east(dirt, 1);
    ctx.require(ctx.setBlock(dirt, "minecraft:dirt"), "dirt");
    ctx.require(ctx.setBlock(stone, "minecraft:stone"), "stone");
    auto* region = cannon::regionOf(dirt.dim);
    ctx.require(region != nullptr, "region");
    co_await ctx.ticks(1);
    ctx.info("blast resistance: dirt " + std::to_string(region->getBlock(cm::game::blockPos(dirt)).getBlockType().getExplosionResistance())
             + ", stone " + std::to_string(region->getBlock(cm::game::blockPos(stone)).getBlockType().getExplosionResistance()));
    // a thrown bomb falls on the dirt and explodes on contact
    Vec3 above = centerOf(dirt) + Vec3{0, 3.0f, 0};
    ctx.require(cannon::shootProjectile(*region, cannon::kBomb, above, Vec3{0, -0.6f, 0}) != nullptr, "spawn a bomb");
    for (int i = 0; i < 60 && blockAt(dirt) == "minecraft:dirt"; ++i) co_await ctx.ticks(1);
    ctx.check(blockAt(dirt) != "minecraft:dirt", "the bomb broke the dirt (weak block)");
    ctx.check(blockAt(stone) == "minecraft:stone", "the bomb did not break stone");
    removeNear(dirt, 6.0f, "minecraft:item");
});

CM_REGISTER_SELFTEST("cannon.blue_bomb_scatters_babies", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1540);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto dirt = up(base, 4);
    ctx.require(ctx.setBlock(dirt, "minecraft:dirt"), "dirt");
    cannon::explodeBomb(dirt.dim, cannon::kBombBlue, centerOf(dirt) + Vec3{0, 0.6f, 0}, 0);
    co_await ctx.ticks(1);
    int babies = static_cast<int>(entitiesNear(dirt, 4.0f, cannon::kBombBlueBaby).size());
    ctx.check(babies == cm::config::get<cannon::CannonConfig>().blueBombSplitCount, "the blue bomb scattered its baby bombs (" + std::to_string(babies) + ")");
    ctx.check(blockAt(dirt) == "minecraft:dirt", "the blue bomb does not break blocks");
    for (int i = 0; i < 80 && !entitiesNear(dirt, 12.0f, cannon::kBombBlueBaby).empty(); ++i) co_await ctx.ticks(1);
    ctx.check(entitiesNear(dirt, 12.0f, cannon::kBombBlueBaby).empty(), "the baby bombs went off");
    removeNear(dirt, 12.0f, cannon::kBombBlueBaby);
});

CM_REGISTER_SELFTEST("cannon.spiky_bomb_shrapnel", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1550);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto  spot   = up(base, 1);
    auto* region = cannon::regionOf(spot.dim);
    auto  level  = ll::service::getLevel();
    ctx.require(region && level, "level");
    auto* pig = level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:pig"), nullptr, centerOf(spot) - Vec3{0, 0.5f, 0}, false, true, false);
    ctx.require(pig != nullptr, "spawn a pig");
    int before = pig->getHealth();
    cannon::explodeBomb(spot.dim, cannon::kBombSpiky, centerOf(spot) + Vec3{3.5f, 0.5f, 0}, 0);
    co_await ctx.ticks(2);
    auto pigs = entitiesNear(spot, 3.0f, "minecraft:pig");
    ctx.check(pigs.empty() || pigs.front()->getHealth() < before, "the shrapnel hurt the pig");
    removeNear(spot, 8.0f, "minecraft:pig");
    removeNear(spot, 8.0f, "minecraft:item");
});

CM_REGISTER_SELFTEST("cannon.persistence", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1560);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto  pos = placeCannon(ctx, base);
    auto& c   = cannon::Cannons::get();
    co_await ctx.ticks(2);
    c.insert(pos, cannon::makeItem("minecraft:gunpowder", 7));
    c.insert(pos, cannon::makeItem(cannon::kBomb, 2));
    c.setAim(pos, 33.0f, 21.0f, 3);
    c.saveWorld();
    ctx.check(cm::game::db::load("cm:cannon:c:" + pos.key()).has_value(), "the cannon is stored in the world DB");
    c.loadWorld(); // drop the memory state and read the DB again
    auto* d = c.at(pos);
    ctx.require(d != nullptr, "record after reload");
    ctx.check(d->fuel == 7 && d->power == 3, "fuel and power survive");
    ctx.check(std::abs(d->yaw - 33.0f) < 0.01f && std::abs(d->pitch - 21.0f) < 0.01f, "aim survives");
    ctx.check(!d->projectile.isNull() && d->projectile.getTypeName() == cannon::kBomb && d->projectile.mCount == 2, "projectile survives");
});

CM_REGISTER_SELFTEST("cannon.not_loaded_does_not_fire", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1570);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto  pos = placeCannon(ctx, base);
    auto& c   = cannon::Cannons::get();
    co_await ctx.ticks(2);
    ctx.check(!c.ignite(pos, nullptr), "an empty cannon does not light");
    c.insert(pos, cannon::makeItem(cannon::kCannonball, 1));
    c.setAim(pos, 0.0f, 0.0f, 2);
    c.insert(pos, cannon::makeItem("minecraft:gunpowder", 1));
    ctx.check(!c.ignite(pos, nullptr), "1 gunpowder is not enough for power 2");
});

CM_REGISTER_SELFTEST("cannon.red_merchant", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1580);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p        = ctx.player();
    Actor* merchant = cannon::spawnRedMerchantNear(p);
    ctx.require(merchant != nullptr, "a red merchant spawns near the player");
    ctx.check(merchant->getTypeName() == cannon::kRedMerchant, "it is a red merchant");
    ctx.check(merchant->getPosition().distanceTo(p.getPosition()) >= 15.0, "16-32 blocks away");
    co_await ctx.ticks(2);
    merchant->remove();
});

