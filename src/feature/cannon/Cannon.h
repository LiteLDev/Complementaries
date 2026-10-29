#pragma once

// U15 `cannon`: the cannon block, cannonballs, bombs and the red merchant.
// Internal API shared by the unit's .cpp files and its selftests.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core/GlobalPos.h"
#include "feature/cannon/CannonMath.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/item/ItemStack.h"

class Actor;
class BlockSource;
class BlockPos;
class Player;

namespace cm::feature::cannon {

inline constexpr std::string_view kCannon      = "complementaries:cannon";
inline constexpr std::string_view kBarrel      = "complementaries:cannon_barrel";
inline constexpr std::string_view kCannonball  = "complementaries:cannonball";
inline constexpr std::string_view kBomb        = "complementaries:bomb";
inline constexpr std::string_view kBombBlue    = "complementaries:bomb_blue";
inline constexpr std::string_view kBombSpiky   = "complementaries:bomb_spiky";
inline constexpr std::string_view kBombBlueBaby = "complementaries:bomb_blue_baby";
inline constexpr std::string_view kRedMerchant = "complementaries:red_merchant";
inline constexpr int              kMaxFuel     = 64;

/// One cannon block (contents + aim). Stored in the world DB under `cm:cannon:c:<pos key>`.
struct CannonData {
    int       fuel  = 0;
    ItemStack projectile; ///< null = empty
    float     yaw   = 0.0f;
    float     pitch = 30.0f;
    int       power = 1;
    bool      powered = false; ///< last redstone state (rising edge fires)

    // runtime
    int     fuse     = 0;  ///< ticks until the shot (0 = not ignited)
    int     cooldown = 0;  ///< ticks until the next shot is allowed
    int64_t igniter  = 0;  ///< unique id of the player who lit the fuse (0 = redstone)
    int64_t barrel   = 0;  ///< unique id of the barrel entity (cache)
    bool    dirty    = false;
};

/// Server-side state and logic of every cannon block.
class Cannons {
public:
    static Cannons& get();

    void install();
    void uninstall();

    // ---- cannon blocks -------------------------------------------------------------------------
    /// The record of the cannon at `pos` (created when the block is a cannon), nullptr otherwise.
    CannonData* at(core::GlobalPos const& pos);
    [[nodiscard]] bool isCannon(core::GlobalPos const& pos) const;
    /// The face the cannon was placed on (`minecraft:block_face`, FacingID order), 1 when unknown.
    [[nodiscard]] int face(core::GlobalPos const& pos) const;
    /// Direction the barrel points to.
    [[nodiscard]] math::V3 direction(core::GlobalPos const& pos);

    /// Puts (part of) `stack` into the cannon (gunpowder → fuel, else → projectile slot).
    /// Returns the number of items taken.
    int insert(core::GlobalPos const& pos, ItemStack const& stack);
    /// Sets yaw/pitch (clamped) and power (1..maxPower) and turns the barrel.
    void setAim(core::GlobalPos const& pos, float yaw, float pitch, std::optional<int> power = std::nullopt);
    /// Lights the fuse. Returns false (and tells `by`) when not loaded or cooling down.
    bool ignite(core::GlobalPos const& pos, Player* by);
    /// Fires right away (no fuse). Returns false when not loaded.
    bool fireNow(core::GlobalPos const& pos);
    /// Removes a cannon's record: drops its contents and removes the barrel.
    void removeCannon(core::GlobalPos const& pos, bool dropContents);
    /// Gives `stack` back to the player or drops it at the cannon.
    void giveOrDrop(Player& player, core::GlobalPos const& pos, ItemStack const& stack);
    /// Predicted path of the next shot (muzzle first, last point = impact if `blocked`).
    std::vector<math::V3> predict(core::GlobalPos const& pos, bool* blocked = nullptr);
    /// The barrel entity of a cannon (spawned when missing), nullptr if the chunk isn't loaded.
    Actor* barrel(core::GlobalPos const& pos);

