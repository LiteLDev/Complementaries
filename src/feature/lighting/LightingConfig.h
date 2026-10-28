#pragma once

#include <string>
#include <vector>

namespace cm::feature::lighting {

/// `config/lighting.json` (see docs/spec/lighting.md, section 4).
struct LightingConfig {
    int version = 1;
    /// Using a lantern on the side of a block hangs it on the wall (Java "wall lanterns" tweak).
    bool wallLanterns = true;
    /// Lantern item ids ("minecraft:soul_lantern", ...) that never become wall lanterns.
    std::vector<std::string> wallLanternBlacklist{};
    /// Entities moving through a wall lantern make it swing.
    bool wallLanternSwing = true;
    /// Burning projectiles (flaming arrows, fireballs) light sconces and candle holders.
    bool fireProjectilesLight = true;
    /// Splash water potions put sconces and candle holders out.
    bool waterPotionsExtinguish = true;
    /// Server-side flame / smoke particles of lit sconces and candle holders.
    bool particles = true;
    /// Random block samples per player per tick looking for lit blocks (like the client's animate tick).
    int particleSamplesPerTick = 667;
};

} // namespace cm::feature::lighting
