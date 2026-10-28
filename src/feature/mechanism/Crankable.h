#pragma once

// Public extension point of the mechanism unit (U7): blocks that a crank can turn.
//
// In Supplementaries a crank attached to a "crankable" block (the pulley block, ...) also turns it
// each time the crank is used. Another unit registers its block once (e.g. in `install()`):
//
//     cm::feature::mechanism::registerCrankable("complementaries:pulley_block",
//         [](BlockSource& region, BlockPos const& pos, bool ccw, Player* player) { ... });
//
// `pos` is the crankable block, `ccw` true for a sneaking (counter-clockwise) turn, `player` the
// player turning the crank (nullptr if none). Runs on the server thread, outside of any game
// callback (world edits are safe).

#include <functional>
#include <string>
#include <string_view>

class BlockPos;
class BlockSource;
class Player;

namespace cm::feature::mechanism {

using CrankableFn = std::function<void(BlockSource& region, BlockPos const& pos, bool ccw, Player* player)>;

/// Registers (or replaces) the handler of block `id`.
void registerCrankable(std::string const& id, CrankableFn fn);
/// Removes the handler of block `id`.
void unregisterCrankable(std::string const& id);

} // namespace cm::feature::mechanism
