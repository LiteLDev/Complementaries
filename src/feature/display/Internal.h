#pragma once

// Internal wiring between the files of the display unit.

class BlockPos;
class Player;

namespace cm::feature::display::detail {

/// A world became available / is closing (resolve block types, patch vtables, reset caches).
void onWorldReady();
void onWorldClosed();

/// Comparator vtable patch, enchanting hook, entity interaction hook (Hooks.cpp, HatStand.cpp).
void installHooks();
void uninstallHooks();

/// Hat stand hooks / listeners and per-world state (HatStand.cpp).
void installHatStand();
void uninstallHatStand();
void resetHatStands();

/// Hat stand placement requested by a cancelled interaction (done next tick).
void queueHatStandPlacement(Player& player, BlockPos const& clicked, int face);
/// Per-tick hat stand work (deferred placements, collisions, skibidi).
void tickHatStands(int tick);

} // namespace cm::feature::display::detail
