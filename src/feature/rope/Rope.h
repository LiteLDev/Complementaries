#pragma once

// U11 `rope`: public entry points other units may call (crank, turn table, ...).

class BlockSource;
class BlockPos;
class Actor;

namespace cm::feature::rope {

/// Turns the pulley block at `pos` once (server thread). Clockwise winds the rope/chain hanging
/// under it back in (and pulls a hanging block up); counter-clockwise lowers one more piece.
/// `by` (optional) is used for sounds. Returns false when `pos` is not a pulley or nothing moved.
bool turnPulley(BlockSource& region, BlockPos const& pos, bool clockwise, Actor* by = nullptr);

/// True if the block at `pos` is a Complementaries rope.
[[nodiscard]] bool isRopeAt(BlockSource& region, BlockPos const& pos);

} // namespace cm::feature::rope
