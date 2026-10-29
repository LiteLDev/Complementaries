#pragma once

// U9 lighting: sconces, candle holders, stone lamps, wall lanterns (docs/spec/lighting.md).
//
// Blocks, light levels, models and drops are data driven (BP/RP). This module adds the behaviour:
// lighting / extinguishing, adding candles, the sconce lever, wall lantern placement and swing,
// water and projectiles, and the flame / smoke particles (sent by the server, so vanilla clients
// see them too).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/lighting/LightBlocks.h"

class Actor;
class BlockPos;
class BlockSource;
class BlockType;
class HitResult;
class Player;
class Vec3;

namespace ll::event {
class ListenerBase;
}

namespace cm::feature::lighting {

class Lighting {
public:
    static Lighting& get();

    void install();
    void uninstall();

    // ---- actions (server thread; they edit the world at once: never call them from inside a
    //      cancelled interaction event, use `defer`) --------------------------------------------
    /// Lights / puts out a sconce or candle holder. False if nothing changed (already in that
    /// state, not lightable, or waterlogged when lighting).
    static bool setLit(BlockSource& region, BlockPos const& pos, bool lit, bool effects = true);
    /// One more candle on a candle holder (max 4). False if full or not a candle holder.
    static bool addCandle(BlockSource& region, BlockPos const& pos);
    /// Toggles a sconce lever. False if `pos` holds none.
    static bool toggleLever(BlockSource& region, BlockPos const& pos);
    /// Hangs lantern `lantern` (index into `kLanterns`) on face `facing` (2..5) of the block at
    /// `support`. False if the spot is taken or the wall can't hold it.
    static bool placeWallLantern(BlockSource& region, BlockPos const& support, int facing, int lantern);
    /// The block at `support` can hold a wall lantern / sconce on its face `facing`.
    [[nodiscard]] static bool canHangOn(BlockSource& region, BlockPos const& support, int facing);

    /// Gives a wall lantern a push (degrees per tick, signed). Used by entity collisions.
    void push(int dim, BlockPos const& pos, float impulse);
    [[nodiscard]] int swinging() const { return static_cast<int>(mSwings.size()); }
    /// Number of particle effects sent since the world loaded (selftests).
    [[nodiscard]] std::uint64_t particlesSent() const { return mParticles; }

    /// Projectile hit (from the ProjectileComponent hook).
    void onProjectileHit(Actor const& projectile, HitResult const& hit);
    /// The decision part of `onProjectileHit`: a burning projectile lights the block it hit, a
    /// splash water potion puts out everything within 1 block of `impact`.
    void onProjectileHit(int dim, BlockPos const& hitBlock, bool hitTile, Vec3 const& impact, bool burning, bool water);

    /// Runs `fn` on the next server tick.
    void defer(std::function<void()> fn) { mPending.push_back(std::move(fn)); }

    /// True on the thread that runs the server tick.
    [[nodiscard]] bool onServerThread() const { return std::this_thread::get_id() == mServerThread; }

    /// Kind of a block type of this unit (Kind::None for anything else). Cheap.
    [[nodiscard]] BlockInfo infoOf(BlockType const& type);

private:
    struct Swing {
        float angle = 0.f; ///< degrees
        float vel   = 0.f; ///< degrees per tick
        int   frame = 0;
    };

    void tick();
    void tickSwings();
    void tickCollisions();
    void tickNearby(); ///< particles + wall lantern discovery around players
    void rebuildTypes();
    void reset();

    std::vector<std::shared_ptr<ll::event::ListenerBase>> mListeners;
    std::vector<std::function<void()>>                    mPending;
    std::map<core::GlobalPos, Swing>                      mSwings;
    std::unordered_map<BlockType const*, BlockInfo>       mTypes;
    std::unordered_map<std::int64_t, std::tuple<std::uint64_t, int, int, int>> mLastUse; ///< player -> tick, pos
    std::set<core::GlobalPos>                             mLanterns; ///< wall lanterns near players
    std::thread::id                                       mServerThread{};
    bool                                                  mTypesBuilt = false;
    std::uint64_t                                         mTick      = 0;
    std::uint64_t                                         mParticles = 0;
    bool                                                  mInstalled = false;
};

} // namespace cm::feature::lighting
