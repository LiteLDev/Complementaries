#pragma once

// U1 jar: shared declarations of the unit's .cpp files (server side).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/jar/Contents.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;
class Vec3;

namespace cm::feature::jar {

inline constexpr std::string_view kJarId     = "complementaries:jar";
inline constexpr std::string_view kFaucetId  = "complementaries:faucet";
inline constexpr std::string_view kJarBoatId = "complementaries:jar_boat";
inline constexpr std::string_view kJarMobId  = "complementaries:jar_mob";

// The `content` value (0..63, see Fluids.h) is split in two block states (16 values max each):
// kind = content / 16, fluid = content % 16.
inline constexpr std::string_view kStateKind      = "complementaries:kind";
inline constexpr std::string_view kStateFluid     = "complementaries:fluid";
inline constexpr std::string_view kStateLevel     = "complementaries:level";
inline constexpr std::string_view kStateEnabled   = "complementaries:enabled";
inline constexpr std::string_view kStatePowered   = "complementaries:powered";
inline constexpr std::string_view kStateConnected = "complementaries:connected";
inline constexpr std::string_view kStateFacing    = "complementaries:facing"; ///< Java 2D: 0 S, 1 W, 2 N, 3 E

[[nodiscard]] inline std::vector<std::pair<std::string_view, int>> contentStates(int content) {
    return {{kStateKind, (content >> 4) & 3}, {kStateFluid, content & 15}};
}
/// The `content` value of a jar / faucet block.
[[nodiscard]] int contentOf(Block const& block);

/// `config/jar.json`.
struct JarConfig {
    int                      version          = 1;
    bool                     enabled          = true;
    int                      capacity         = 12;
    bool                     drinkFromJar     = false;
    bool                     drinkFromJarItem = false;
    bool                     autoDetect       = false;
    bool                     jarCapture       = true;
    bool                     jarCookies       = true;
    bool                     jarLiquids       = true;
    std::vector<std::string> cookieItems{"minecraft:cookie", "complementaries:candy"};
    std::vector<std::string> extraCatchable{};
    bool                     allowAllMobs      = false;
    bool                     persistentMobs    = false;
    int                      healthThreshold   = 100;
    bool                     requireTaming     = true;
    bool                     faucetEnabled     = true;
    bool                     spillItems        = true;
    bool                     fillEntitiesBelow = false;
};

[[nodiscard]] JarConfig const& cfg();
[[nodiscard]] int              capacity();

// ---- blocks ------------------------------------------------------------------------------------
[[nodiscard]] bool isBlock(Block const& block, std::string_view id);
[[nodiscard]] BlockSource* regionOf(int dim);
[[nodiscard]] int          stateOf(Block const& block, std::string_view state, int fallback = 0);
/// Any block state (vanilla ones too), read from the block's serialization tag.
[[nodiscard]] int         vanillaInt(Block const& block, std::string_view name, int fallback = 0);
[[nodiscard]] std::string vanillaString(Block const& block, std::string_view name);
/// Sets `states` on the block at `pos` (keeps the block's other states).
bool setStates(BlockSource& region, BlockPos const& pos, std::vector<std::pair<std::string_view, int>> const& states);

// ---- items ---------------------------------------------------------------------------------------
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1, int aux = 0);
[[nodiscard]] ItemRef   refOf(ItemStack const& stack);
[[nodiscard]] bool      isItem(ItemStack const& stack, std::string_view id);
/// Contents stored in a jar item (nullopt when the item holds nothing).
[[nodiscard]] std::optional<Contents> itemContents(ItemStack const& stack);
/// Stores `c` in a jar item (user data + tooltip lore), or clears it when empty.
void setItemContents(ItemStack& stack, Contents const& c, Player const* viewer = nullptr);
/// The tooltip lines of a jar item with `c` (also used by the selftests).
[[nodiscard]] std::vector<std::string> tooltipFor(Contents const& c, Player const* viewer);
/// Serialized NBT of an item (hex) / the item back.
[[nodiscard]] StoredItem storeItem(ItemStack const& stack);
[[nodiscard]] ItemStack  restoreItem(StoredItem const& item);
/// Supplementaries' `Utils.swapItem`: survival uses one of the held item and gives `result`;
/// creative keeps the held item and adds `result` if the inventory lacks it.
void swapHeld(Player& player, ItemStack const& result);
void giveOrDrop(Player& player, ItemStack item);
void playSound(BlockSource& region, Vec3 const& pos, std::string_view name, float volume = 1.f, float pitch = 1.f);

