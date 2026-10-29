#pragma once

// Container screens for blocks and items without block entities.
//
// A screen is opened on an invisible helper entity (`complementaries:storage_view`) whose inventory
// is rebuilt with the wanted size / vanilla grid, filled from a content record and opened for the
// player. While it is open, every change is written through to the record (so the record is always
// authoritative and nothing can be duplicated); items that are not allowed bounce back to the player.
// The helper is removed when the last viewer closes the screen.

#include <functional>
#include <optional>
#include <string>

#include "core/GlobalPos.h"

class ItemStack;
class Player;

namespace cm::feature::storage::view {

struct Options {
    std::string                               uid;   ///< content record
    int                                       size = 9;
    std::string                               title;
    std::function<bool(ItemStack const&)>     allowed; ///< null = everything
    std::optional<core::GlobalPos>            block;   ///< the helper sits in this block (else at the player)
    std::function<void()>                     onChange; ///< after each write-through
    std::function<void()>                     onClosed; ///< when the last viewer closed the screen
};

/// Opens the screen for `player` (joins an existing view of the same uid). False on failure.
bool open(Player& player, Options opts);
/// True while some player views `uid`.
[[nodiscard]] bool viewed(std::string const& uid);
/// Writes back and closes every view of `uid` (block broken / moved).
void close(std::string const& uid);
/// Polls views (closed screens, changed contents). Called every tick by the module.
void tick();
/// Drops every view without touching the world (world closed).
void reset();
/// Hooks used by the views (module install / uninstall).
void install();
void uninstall();

} // namespace cm::feature::storage::view