    // ---- players -------------------------------------------------------------------------------
    void openScreen(Player& player, core::GlobalPos const& pos);
    void startManoeuvre(Player& player, core::GlobalPos const& pos);
    void stopManoeuvre(Player& player, bool notify);
    [[nodiscard]] std::optional<core::GlobalPos> manoeuvring(Player const& player) const;
    /// Handles a use of the cannon by `player` holding `held` (load / open the screen).
    void interact(Player& player, core::GlobalPos const& pos);

    /// Called every server tick.
    void tick();
    [[nodiscard]] uint64_t now() const { return mTick; }
    [[nodiscard]] std::map<core::GlobalPos, CannonData> const& all() const { return mCannons; }

    /// Reads one cannon's record from the world DB again (drops its runtime state).
    bool reload(core::GlobalPos const& pos);
    void loadWorld();
    void saveWorld();
    void closeWorld();

private:
    void fire(core::GlobalPos const& pos, CannonData& data);
    void syncBarrel(core::GlobalPos const& pos, CannonData& data, Actor* actor = nullptr);
    void tickManoeuvres();
    [[nodiscard]] bool redstonePowered(core::GlobalPos const& pos) const;

    struct Manoeuvre {
        core::GlobalPos pos;
        int             slot  = -1;
        int             ticks = 0;
    };

    std::map<core::GlobalPos, CannonData> mCannons;
    std::map<std::string, Manoeuvre>      mManoeuvres; ///< by player UUID
    uint64_t                              mTick   = 0;
    bool                                  mLoaded = false;
};

// ---- shared helpers (Effects.cpp) ---------------------------------------------------------------
BlockSource* regionOf(int dim);
/// Plays a resource-pack sound to every player within 48 blocks.
void playSound(int dim, Vec3 const& pos, std::string const& name, float volume = 1.0f, float pitch = 1.0f);
/// Spawns a particle effect for every player of the dimension.
void particle(int dim, Vec3 const& pos, std::string const& name);
/// Spawns a particle effect for one player only.
void particleFor(Player& player, Vec3 const& pos, std::string const& name);
/// Action-bar text.
void actionBar(Player& player, std::string const& text);
/// Spawns any actor from SNBT-like fields. Returns nullptr on failure.
Actor* spawnActor(BlockSource& region, std::string_view id, Vec3 const& pos, Vec3 const& motion, int64_t owner = 0, std::string const& extra = {});
/// Spawns a projectile entity the way the game shoots it (hit detection on), with elocity.
Actor* shootProjectile(BlockSource& region, std::string_view id, Vec3 const& pos, Vec3 const& velocity, int64_t owner = 0);
/// Spawns an item entity flying with `motion`.
Actor* dropItem(BlockSource& region, ItemStack const& stack, Vec3 const& pos, Vec3 const& motion = {});
ItemStack makeItem(std::string_view id, int count, int aux = 0);
std::string itemToSnbt(ItemStack const& stack);
ItemStack   itemFromSnbt(std::string const& snbt);
/// Runs a command as the server (selftests, animations).
void runCommand(std::string const& command);

// ---- projectiles (Projectiles.cpp) --------------------------------------------------------------
/// Launches what a cannon fires for `item` from `pos` with `velocity`. Returns the spawned actor.
Actor* launch(BlockSource& region, ItemStack const& item, Vec3 const& pos, Vec3 const& velocity, int64_t owner);
/// Explodes a bomb of `type` at `pos` (bomb, bomb_blue or bomb_spiky).
void explodeBomb(int dim, std::string_view type, Vec3 const& pos, int64_t owner);
void installProjectiles();
void uninstallProjectiles();
void tickProjectiles();
void clearProjectiles();

// ---- red merchant (RedMerchant.cpp) -------------------------------------------------------------
/// Spawns a red merchant 16-32 blocks from `player` (ignores time and chance). nullptr on failure.
Actor* spawnRedMerchantNear(Player& player);
void   tickRedMerchant(uint64_t tick);

} // namespace cm::feature::cannon
