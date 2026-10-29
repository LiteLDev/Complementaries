#pragma once

// U8 `circuit`: relayer, speaker block, crystal display, redstone illuminator and the minecart with
// dispenser (docs/spec/circuit.md).
//
// The blocks are data-driven (BP blocks/*.json). Their redstone side lives here:
// - the relayer is a `minecraft:redstone_producer` whose permutation (`complementaries:powered`)
//   switches its strong output on the front face; its input is read natively from the circuit
//   component behind it;
// - the other blocks are `minecraft:redstone_consumer`s; their input is the strength of their own
//   circuit component.
// Positions of our blocks are tracked through block changes (and saved in the world DB, so they
// survive restarts) and evaluated every server tick.

#include <optional>
#include <string>
#include <string_view>

#include "core/GlobalPos.h"
#include "feature/circuit/CircuitLogic.h"

class Block;
class BlockPos;
class BlockSource;
class Dimension;
class Player;
class Actor;

namespace cm::feature::circuit {

inline constexpr std::string_view kRelayer     = "complementaries:relayer";
inline constexpr std::string_view kSpeaker     = "complementaries:speaker_block";
inline constexpr std::string_view kDisplay     = "complementaries:crystal_display";
inline constexpr std::string_view kIlluminator = "complementaries:redstone_illuminator";
inline constexpr std::string_view kCart        = "complementaries:dispenser_minecart";

inline constexpr std::string_view kPowered = "complementaries:powered";
inline constexpr std::string_view kPower   = "complementaries:power";
inline constexpr std::string_view kChain   = "complementaries:chain";
inline constexpr std::string_view kFacing6 = "minecraft:facing_direction";
inline constexpr std::string_view kFacing4 = "minecraft:cardinal_direction";

/// `config/circuit.json`.
struct CircuitConfig {
    int  version                  = 1;
    int  speakerRange             = 64;
    int  speakerDefaultVolume     = 16;
    bool speakerNarrator          = true;
    int  speakerMaxLength         = 256;
    bool dispenserMinecartEnabled = true;
};

/// Which of our blocks `block` is (`Kind::None` for anything else).
[[nodiscard]] Kind kindOf(Block const& block);

/// Facing (0 down, 1 up, 2 north, 3 south, 4 west, 5 east) of a relayer / the front of a speaker or
/// display.
[[nodiscard]] int facingOf(Block const& block);

/// Redstone strength of the circuit component at `pos` (nullopt: no component there).
[[nodiscard]] std::optional<int> componentStrength(Dimension& dim, BlockPos const& pos);
/// "type=... dir=... strength=..." of the circuit component at `pos` (diagnostics).
[[nodiscard]] std::string describeComponent(Dimension& dim, BlockPos const& pos);
/// The signal the relayer at `pos` (facing `facing`) receives from behind.
[[nodiscard]] int relayerInput(BlockSource& region, BlockPos const& pos, int facing);

/// Evaluates every tracked block now (also runs every tick).
void evaluateAll();
/// Starts tracking `pos` if it holds one of our blocks (selftests; block changes do it by themselves).
void               track(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool tracked(core::GlobalPos const& pos);

// ---- speaker block (Speaker.cpp) ------------------------------------------------------------
struct SpeakerData {
    std::string message;
    int         mode   = 0; ///< SpeakerMode
    int         volume = 16;
    std::string name; ///< custom name of the placed item ("" = default)
};
[[nodiscard]] SpeakerData speakerData(core::GlobalPos const& pos);
void                      setSpeakerData(core::GlobalPos const& pos, SpeakerData const& data);
void                      eraseSpeakerData(core::GlobalPos const& pos);
/// The speaker at `pos` turned on: broadcast its message. Returns the number of players reached.
int speak(BlockSource& region, BlockPos const& pos);
/// Opens the speaker screen (form) for `player`.
void openSpeakerScreen(Player& player, core::GlobalPos const& pos);
void installSpeaker();
void uninstallSpeaker();
void clearSpeakerCache();

// ---- minecart with dispenser (DispenserMinecart.cpp) ----------------------------------------
void installCart();
void uninstallCart();
void tickCarts();
/// Fires one item from the minecart with dispenser `cart` (true if something was dispensed).
bool dispenseFromCart(Actor& cart);
void clearCartCache();

} // namespace cm::feature::circuit
