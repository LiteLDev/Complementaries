#pragma once

// U3 `lock`: keys, lock block, safe, iron / gold gates, gold and netherite doors and trapdoors.
// Behaviour reference: docs/spec/lock.md. Everything here runs on the server thread.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/GlobalPos.h"

class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;
class Vec3;

namespace cm::feature::lock {

/// `config/lock.json` (Supplementaries' keys in docs/spec/lock.md §9).
struct LockConfig {
    int  version                 = 1;
    bool enableKey               = true;
    bool enableLockBlock         = true;
    bool enableSafe              = true;
    bool enableIronGate          = true;
    bool enableGoldDoor          = true;
    bool enableGoldTrapdoor      = true;
    bool enableNetheriteDoor     = true;
    bool enableNetheriteTrapdoor = true;
    /// Safes can only be broken by players who can open them (or creative players).
    bool safePreventBreaking = false;
    /// Safes are bound to their first user instead of a key.
    bool simpleSafes = false;
    /// Two gates on top of each other open together.
    bool gateDoubleOpening = true;
    /// Iron gates only open by redstone; redstone locks gold gates (like gold doors).
    bool doorLikeGates = false;
    /// Netherite doors and trapdoors can only be broken by players carrying their key.
    bool netheriteUnbreakable = false;
};
[[nodiscard]] LockConfig const& config();

// ---- ids -------------------------------------------------------------------------------------------
inline constexpr std::string_view kKeyId               = "complementaries:key";
inline constexpr std::string_view kLockBlockId         = "complementaries:lock_block";
inline constexpr std::string_view kSafeId              = "complementaries:safe";
inline constexpr std::string_view kIronGateId          = "complementaries:iron_gate";
inline constexpr std::string_view kGoldGateId          = "complementaries:gold_gate";
inline constexpr std::string_view kGoldDoorId          = "complementaries:gold_door";
inline constexpr std::string_view kNetheriteDoorId     = "complementaries:netherite_door";
inline constexpr std::string_view kGoldTrapdoorId      = "complementaries:gold_trapdoor";
inline constexpr std::string_view kNetheriteTrapdoorId = "complementaries:netherite_trapdoor";
inline constexpr std::string_view kSafeStorageId       = "complementaries:safe_storage";

// states (Java 2D facing: 0 S, 1 W, 2 N, 3 E)
inline constexpr std::string_view kFacing  = "complementaries:facing";
inline constexpr std::string_view kOpen    = "complementaries:open";
inline constexpr std::string_view kPowered = "complementaries:powered";
inline constexpr std::string_view kHinge   = "complementaries:hinge"; ///< true = hinge on the right
inline constexpr std::string_view kUpper   = "complementaries:upper";
inline constexpr std::string_view kTop     = "complementaries:top";
inline constexpr std::string_view kInWall  = "complementaries:in_wall";

enum class Kind {
    None,
    LockBlock,
    Safe,
    IronGate,
    GoldGate,
    GoldDoor,
    NetheriteDoor,
    GoldTrapdoor,
    NetheriteTrapdoor,
};
[[nodiscard]] Kind kindOf(Block const& block);
[[nodiscard]] Kind kindOf(std::string_view id);
[[nodiscard]] inline bool isDoor(Kind k) { return k == Kind::GoldDoor || k == Kind::NetheriteDoor; }
[[nodiscard]] inline bool isTrapdoor(Kind k) { return k == Kind::GoldTrapdoor || k == Kind::NetheriteTrapdoor; }
[[nodiscard]] inline bool isGate(Kind k) { return k == Kind::IronGate || k == Kind::GoldGate; }
/// Blocks that read the redstone signal (they carry `minecraft:redstone_consumer`).
[[nodiscard]] inline bool readsRedstone(Kind k) { return isGate(k) || k == Kind::GoldDoor || k == Kind::GoldTrapdoor; }
/// Blocks that keep a password in the world DB at their position (doors: the lower half).
[[nodiscard]] inline bool isKeyLockable(Kind k) {
    return k == Kind::LockBlock || k == Kind::NetheriteDoor || k == Kind::NetheriteTrapdoor || k == Kind::Safe;
}
[[nodiscard]] bool enabled(Kind k);

// ---- block states ----------------------------------------------------------------------------------
[[nodiscard]] int          state(Block const& block, std::string_view name, int fallback = 0);
[[nodiscard]] bool         flag(Block const& block, std::string_view name);
/// `block` with the given states changed (unknown states are ignored).
[[nodiscard]] Block const& with(Block const& block, std::vector<std::pair<std::string_view, int>> const& states);
/// Sets a block now (neighbour + client updates).
bool place(BlockSource& region, BlockPos const& pos, Block const& block);
[[nodiscard]] BlockSource* regionOf(int dim);
/// A fresh stack of `id` (null stack when the item is unknown).
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1);
/// Spawns an item entity at `pos`.
void dropItem(BlockSource& region, Vec3 const& pos, ItemStack const& item);
/// Gives `item` to the player (dropped at their feet when the inventory is full).
void giveItem(Player& player, ItemStack const& item);

