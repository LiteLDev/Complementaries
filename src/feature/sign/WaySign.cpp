// Way signs (world generation) and compass targets.

#include <cmath>
#include <random>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/LodestoneCompassItem.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/WorldGenerator.h"
#include "mc/world/level/position_trackingdb/PositionTrackingDBServer.h"
#include "mc/world/level/position_trackingdb/ResultCode.h"
#include "mc/world/level/position_trackingdb/TrackingRecord.h"

#include "feature/sign/Logic.h"
#include "feature/sign/SignConfig.h"
#include "feature/sign/SignGame.h"
#include "feature/sign/SignModule.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::sign {

std::optional<std::pair<int, int>> lodestoneOf(ItemStack const& compass, int dim) {
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    auto* trackers = level->getPositionTrackerDBServer();
    if (!trackers) return std::nullopt;
    // the compass stores its tracker handle in its user data ("trackingHandle")
    auto const* tag = compass.mUserData.get();
    if (!tag || !tag->contains("trackingHandle")) return std::nullopt;
    auto const& handle = tag->at("trackingHandle");
    if (!handle.is_number_integer()) return std::nullopt;
    PositionTrackingId id;
    id.mRawId = static_cast<uint>(static_cast<std::int64_t>(handle));
    PositionTrackingDB::TrackingRecord* record = nullptr;
    if (trackers->findTracker(id, &record) != PositionTrackingDB::ResultCode::Ok || !record) return std::nullopt;
    if (record->mStatus == PositionTrackingDB::TrackingRecord::RecordStatus::Deleted) return std::nullopt;
    if (static_cast<int>(*record->mDimensionType) != dim) return std::nullopt;
    return std::pair{record->mPosition->x, record->mPosition->z};
}

namespace {
constexpr int kCell = 128;

struct StructureType {
    std::string_view feature;
    std::string_view key;
    std::string_view english;
};
constexpr StructureType kVillage{"village", "cm.sign.way_sign.village", "Village"};
// Structures with a regular spacing grid (cheap to locate); woodland mansions are left out on
// purpose: their search can scan a huge area and stall the server thread.
constexpr std::array<StructureType, 3> kOthers = {{
    {"pillager_outpost", "cm.sign.way_sign.pillager_outpost", "Pillager Outpost"},
    {"trail_ruins", "cm.sign.way_sign.trail_ruins", "Trail Ruins"},
    {"trial_chambers", "cm.sign.way_sign.trial_chambers", "Trial Chambers"},
}};

WorldGenerator* generatorOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto dimension = level->getDimension(static_cast<DimensionType>(dim)).lock();
    if (!dimension) return nullptr;
    return dimension->mWorldGenerator;
}

