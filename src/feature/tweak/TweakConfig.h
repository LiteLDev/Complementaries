#pragma once

namespace cm::feature::tweak {

/// `config/tweak.json`: every vanilla tweak of the unit can be switched off (docs/spec/tweak.md §5).
struct TweakConfig {
    int version = 1;

    bool placeableSticks     = true;
    bool placeableBlazeRods  = true;
    bool placeableBreezeRods = true;

    bool placeableGunpowder = true;
    int  gunpowderBurnTicks = 2; ///< ticks per burning stage (1..20)
    int  gunpowderSpreadAge = 2; ///< stage at which a burning trail ignites its neighbours (1..8)

    bool hangingFlowerPots = true;

    bool throwableBricks     = true;
    bool throwableSlimeballs = true;
    int  slimedDurationTicks = 200;

    bool clockRightClick   = true;
    bool compassRightClick = true;

    bool bottleXp     = true;
    int  bottleXpCost = 7; ///< experience points per bottle o' enchanting

    bool bellChainRinging = true;
    int  bellChainLength  = 16;

    bool cauldronDyeing = true;

    bool zombieHorseConversion = true;
    int  zombieHorseFleshCount = 64;
};

[[nodiscard]] TweakConfig const& config();

} // namespace cm::feature::tweak
