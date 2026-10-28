// Fodder: a layered feed block. Layers settle downwards, hoes trim them and farm animals walk to
// it, eat a layer and fall in love (Java EatFodderGoal).

#include "feature/trap/Trap.h"

#include <cmath>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/BreedableComponent.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::trap::fodder {
namespace {

constexpr int   kSearchRange    = 8;
constexpr int   kVerticalRange  = 2;
constexpr int   kBreakTime      = 30;
constexpr float kAcceptDistance = 1.5f;

enum class Phase { Idle, Seeking, Eating };

struct Goal {
    Phase    phase     = Phase::Idle;
    uint64_t nextStart = 0;
    int      dim       = 0;
    BlockPos target{0, 0, 0};
    int      tryTicks = 0;
    int      eatTicks = 0;
};

std::unordered_map<int64_t, Goal> gGoals;
std::mt19937                      gRandom{std::random_device{}()};

int randomCooldown() { return std::uniform_int_distribution<int>(800, 1200)(gRandom); }

bool validTarget(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) return false;
    return isBlock(region.getBlock(pos), kFodderId) && region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z}).isAir();
}

std::optional<BlockPos> findFodder(BlockSource& region, Vec3 const& from) {
    BlockPos origin = game::blockPosOf(from);
    std::optional<BlockPos> best;
    float                   bestD = 1e9f;
    for (int dy = -kVerticalRange; dy <= kVerticalRange; ++dy) {
        for (int dx = -kSearchRange; dx <= kSearchRange; ++dx) {
            for (int dz = -kSearchRange; dz <= kSearchRange; ++dz) {
                BlockPos p{origin.x + dx, origin.y + dy, origin.z + dz};
                if (!validTarget(region, p)) continue;
                float d = static_cast<float>(dx * dx + dy * dy + dz * dz);
                if (d < bestD) {
                    bestD = d;
                    best  = p;
                }
            }
        }
    }
    return best;
}

bool eligible(Actor& actor) {
    if (!actor.isAlive() || !actor.hasCategory(ActorCategory::Mob) || actor.isPlayer() || cage::isDisplay(actor)) return false;
    auto const& eaters = config().fodder.eaters;
    if (std::find(eaters.begin(), eaters.end(), actor.getTypeName()) == eaters.end()) return false;
    if (actor.isInLove()) return false;
    if (auto breed = actor.getEntityContext().tryGetComponent<BreedableComponent>()) {
        if (breed->mBreedCooldown > 0) return false; // Java: getAge() > 0
    }
    return true;
}

Vec3 aboveCenter(BlockPos const& pos) {
    return Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 1.5f, static_cast<float>(pos.z) + 0.5f};
}

void moveTo(Mob& mob, BlockPos const& target) {
    auto nav = mob.getEntityContext().tryGetComponent<NavigationComponent>();
    if (!nav || !nav->mNavigation.get()) return;
    Vec3 dest{static_cast<float>(target.x) + 0.5f, static_cast<float>(target.y) + 1.f, static_cast<float>(target.z) + 0.5f};
    nav->mNavigation.get()->moveTo(*nav, mob, dest, 1.f);
}

void stopMoving(Mob& mob) {
    auto nav = mob.getEntityContext().tryGetComponent<NavigationComponent>();
    if (!nav || !nav->mNavigation.get()) return;
    nav->mNavigation.get()->stop(*nav, mob);
}

void tickGoal(Mob& mob, Goal& goal, uint64_t now) {
    auto& region = mob.getDimensionBlockSource();
    switch (goal.phase) {
    case Phase::Idle: {
        if (now < goal.nextStart || !eligible(mob)) return;
        if (auto found = findFodder(region, mob.getPosition())) {
            goal.phase     = Phase::Seeking;
            goal.target    = *found;
            goal.dim       = static_cast<int>(mob.getDimensionId());
            goal.tryTicks  = 0;
            goal.eatTicks  = 0;
            goal.nextStart = now + 600; // cooldown between attempts while fodder is around
            moveTo(mob, goal.target);
        } else {
            goal.nextStart = now + static_cast<uint64_t>(randomCooldown());
        }
        return;
    }
    case Phase::Seeking:
    case Phase::Eating: {
        if (!validTarget(region, goal.target) || goal.tryTicks > 200) {
            goal.phase = Phase::Idle;
            return;
        }
        bool reached = (mob.getPosition() - aboveCenter(goal.target)).length() < kAcceptDistance
                    || (mob.getFeetPos() - aboveCenter(goal.target)).length() < kAcceptDistance;
        if (!reached) {
            ++goal.tryTicks;
            if (goal.phase == Phase::Seeking && goal.tryTicks % 20 == 1) moveTo(mob, goal.target);
            return;
        }
        if (goal.phase == Phase::Seeking) {
            goal.phase = Phase::Eating;
            stopMoving(mob);
        }
        ++goal.eatTicks;
        if (goal.eatTicks % 2 == 0) blockParticles(region, goal.target, region.getBlock(goal.target));
        if (goal.eatTicks > kBreakTime) {
            eat(mob, region, goal.target);
            goal.phase     = Phase::Idle;
            goal.nextStart = now + static_cast<uint64_t>(randomCooldown());
        }
        return;
    }
    }
}