// ---- feedback --------------------------------------------------------------------------------------
/// Java's `displayClientMessage(msg, true)`: the action bar.
void actionBar(Player& player, std::string const& text);
/// Plays a sound of our sound definitions (RP `sounds.d/lock.json`) or a vanilla one.
void playSound(int dim, Vec3 const& pos, std::string const& name, float volume, float pitch);
void playSound(int dim, BlockPos const& pos, std::string const& name, float volume, float pitch);
[[nodiscard]] float randomFloat(); ///< [0, 1)

// ---- keys ------------------------------------------------------------------------------------------
/// The password of a key item: its custom name, or "Key" when unnamed. nullopt when not a key.
[[nodiscard]] std::optional<std::string> keyPassword(ItemStack const& stack);
enum class KeyStatus { Correct, Incorrect, NoKey };
/// The key status of a held item against `password`.
[[nodiscard]] KeyStatus keyStatus(ItemStack const& stack, std::optional<std::string> const& password);
/// Searches the inventory, armor and offhand. No password = always correct.
[[nodiscard]] KeyStatus inventoryKeyStatus(Player& player, std::optional<std::string> const& password);
/// Like `inventoryKeyStatus` but creative players always pass.
[[nodiscard]] KeyStatus playerKeyStatus(Player& player, std::optional<std::string> const& password);
/// `message.supplementaries.<what>.locked` / incorrect key message for a status.
void sendKeyMessage(Player& player, KeyStatus status, std::string_view what);
inline constexpr std::size_t kMaxShownPassword = 50;

// ---- world data ------------------------------------------------------------------------------------
[[nodiscard]] std::optional<std::string> password(core::GlobalPos const& pos);
void setPassword(core::GlobalPos const& pos, std::optional<std::string> const& password);
void eraseLockData(core::GlobalPos const& pos);

/// Java `KeyLockableTile.handleAction`. Returns true when the block has to be used (opened).
/// May assign / clear the password and sends the feedback.
bool handleKeyAction(Player& player, ItemStack const& held, bool sneaking, core::GlobalPos const& lockPos, std::string_view what);

// ---- scheduling ------------------------------------------------------------------------------------
/// Runs `fn` at the start of the next server tick (after a cancelled interaction rolled back).
void defer(std::function<void()> fn);
/// Runs `fn` after `ticks` ticks (ticks >= 1).
void later(int ticks, std::function<void()> fn);
namespace detail {
void      runScheduled(); ///< module tick
void      clearScheduled();
long long currentTick();
} // namespace detail

// ---- interactions (server thread; return true when the click is consumed) --------------------------
struct Click {
    core::GlobalPos pos;               ///< the clicked block
    int             face     = 1;      ///< Bedrock FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
    float           hitX     = 0.5f;   ///< click position inside the clicked block (0..1)
    float           hitY     = 0.5f;
    float           hitZ     = 0.5f;
    bool            sneaking = false;
    float           yaw      = 0.0f;   ///< player yaw (Java convention: 0 south, 90 west)
};
/// A right click on a block (Java's useItemOn / useWithoutItem order, sneaking rules). True when
/// one of our blocks consumed it.
bool interact(Player& player, ItemStack const& held, Click const& click);
bool useLockBlock(Player& player, ItemStack const& held, Click const& click);
bool useDoor(Player& player, ItemStack const& held, Click const& click);
bool useTrapdoor(Player& player, ItemStack const& held, Click const& click);
bool useGate(Player& player, ItemStack const& held, Click const& click);
bool useSafe(Player& player, ItemStack const& held, Click const& click);

