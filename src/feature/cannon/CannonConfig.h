#pragma once

#include <string>

namespace cm::feature::cannon {

/// `config/cannon.json` (see docs/spec/cannon.md §7). Keys follow the Supplementaries options
/// (`cannon.*`, `cannonball.*`, `bomb.*`, `blue_bomb.*`, `general.red_merchant_spawn_multiplier`).
struct CannonConfig {
    int version = 1;

    // cannon
    int   fuseTime   = 40;   ///< ticks between lighting the fuse and the shot
    int   cooldown   = 60;   ///< ticks after a shot before the cannon can fire again
    float firePower  = 1.0f; ///< fire power multiplier (speed = power level x 0.75 x this)
    bool  explodeTnt = true; ///< TNT shot from a cannon explodes on impact
    int   maxPower   = 4;    ///< highest power level (= gunpowder used per shot)

    // cannonball: energy = powerScaling x speed^2; damage = energy; breaking spends energy
    float cannonballPowerScaling = 1.0f;
    float cannonballBreakRadius  = 1.1f; ///< radius of the "explosion" on a block hit, 0 = no breaking

    // bomb
    float       bombExplosionRadius = 2.0f;
    std::string bombBreakBlocks     = "weak"; ///< "all", "weak" or "none"
    float       bombMaxResistance   = 4.0f;   ///< blast resistance up to which a block is "weak"
    int         bombFuse            = 0;      ///< ticks before a bomb explodes; 0 = on contact
    bool        bombCooldown        = true;   ///< (item cooldown, set in the item definition)

    // blue bomb
    float       blueBombExplosionRadius     = 3.0f;
    std::string blueBombBreakBlocks         = "none";
    int         blueBombSplitCount          = 4;
    float       blueBombBabyExplosionRadius = 1.2f;
    int         blueBombBabyFireDuration    = 5; ///< seconds; 0 = no fire

    // spiky bomb (Supplementaries 1.x)
    int   spikyShrapnel = 24;
    float spikyDamage   = 4.0f;

    // red merchant
    float redMerchantSpawnMultiplier = 1.0f; ///< 0 = never spawns, 2 = twice as often
};

} // namespace cm::feature::cannon