// ---- placed jars ------------------------------------------------------------------------------
[[nodiscard]] Contents jarAt(core::GlobalPos const& pos);
/// Stores the contents of a placed jar (world DB), updates its block states, light and mob display.
void setJar(core::GlobalPos const& pos, Contents const& c);
/// Forgets a jar (no drop): used when the block is gone.
void forgetJar(core::GlobalPos const& pos);
/// Right click on a placed jar. Returns true when handled.
bool useJar(Player& player, core::GlobalPos const& pos);
/// The jar item that a jar with `c` drops.
[[nodiscard]] ItemStack jarItem(Contents const& c);
/// Would `useJar` do something for this player's click (no side effects)?
[[nodiscard]] bool jarWouldHandle(Player const& player, core::GlobalPos const& pos);
/// Drops the jar item of a jar that is gone.
void dropJar(core::GlobalPos const& pos, Contents const& c);
/// The jar at `pos` will be removed without dropping (creative break, selftest cleanup).
void suppressDrop(core::GlobalPos const& pos);
/// Drinks / eats one bottle of `fluid` (Supplementaries `tryDrinkUpFluid`).
bool drinkFluid(Player& player, FluidStack& fluid);

// ---- mobs -------------------------------------------------------------------------------------
enum class CaptureFail { None, NotCatchable, Tamed, Health };
/// Can a jar take this mob? `CaptureFail::None` when yes.
[[nodiscard]] CaptureFail canCatch(Actor& mob, Player const* player);
/// Stores `mob` in a StoredMob and removes it from the world (nullopt if it can't be stored).
[[nodiscard]] std::optional<StoredMob> captureMob(Actor& mob);
/// Spawns a stored mob at `pos`. Returns the spawned actor or nullptr.
Actor* releaseMob(BlockSource& region, StoredMob const& mob, Vec3 const& pos, float yaw);
/// Jar item used on a mob (left or right click). Returns true when it caught it (or showed why not).
bool useJarItemOnMob(Player& player, Actor& target);
/// Jar item used on a block: releases its mob. Returns true when handled.
bool releaseFromItem(Player& player, BlockPos const& clicked, Vec3 const& clickPos);
/// A stored fish bucket from a bucket item id (nullopt if not a fish bucket).
[[nodiscard]] std::optional<StoredMob> mobFromBucket(ItemStack const& bucket);
/// Display kind of a stored mob (0 = not drawn).
[[nodiscard]] int displayKindFor(std::string const& type, int variant, bool baby);
/// "minecraft:magma_cube" -> "Magma Cube".
[[nodiscard]] std::string prettyMobName(std::string_view type);
/// `/supplementaries jardemo`: the showcase row; returns the blocks it placed.
std::vector<core::GlobalPos> buildJarDemo(Player& player);
/// Keeps the helper entity that draws a jar's mob in sync with `c`.
void syncDisplay(core::GlobalPos const& pos, Contents const& c);
void removeDisplay(core::GlobalPos const& pos);
/// A jar with a drawn mob exists at `pos` (world load): the display is spawned once loaded.
void wantDisplay(core::GlobalPos const& pos);
[[nodiscard]] bool hasDisplay(core::GlobalPos const& pos);
void clearDisplays();
/// Removes display entities whose jar is gone, spawns missing ones (periodic).
void reconcileDisplays();

// ---- faucets ----------------------------------------------------------------------------------
void faucetPlaced(core::GlobalPos const& pos);
void faucetRemoved(core::GlobalPos const& pos);
void faucetNeighborChanged(core::GlobalPos const& pos);
/// Right click: toggles the faucet.
void toggleFaucet(core::GlobalPos const& pos);
/// One server tick of every faucet.
void tickFaucets();
/// Runs one transfer attempt now (ignores the cooldown). Returns the cooldown it would set.
int  runFaucetOnce(core::GlobalPos const& pos);
void loadFaucets();
void saveFaucets();
void clearFaucets();
[[nodiscard]] bool faucetKnown(core::GlobalPos const& pos);

// ---- lifecycle --------------------------------------------------------------------------------
void clearJarCache();

} // namespace cm::feature::jar
