#pragma once

// U5 blackboard: the placed board (data in the world DB, rendering through helper entities),
// direct drawing, waxing / glowing / washing, item data and the edit screen.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/blackboard/Board.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class ItemStackBase;
class Player;
class Vec3;

namespace cm::feature::blackboard {

inline constexpr std::string_view kBlockId   = "complementaries:blackboard";
inline constexpr std::string_view kDisplayId = "complementaries:blackboard_display";

/// `config/blackboard.json` (Supplementaries `building.blackboard`).
struct BlackboardConfig {
    int  version = 1;
    /// When false the board is decorative: no drawing, waxing or screen.
    bool enabled = true;
    /// Enable to draw directly on a blackboard using any dye.
    bool colored_blackboard = false;
    /// Interaction mode for blackboards: "both", "gui" (screen only) or "manual" (direct drawing only).
    std::string interaction_mode = "both";
    /// Opening a blackboard gui also shows up to a 3x3 of connected blackboards so you can draw on all
    /// of them at once.
    bool connected_gui = true;
};

[[nodiscard]] BlackboardConfig const& config();
[[nodiscard]] bool                    canOpenGui();
[[nodiscard]] bool                    canManualDraw();

// ---- blocks -----------------------------------------------------------------------------------
[[nodiscard]] bool isBlackboard(Block const& block);
/// Direction the drawing faces: 0 north, 1 east, 2 south, 3 west; -1 when not a blackboard.
[[nodiscard]] int frontOf(Block const& block);
/// Block face id (Bedrock FacingID: 2 north, 3 south, 4 west, 5 east) of a front direction.
[[nodiscard]] int faceIdOf(int front);
/// The pixel (column from the viewer's left, row from the top) under a hit point on the front face.
/// `hit` is the click position (absolute, or relative to the block).
[[nodiscard]] std::pair<int, int> hitPixel(int front, BlockPos const& pos, Vec3 const& hit);

// ---- board data -------------------------------------------------------------------------------
/// The board stored at `pos` (an empty board when there is none).
[[nodiscard]] Board boardAt(core::GlobalPos const& pos);
/// Stores `board` for the blackboard at `pos` and updates its display entities.
void setBoard(BlockSource& region, BlockPos const& pos, Board const& board);
/// The display entities of the board at `pos` (both halves, unordered).
[[nodiscard]] std::vector<Actor*> displaysAt(BlockSource& region, BlockPos const& pos);
/// An int property of a display entity (-1 if missing).
[[nodiscard]] int displayProperty(Actor& actor, std::string_view name);

// ---- items ------------------------------------------------------------------------------------
/// The board data an item carries (nullopt for a blank item or another item).
[[nodiscard]] std::optional<Board> boardOfItem(ItemStackBase const& item);
/// Writes the board data (+ tooltip lore) into a blackboard item; an empty board clears it.
void writeBoardToItem(ItemStackBase& item, Board const& board);
/// A blackboard item carrying `board`.
[[nodiscard]] ItemStack makeBoardItem(Board const& board, int count = 1);

// ---- interaction ------------------------------------------------------------------------------
/// Handles a use of `item` on the blackboard at `pos` (Java `useItemOn`). Returns true when the
/// interaction was consumed (the vanilla use must be cancelled).
bool use(Player& player, ItemStack& item, BlockPos const& pos, int face, Vec3 const& hit);

// ---- connected boards ---------------------------------------------------------------------------
/// The boards the edit screen shows: `cols` x `rows` cells (column-major, cell = c * rows + r),
/// column 0 on the viewer's left, row 0 at the top. Cells without a board are nullopt.
struct Canvas {
    int                                  cols = 1;
    int                                  rows = 1;
    std::vector<std::optional<core::GlobalPos>> cells;
    [[nodiscard]] std::optional<core::GlobalPos> const& at(int c, int r) const {
        return cells[static_cast<std::size_t>(c * rows + r)];
    }
};
[[nodiscard]] Canvas connectedBoards(BlockSource& region, BlockPos const& clicked, bool connected);

// ---- sounds -----------------------------------------------------------------------------------
void playSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume = 1.f, float pitch = 1.f);

} // namespace cm::feature::blackboard
