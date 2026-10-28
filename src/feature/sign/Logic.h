#pragma once

// Gameplay operations of the sign unit, shared by the interaction handlers, the forms, way sign
// generation and the selftests. Positions are `posKey(dim, x, y, z)` strings. Server thread only.

#include <array>
#include <optional>
#include <string>

#include "feature/sign/SignData.h"

class ItemStack;
class Player;

namespace cm::feature::sign::logic {

// ---- sign posts ---------------------------------------------------------------------------------
/// Adds an arm on `upper` (or the other half when taken). False if the post is full or not a post.
bool addArm(std::string const& key, int wood, bool upper, float yaw, bool left);
/// The arm that a click at `relY` (0..1 inside the block) selects: the clicked half if active, else
/// the other active one. nullopt when the post has no arms.
[[nodiscard]] std::optional<bool> pickArm(std::string const& key, double relY);
bool setArmText(std::string const& key, bool upper, std::string const& text);
bool pointArm(std::string const& key, bool upper, double targetX, double targetZ);
bool flipArm(std::string const& key, bool upper);
bool dyeArm(std::string const& key, bool upper, int dye);
bool glowArm(std::string const& key, bool upper, bool glowing);
bool waxPost(std::string const& key, bool waxed);

// ---- notice boards ------------------------------------------------------------------------------
/// Puts one item on the board. False if occupied or not accepted.
bool boardInsert(std::string const& key, ItemStack const& item);
/// Takes the item off the board (empty stack when there was none).
ItemStack boardTake(std::string const& key);
/// Next page of a multi-page book (wraps). False if there is no such book.
bool boardNextPage(std::string const& key);
bool boardDye(std::string const& key, int dye);
bool boardGlow(std::string const& key, bool glowing);
/// Comparator output of a board (0 empty, page number for books, 15 otherwise).
[[nodiscard]] int boardSignal(std::string const& key);
/// Updates the board block's has_book state from the stored item.
void refreshBoardBlock(std::string const& key);

// ---- doormats -----------------------------------------------------------------------------------
bool setMatLines(std::string const& key, std::array<std::string, kDoormatLines> const& lines);
bool matDye(std::string const& key, int dye);
bool matGlow(std::string const& key, bool glowing);
bool matWax(std::string const& key, bool waxed);

// ---- screens ------------------------------------------------------------------------------------
void openPostEditor(Player& player, std::string const& key);
void openMatEditor(Player& player, std::string const& key);

// ---- way signs ----------------------------------------------------------------------------------
struct WayTarget {
    std::string name; ///< translated structure name
    int         x = 0, z = 0;
};
/// Builds a way sign: a fence at `fencePos` with a sign post pointing to `first` (and `second`).
bool buildWaySign(int dim, int x, int y, int z, WayTarget const& first, std::optional<WayTarget> const& second);
/// Text of a way sign arm ("Village 123m").
[[nodiscard]] std::string waySignText(WayTarget const& target, int fromX, int fromZ);

} // namespace cm::feature::sign::logic