/// Animal::setInLove(null): the love timer of the breedable component and the in-love flag.
void fallInLove(Mob& animal) {
    animal.setInLove(nullptr);
    if (animal.isInLove()) return;
    if (auto breed = animal.getEntityContext().tryGetComponent<BreedableComponent>()) {
        breed->mLoveTimer = 600;
        SynchedActorDataAccess::setActorFlag(animal.getEntityContext(), ActorFlags::Inlove, true);
    }
}

} // namespace

int layersAt(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (!isBlock(block, kFodderId)) return 0;
    return std::clamp(kMaxLayers - stateOf(block, kEatenState, 0), 1, kMaxLayers);
}

void setLayers(BlockSource& region, BlockPos const& pos, int layers) {
    if (layers <= 0) {
        game::clearBlock(region, pos);
        return;
    }
    if (auto const* block = game::blockWith(kFodderId, {{kEatenState, kMaxLayers - std::min(layers, kMaxLayers)}})) {
        game::setBlock(region, pos, *block);
    }
}

void settle(BlockSource& region, BlockPos const& pos) {
    // move layers down the column: each fodder block on a non-full fodder block fills it
    for (int y = pos.y - 2; y <= pos.y + 1; ++y) {
        BlockPos lower{pos.x, y, pos.z};
        BlockPos upper{pos.x, y + 1, pos.z};
        int      lo = layersAt(region, lower);
        int      up = layersAt(region, upper);
        if (lo == 0 || up == 0 || lo >= kMaxLayers) continue;
        int moved = std::min(kMaxLayers - lo, up);
        setLayers(region, lower, lo + moved);
        setLayers(region, upper, up - moved);
    }
}

void useHoe(Player& player, BlockPos const& pos) {
    auto& region = player.getDimensionBlockSource();
    int   layers = layersAt(region, pos);
    if (layers == 0) return;
    blockParticles(region, pos, region.getBlock(pos));
    setLayers(region, pos, layers - 1);
    playSound(region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, "use.grass", 1.f, 1.f);
    if (!isCreative(player)) {
        ItemStack held = player.getSelectedItem();
        if (held.isDamageableItem()) {
            held.hurtAndBreak(1, &player);
            player.setSelectedItem(held);
            player.sendInventory(false);
        }
    }
    settle(region, pos);
}

void eat(Mob& animal, BlockSource& region, BlockPos const& pos) {
    int layers = layersAt(region, pos);
    if (layers == 0) return;
    blockParticles(region, pos, region.getBlock(pos));
    setLayers(region, pos, layers - 1);
    particle(region, animal.getPosition(), "minecraft:villager_happy");
    if (!animal.isBaby()) fallInLove(animal);
    if (animal.getTypeName() == "minecraft:sheep") {
        // Sheep::ate(): regrow the wool (the vanilla grass-eating event)
        std::string tag = "cm_trap_ate_" + std::to_string(uid(animal) & 0xffffff);
        animal.addTag(tag);
        runCommand("event entity @e[tag=" + tag + "] minecraft:on_eat_block");
        runCommand("tag @e[tag=" + tag + "] remove " + tag);
    }
    log().info("trap: {} ate fodder at {} {} {}", animal.getTypeName(), pos.x, pos.y, pos.z);
}

void forceSearch(Actor& animal) {
    auto& goal     = gGoals[uid(animal)];
    goal.phase     = Phase::Idle;
    goal.nextStart = 0;
}

void onWorldClosed() { gGoals.clear(); }

void tick(Level& level) {
    if (!config().fodder.enabled) return;
    uint64_t    now    = currentTick();
    auto const& eaters = config().fodder.eaters;
    if (now % 20 == 0) {
        // once a second: track the animals that eat fodder and run their idle goals
        std::unordered_set<int64_t> present;
        for (Actor* actor : level.getRuntimeActorList()) {
            if (!actor || actor->isPlayer() || !actor->hasCategory(ActorCategory::Mob)) continue;
            if (std::find(eaters.begin(), eaters.end(), actor->getTypeName()) == eaters.end()) continue;
            int64_t id = uid(*actor);
            present.insert(id);
            auto& goal = gGoals[id];
            if (goal.phase == Phase::Idle && actor->isAlive()) tickGoal(static_cast<Mob&>(*actor), goal, now);
        }
        std::erase_if(gGoals, [&](auto const& entry) { return !present.contains(entry.first); });
    }
    // every tick: animals walking to fodder or eating it
    for (auto& [id, goal] : gGoals) {
        if (goal.phase == Phase::Idle) continue;
        Actor* actor = actorByUid(id);
        if (!actor || !actor->isAlive()) {
            goal.phase = Phase::Idle;
            continue;
        }
        tickGoal(static_cast<Mob&>(*actor), goal, now);
    }
}
} // namespace cm::feature::trap::fodder