// ---- placement / removal ---------------------------------------------------------------------------
/// Fixes the states of a freshly placed block (facing, hinge, halves, power).
void onPlaced(Player& player, core::GlobalPos const& pos, Click const& click, ItemStack const& placedFrom);
/// A block of ours was replaced at `pos` (`prevId` = its id). `byCreative` when a creative player
/// broke it.
void onRemoved(core::GlobalPos const& pos, Block const& previous, bool byCreative);
/// Player break check (`netheriteUnbreakable`, `safePreventBreaking`). False = keep the block.
[[nodiscard]] bool mayBreak(Player& player, core::GlobalPos const& pos);

// ---- redstone ----------------------------------------------------------------------------------------
/// The signal the consumer at `pos` receives (0 when none / unloaded).
[[nodiscard]] int  redstoneSignal(BlockSource& region, BlockPos const& pos);
void track(core::GlobalPos const& pos);
void untrack(core::GlobalPos const& pos);
[[nodiscard]] bool tracked(core::GlobalPos const& pos);
/// Polls the tracked redstone readers (every tick).
void tickRedstone();
void loadTracked();
void saveTracked();
void clearTracked();

// ---- gates / doors helpers used by tests -----------------------------------------------------------
[[nodiscard]] bool gateInWall(BlockSource& region, BlockPos const& pos, int facing);
/// Vanilla hinge rule for a door placed at `lower` facing `facing` (0 S ... 3 E). True = right.
[[nodiscard]] bool doorHingeRight(BlockSource& region, BlockPos const& lower, int facing, float clickX, float clickZ);
/// Placement states of a trapdoor: {facing, top}.
[[nodiscard]] std::pair<int, bool> trapdoorPlacement(Click const& click, int playerFacing);
/// The facing a gate opening toward `yaw` gets (closest of `facing` and its opposite).
[[nodiscard]] int gateOpenFacing(float yaw, int facing);

// ---- safe --------------------------------------------------------------------------------------------
struct SafeData {
    std::optional<std::string> owner;     ///< owner UUID string
    std::optional<std::string> ownerName;
    std::optional<std::string> password;
    std::optional<std::string> customName; ///< the safe item's custom name
    long long                  storage = 0; ///< ActorUniqueID of the storage entity (0 = none)
};
[[nodiscard]] SafeData safeData(core::GlobalPos const& pos);
void saveSafeData(core::GlobalPos const& pos, SafeData const& data);
[[nodiscard]] std::string safeTitle(SafeData const& data);
/// Comparator-like status: 15 when locked.
[[nodiscard]] bool safeLocked(SafeData const& data);
/// Number of non-empty slots of the safe's storage (-1 when there is no storage).
[[nodiscard]] int safeItemCount(core::GlobalPos const& pos);
/// Puts an item into slot `slot` of the safe's storage (tests).
bool safeSetItem(core::GlobalPos const& pos, int slot, ItemStack const& item);
/// The safe item that keeps the safe's data and contents.
[[nodiscard]] ItemStack safeItem(core::GlobalPos const& pos);
/// Whether `player` currently has the safe at `pos` open.
[[nodiscard]] bool safeOpenBy(Player& player, core::GlobalPos const& pos);
/// Java `SafeBlockTile.canPlayerOpen`.
[[nodiscard]] bool safeCanOpen(Player& player, core::GlobalPos const& pos, bool feedback);
/// A safe was placed by `player` from `item` (restores its data and contents).
void placeSafe(Player& player, core::GlobalPos const& pos, ItemStack const& item);
/// A safe block disappeared: drops it with its data (unless broken by a creative player with an
/// empty safe) and removes its storage.
void removeSafe(core::GlobalPos const& pos, bool byCreative);
/// The data a safe item carries (from its user NBT).
[[nodiscard]] SafeData safeItemData(ItemStack const& item);
void tickSafes();
void clearSafes();

// ---- lock block timers ------------------------------------------------------------------------------
void loadTimers();
void saveTimers();
void clearTimers();
void tickTimers();

} // namespace cm::feature::lock
