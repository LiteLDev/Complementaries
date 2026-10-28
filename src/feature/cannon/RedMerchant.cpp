// U15 `cannon`: natural spawning of the red merchant (the entity itself is data-driven:
// assets/pack/bp/entities/red_merchant.json).

#include <cmath>
#include <random>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"

#include "feature/cannon/Cannon.h"
#include "feature/cannon/CannonConfig.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace cm::feature::cannon {

namespace {

constexpr char const* kMerchantKey   = "cm:cannon:red_merchant";
constexpr uint64_t    kSpawnInterval = 1200;  ///< ticks between spawn attempts
constexpr float       kSpawnChance   = 0.05f; ///< per attempt, at night, times the multiplier

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

/// Ticks a red merchant lives at most (its despawn timer is 20-30 minutes, then it leaves once no
/// player is near).
constexpr uint64_t kMaxLifetime = 30 * 60 * 20 + 6000;

uint64_t serverTick(Level const& level) { return level.getCurrentServerTick().tickID; }

/// The last spawned merchant is still around: loaded, or spawned recently enough that it may just
/// be in an unloaded chunk.
bool merchantAlive() {
    auto level = ll::service::getLevel();
    if (!level || !game::db::available()) return false;
    auto j = game::db::load(kMerchantKey);
    if (!j) return false;
    auto id = j->value("id", static_cast<int64_t>(0));
    if (id && level->fetchEntity(ActorUniqueID{id}, false) != nullptr) return true;
    auto     at  = j->value("tick", static_cast<uint64_t>(0));
    uint64_t now = serverTick(*level);
    return at <= now && now - at < kMaxLifetime;
}

} // namespace

Actor* spawnRedMerchantNear(Player& player) {
    auto& region = player.getDimensionBlockSource();
    auto  level  = ll::service::getLevel();
    if (!level) return nullptr;
    std::uniform_real_distribution<double> angle(0.0, 2.0 * math::kPi);
    std::uniform_real_distribution<double> dist(16.0, 32.0);
    Vec3 const                             at = player.getPosition();
    for (int attempt = 0; attempt < 10; ++attempt) {
        double   a = angle(rng()), r = dist(rng());
        int      x = static_cast<int>(std::floor(at.x + std::cos(a) * r));
        int      z = static_cast<int>(std::floor(at.z + std::sin(a) * r));
        BlockPos column{x, static_cast<int>(at.y), z};
        if (!region.hasChunksAt(column, 0, false)) continue;
        int y = region.getAboveTopSolidBlock(x, z, false, false);
        if (y <= region.getMinHeight() || y >= region.getMaxHeight() - 2) continue;
        BlockPos feet{x, y, z};
        auto const& ground = region.getBlock(BlockPos{x, y - 1, z});
        if (ground.isAir() || !ground.mCachedComponentData->mIsSolid) continue;
        if (!region.getBlock(feet).isAir() || !region.getBlock(BlockPos{x, y + 1, z}).isAir()) continue;
        Vec3 pos{x + 0.5f, static_cast<float>(y), z + 0.5f};
        auto* mob = level->getSpawner().spawnMob(region, ActorDefinitionIdentifier(std::string(kRedMerchant)), nullptr, pos, false, true, false);
        if (!mob) continue;
        if (game::db::available()) game::db::save(kMerchantKey, {{"id", mob->getOrCreateUniqueID().rawID}, {"tick", serverTick(*level)}});
        log().info("Red merchant spawned at {} {} {}", x, y, z);
        return mob;
    }
    return nullptr;
}

void tickRedMerchant(uint64_t tick) {
    auto const& c = config::get<CannonConfig>();
    if (c.redMerchantSpawnMultiplier <= 0.0f || tick % kSpawnInterval != 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    int time = level->getTime() % 24000;
    if (time < 13000 || time > 23000) return; // nights only
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    if (roll(rng()) >= kSpawnChance * c.redMerchantSpawnMultiplier) return;
    if (merchantAlive()) return;
    std::vector<Player*> players;
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == 0 && !p.isSimulatedPlayer()) players.push_back(&p);
        return true;
    });
    if (players.empty()) return;
    std::uniform_int_distribution<size_t> pick(0, players.size() - 1);
    spawnRedMerchantNear(*players[pick(rng())]);
}

} // namespace cm::feature::cannon