std::optional<BlockPos> findStructure(int dim, std::string_view feature, BlockPos const& origin) {
    auto* gen = generatorOf(dim);
    if (!gen) return std::nullopt;
    for (std::string name : {std::string(feature), "minecraft:" + std::string(feature)}) {
        BlockPos out{0, 0, 0};
        try {
            if (gen->findNearestStructureFeature(HashedString(name), origin, out, false, std::nullopt)) return out;
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

double dist2d(BlockPos const& a, int x, int z) {
    double dx = a.x - x;
    double dz = a.z - z;
    return std::sqrt(dx * dx + dz * dz);
}

bool goodGround(std::string_view id) {
    for (std::string_view g : {"grass_block", "dirt", "coarse_dirt", "podzol", "sand", "red_sand", "gravel", "grass_path", "dirt_path"}) {
        if (id == "minecraft:" + std::string(g)) return true;
    }
    return false;
}

/// A surface spot near (x, z) for a way sign (the fence position), if any.
std::optional<BlockPos> surfaceSpot(BlockSource& region, int x, int z) {
    for (int r = 0; r <= 6; r += 2) {
        for (int dx = -r; dx <= r; dx += 2) {
            for (int dz = -r; dz <= r; dz += 2) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                BlockPos col{x + dx, 0, z + dz};
                if (!loadedAt(region, col)) continue;
                int      y = region.getAboveTopSolidBlock(col.x, col.z, false, false);
                BlockPos pos{col.x, y, col.z};
                if (!goodGround(game::blockIdAt(region, BlockPos{pos.x, pos.y - 1, pos.z}))) continue;
                if (!game::replaceable(region, pos) || !game::replaceable(region, BlockPos{pos.x, pos.y + 1, pos.z})) continue;
                if (game::blockIdAt(region, pos).find("water") != std::string::npos) continue;
                return pos;
            }
        }
    }
    return std::nullopt;
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

/// Tries to build a way sign around (x, z) of the overworld. `force`: ignore distance limits.
bool tryWaySign(int x, int z, bool force, std::string* failure) {
    auto const& c       = config::get<SignConfig>();
    auto*       region  = regionOf(0);
    if (!region) return false;
    auto village = findStructure(0, kVillage.feature, BlockPos{x, 64, z});
    if (!village) {
        if (failure) *failure = tr("cm.sign.way_sign.none", "No village nearby");
        return false;
    }
    double d = dist2d(*village, x, z);
    if (!force && (d < c.waySignMinDistance || d > c.waySignMaxDistance)) return false;
    auto spot = surfaceSpot(*region, x, z);
    if (!spot) {
        if (failure) *failure = tr("cm.sign.way_sign.none", "No village nearby");
        return false;
    }
    logic::WayTarget first{tr(kVillage.key, kVillage.english), village->x, village->z};
    std::optional<logic::WayTarget> second;
    double best = c.waySignMaxDistance * 2.0;
    for (auto const& t : kOthers) {
        auto pos = findStructure(0, t.feature, BlockPos{x, 64, z});
        if (!pos) continue;
        double dd = dist2d(*pos, x, z);
        if (dd < best && dd > 16) {
            best   = dd;
            second = logic::WayTarget{tr(t.key, t.english), pos->x, pos->z};
        }
    }
    return logic::buildWaySign(0, spot->x, spot->y, spot->z, first, second);
}

std::int64_t floorDiv(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
} // namespace

void tickWaySigns() {
    auto const& c = config::get<SignConfig>();
    if (!c.waySigns || !game::db::available()) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<std::pair<int, int>> candidates;
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != 0) return true;
        auto pos   = p.getPosition();
        auto block = game::blockPosOf(pos);
        auto cx0   = floorDiv(block.x, kCell);
        auto cz0   = floorDiv(block.z, kCell);
        for (auto cx = cx0 - 1; cx <= cx0 + 1; ++cx) {
            for (auto cz = cz0 - 1; cz <= cz0 + 1; ++cz) {
                int    mx = static_cast<int>(cx * kCell + kCell / 2);
                int    mz = static_cast<int>(cz * kCell + kCell / 2);
                double dx = mx - pos.x, dz = mz - pos.z;
                double d  = std::sqrt(dx * dx + dz * dz);
                if (d >= 48 && d <= 96) candidates.emplace_back(static_cast<int>(cx), static_cast<int>(cz));
            }
        }
        return true;
    });
    auto* region = regionOf(0);
    if (!region) return;
    for (auto [cx, cz] : candidates) {
        auto key = "cm:sign:wg:" + std::to_string(cx) + ":" + std::to_string(cz);
        if (game::db::load(key)) continue;
        int mx = cx * kCell + kCell / 2, mz = cz * kCell + kCell / 2;
        if (!loadedAt(*region, BlockPos{mx, 0, mz})) continue;
        game::db::save(key, true);
        if (std::uniform_real_distribution<double>(0, 1)(rng()) >= c.waySignChance) continue;
        tryWaySign(mx, mz, false, nullptr);
        return; // one cell per step
    }
}

} // namespace cm::feature::sign

CM_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("waysign").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!cm::commands::isAdmin(origin)) return output.error(cm::tr("cm.commands.need_op", "You need operator permission."));
        auto* player = cm::commands::playerOf(origin);
        if (!player) return output.error("Run this as a player.");
        if (static_cast<int>(player->getDimensionId()) != 0) {
            return output.error(cm::trFor(*player, "cm.sign.way_sign.none", "No village nearby"));
        }
        auto        pos = cm::game::blockPosOf(player->getPosition());
        std::string failure;
        if (cm::feature::sign::tryWaySign(pos.x, pos.z, true, &failure)) {
            output.success(cm::trFor(*player, "cm.sign.way_sign.placed", "Way sign placed at {}", pos.toString()));
        } else {
            output.error(failure.empty() ? cm::trFor(*player, "cm.sign.way_sign.none", "No village nearby") : failure);
        }
    });
});
