#pragma once

// The "Edit Blackboard" screen: a data-driven server form for every client (vanilla fallback), and
// the session bookkeeping used by the native screen of the client mod.

#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/blackboard/Board.h"

class BlockPos;
class Player;

namespace cm::feature::blackboard {

/// Opens the edit screen of the board at `pos` (and its connected boards) for `player`.
/// Returns false when someone else is editing one of the boards.
bool openEditor(Player& player, BlockPos const& pos);
/// The player (UUID string) editing the board at `pos`, if any.
[[nodiscard]] std::optional<std::string> editorOf(core::GlobalPos const& pos);
/// Applies pixels sent by a client for a board of `player`'s open session (or by an admin).
/// Returns an error message, empty on success.
[[nodiscard]] std::string submitPixels(Player* player, bool admin, core::GlobalPos const& pos, Pixels const& px);
/// Closes every session (world closed / module uninstalled).
void closeAllEditors();

} // namespace cm::feature::blackboard
