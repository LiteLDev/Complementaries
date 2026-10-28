#pragma once

// Per-world state of sign posts, notice boards and doormats (world DB `cm:sign:*`) and the display
// entities that show them. Server thread only.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "feature/sign/SignData.h"

class BlockPos;
class BlockSource;
class ItemStack;
class Vec3;

namespace cm::feature::sign {

struct Board {
    std::string  item; ///< SNBT of the held item ("" = empty)
    int          page    = 0;
    int          color   = kDefaultDye;
    bool         glowing = false;
    bool         powered = false;
    std::int64_t entity  = 0;
};

struct Mat {
    std::array<std::string, kDoormatLines> lines;
    int                                    color   = kDefaultDye;
    bool                                   glowing = false;
    bool                                   waxed   = false;
    std::int64_t                           entity  = 0;
};

/// Blocks a sign post can be put on (fences, walls).
[[nodiscard]] bool isPostBlock(std::string_view blockId);
/// Items a notice board accepts.
[[nodiscard]] bool isBoardItem(std::string_view itemId);
/// Pages of a writable / written book (empty for other items).
[[nodiscard]] std::vector<std::string> bookPages(ItemStack const& item);

[[nodiscard]] std::string itemToSnbt(ItemStack const& item);
[[nodiscard]] ItemStack   itemFromSnbt(std::string const& snbt);

/// Facing (0 S, 1 W, 2 N, 3 E) of a notice board / doormat block.
[[nodiscard]] int blockFacing(BlockSource& region, BlockPos const& pos);

class Store {
public:
    static Store& get();

    void load();  ///< world ready
    void clear(); ///< world closed

    // ---- sign posts ---------------------------------------------------------------------------
    [[nodiscard]] Post* post(std::string const& key);
    /// Stores `post` (creating / updating its display entity). An empty post is removed.
    void putPost(std::string const& key, Post post);
    /// Removes the post data and its entity; drops the arms as items when `drop`.
    void removePost(std::string const& key, bool drop);

    // ---- notice boards ------------------------------------------------------------------------
    [[nodiscard]] Board* board(std::string const& key);
    void                 putBoard(std::string const& key, Board board);
    void                 removeBoard(std::string const& key, bool drop);
    /// Lines shown by a board (current page wrapped, or the item name) and the page count.
    [[nodiscard]] std::vector<std::string> boardLines(Board const& board, int* pageCount = nullptr) const;

    // ---- doormats -----------------------------------------------------------------------------
    [[nodiscard]] Mat* mat(std::string const& key);
    void               putMat(std::string const& key, Mat mat);
    void               removeMat(std::string const& key);

    // ---- upkeep -------------------------------------------------------------------------------
    /// The block at a stored position changed: drops / removes data that lost its block.
    void onBlockChanged(int dim, BlockPos const& pos, std::string const& newBlock);
    /// Respawns missing display entities and removes stray ones (every second).
    void reconcile(bool sweepOrphans);
    /// Redstone polling of the notice boards (page turn on a rising edge).
    void tickBoards();
    /// Positions whose next removal must not drop items (creative breaking).
    void suppressDrops(std::string const& key) { mNoDrop.insert(key); }

    [[nodiscard]] std::size_t postCount() const { return mPosts.size(); }
    [[nodiscard]] std::size_t boardCount() const { return mBoards.size(); }
    [[nodiscard]] bool        hasMats() const { return !mMats.empty(); }

private:
    void syncPost(std::string const& key, Post& post);
    void syncBoard(std::string const& key, Board& board);
    void syncMat(std::string const& key, Mat& mat);
    void savePost(std::string const& key, Post const& post);
    void saveBoard(std::string const& key, Board const& board);
    void saveMat(std::string const& key, Mat const& mat);

    std::unordered_map<std::string, Post>  mPosts;
    std::unordered_map<std::string, Board> mBoards;
    std::unordered_map<std::string, Mat>   mMats;
    std::unordered_set<std::string>        mNoDrop;
};

} // namespace cm::feature::sign
