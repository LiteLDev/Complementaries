// Globe and sepia globe: spinning, comparator output, redstone, shears, special names, world map variant.

#include <algorithm>
#include <cctype>
#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockSupportType.h"

#include "feature/instrument/Instrument.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::instrument::globe {

namespace {
constexpr char const* kSeedKey = "cm:instrument:globe_seed";

std::uint64_t gSeed       = 0;
bool          gSeedLoaded = false;

std::uint64_t mix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

std::uint64_t worldSeed() {
    auto level = ll::service::getLevel();
    return level ? level->getLevelSeed64().mValue : 0;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool matches(std::string const& name, std::initializer_list<char const*> words, std::string_view key, std::string_view fallback) {
    for (auto const* w : words) {
        if (name == w) return true;
    }
    auto tr = lower(std::string(cm::translate(key, fallback)));
    return !tr.empty() && name == tr;
}

int faceRot(Record const& rec) { return (3 - (rec.rotation & 3)) * 90; }

/// Java's weighted random: values near 0 are more likely (sum of two uniforms folded).
float weighted(float max) { return static_cast<float>(std::abs(random01() - random01())) * max; }
} // namespace

std::uint64_t seed() {
    if (!gSeedLoaded) loadSeed();
    return gSeed;
}

void loadSeed() {
    gSeed = worldSeed();
    if (auto j = game::db::load(kSeedKey)) {
        try {
            gSeed = std::stoull(j->value("seed", std::string("0")));
        } catch (...) {}
    }
    gSeedLoaded = true;
}

void setSeed(std::uint64_t s, bool custom) {
    if (custom) {
        gSeed = s;
        game::db::save(kSeedKey, {{"seed", std::to_string(s)}});
    } else {
        gSeed = worldSeed();
        game::db::erase(kSeedKey);
    }
    gSeedLoaded = true;
}

void resetSeedCache() { gSeedLoaded = false; }

int mapVariant() { return static_cast<int>(mix(seed()) % 16); }

int skinFor(Record const& rec, int dim) {
    if (rec.sheared) return 14;
    int  palette = !config().globeRandomWorld ? 3 : dim == 1 ? 1 : dim == 2 ? 2 : 0;
    auto name    = lower(rec.name);
    if (!name.empty()) {
        if (matches(name, {"flat", "flat earth"}, "globe.supplementaries.flat", "Flat")) return 4 + palette;
        if (matches(name, {"moon", "luna", "selene", "cynthia"}, "globe.supplementaries.moon", "Moon")) return 12;
        if (matches(name, {"earth", "terra", "gaia", "gaea", "tierra", "tellus", "terre"}, "globe.supplementaries.earth", "Earth"))
            return 3;
        if (matches(name, {"sun", "sol", "helios"}, "globe.supplementaries.sun", "Sun")) return 13;
        if (matches(name, {"round", "sphere", "spherical"}, "globe.supplementaries.round", "Round")) return 8 + palette;
    }
    return palette;
}

void spin(Record& rec, BlockSource& region, BlockPos const& pos) {
    int face     = (faceRot(rec) - 90 + 360) % 360;
    rec.yaw     += 450.f;
    rec.rotation = 3 - face / 90;
    rec.dirty    = true;
    playSound(region, pos, "complementaries.block.globe.spin", 0.65f, 0.9f + weighted(0.2f));
}

bool use(Record& rec, Player& player, BlockSource& region, BlockPos const& pos, bool shears) {
    if (shears) {
        rec.sheared = !rec.sheared;
        rec.skin    = -1;
        rec.dirty   = true;
        playSound(region, pos, "mob.sheep.shear", 1.f, 1.f);
        log().info("instrument: globe at {} {}", rec.pos.key(), rec.sheared ? "sheared" : "unsheared");
        return true;
    }
    spin(rec, region, pos);
    if (config().globeShowCoordinates) {
        std::string x = std::to_string(pos.x), z = std::to_string(pos.z);
        if (!isNaturalDimension(region) && !config().worksInUnnaturalDimensions) {
            x = "§k" + x + "§r";
            z = "§k" + z + "§r";
        }
        actionBar(player, trFor(player, "message.supplementaries.compass", "X: {}, Z: {}", x, z));
    }
    log().info("instrument: globe at {} spun by {}", rec.pos.key(), player.getRealName());
    return true;
}

void tick(Record& rec, BlockSource& region, BlockPos const& pos, Block const& block) {
    // needs a sturdy block below
    BlockPos below{pos.x, pos.y - 1, pos.z};
    if (!region.getBlock(below).canProvideSupport(1, BlockSupportType::Center)) {
        BlockChangeContext ctx{};
        region.getLevel().destroyBlock(region, pos, true, ctx);
        return;
    }
    // redstone: a rising edge spins it
    bool powered = redstoneInput(region, pos) > 0;
    if (powered != rec.powered) {
        rec.powered = powered;
        rec.dirty   = true;
        if (powered) {
            spin(rec, region, pos);
            log().info("instrument: globe at {} spun by redstone", rec.pos.key());
        }
    }
    // spin decay
    if (rec.yaw != 0.f) {
        rec.yaw = rec.yaw < 0.f ? 0.f : rec.yaw * 0.94f - 0.7f;
    }
    float display = static_cast<float>(faceRot(rec)) + std::max(rec.yaw, 0.f);
    int   angle   = static_cast<int>(std::floor(display / 22.5f + 0.5f)) % 16;
    if (rec.skin < 0 || rec.skinRandom != config().globeRandomWorld) {
        rec.skin       = skinFor(rec, rec.pos.dim);
        rec.skinRandom = config().globeRandomWorld;
    }
    auto next = withStates(
        block,
        {{"complementaries:angle", angle}, {"complementaries:skin", rec.skin}, {"complementaries:map", mapVariant()}}
    );
    apply(region, pos, block, next);
    int signal = rec.yaw != 0.f ? 15 : faceRot(rec) / 90 + 1;
    if (signal != rec.signal) {
        rec.signal = signal;
        updateComparators(region, pos);
    }
}

} // namespace cm::feature::instrument::globe
