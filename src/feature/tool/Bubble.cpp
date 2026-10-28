// Bubble blower (blows bubble particles, places bubble blocks) and the bubble block, which pops
// after a while or shortly after something stands on it. Bubble blocks are tracked in memory
// and in the world DB (`cm:tool:bubbles`), since custom blocks have no block entities.

#include <algorithm>
#include <map>
#include <tuple>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/tool/Tool.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::tool::bubble {

namespace {

constexpr char const* kDbKey      = "cm:tool:bubbles";
constexpr int         kPopDelay   = 5;  ///< ticks between being stepped on and popping
constexpr int         kStasisMult = 4;  ///< lifetime multiplier with Stasis

using Key = std::tuple<int, int, int, int>; // dim, x, y, z

struct Bubble {
    std::uint64_t expires = 0;
    std::uint64_t touched = 0; ///< tick it was stepped on (0 = never)
};

std::map<Key, Bubble>& bubbles() {
    static std::map<Key, Bubble> m;
    return m;
}

std::uint64_t now() { return currentTick(); }

Key keyOf(int dim, BlockPos const& pos) { return {dim, pos.x, pos.y, pos.z}; }

int maxCharges(ItemStackBase const& blower) {
    auto* item = blower.mItem.get();
    return item ? item->getMaxDamage() : 250;
}

int charges(ItemStackBase const& blower) { return std::max(0, maxCharges(blower) - blower.getDamageValue()); }

/// Uses `n` charges of the main hand blower (never breaks it: an empty blower stays).
void useCharges(Player& player, int n) {
    if (isCreative(player)) return;
    ItemStack blower = player.getSelectedItem();
    if (!isItem(blower, ids::BubbleBlower)) return;
    int dmg = std::min<int>(maxCharges(blower), blower.getDamageValue() + n);
    blower.setDamageValue(static_cast<short>(dmg));
    setMainhand(player, blower);
}

bool featherFalling(Actor& actor) {
    auto const& boots = actor.getArmor(SharedTypes::Legacy::ArmorSlot::Feet);
    return enchantLevel(boots, static_cast<int>(Enchant::Type::FeatherFalling)) > 0;
}

} // namespace

bool isTracked(int dim, BlockPos const& pos) { return bubbles().contains(keyOf(dim, pos)); }

bool placeBubble(int dim, BlockPos const& pos, int lifetime) {
    auto* region = regionOf(dim);
    auto const* block = game::blockWith(ids::BubbleBlock);
    if (!region || !block || !game::replaceable(*region, pos)) return false;
    if (!game::setBlock(*region, pos, *block)) return false;
    bubbles()[keyOf(dim, pos)] = Bubble{now() + static_cast<std::uint64_t>(std::max(1, lifetime)), 0};
    playSound(dim, center(pos), "random.pop", 0.8f, 1.6f);
    particle(dim, center(pos), "complementaries:suds");
    log().info("bubble block placed at {} {} {}", pos.x, pos.y, pos.z);
    return true;
}

void pop(int dim, BlockPos const& pos) {
    bubbles().erase(keyOf(dim, pos));
    if (auto* region = regionOf(dim)) {
        if (game::blockIdAt(*region, pos) == ids::BubbleBlock) game::clearBlock(*region, pos);
    }
    particle(dim, center(pos), "complementaries:suds");
    playSound(dim, center(pos), "random.pop", 1.0f, 1.2f);
    log().info("bubble block popped at {} {} {}", pos.x, pos.y, pos.z);
}

void blowTick(Player& player, int usedTicks) {
    if (!config().bubbleBlower.enabled) return;
    auto const& blower = player.getSelectedItem();
    if (!isItem(blower, ids::BubbleBlower)) return;
    if (charges(blower) <= 0 && !isCreative(player)) {
        if (usedTicks % 20 == 1) {
            player.sendMessage(trFor(player, "message.supplementaries.bubble_blower.empty", "Needs soap"));
        }
        return;
    }
    if (usedTicks % 2 != 0) return;
    int  dim  = static_cast<int>(player.getDimensionId());
    Vec3 dir  = lookVector(player);
    Vec3 from = eyePos(player) - Vec3{0, 0.2f, 0};
    float dist = 0.7f + static_cast<float>((usedTicks / 2) % 4) * 0.45f;
    particle(dim, from + dir * dist, "complementaries:bubble");
    if (usedTicks % 10 == 0) playSound(dim, from, "bubble.pop", 0.3f, 1.4f + static_cast<float>(usedTicks % 3) * 0.1f);
    if (usedTicks % 8 == 0) useCharges(player, 1);
}

bool tryRefill(Player& player) {
    auto const& blower = player.getSelectedItem();
    if (!isItem(blower, ids::BubbleBlower) || blower.getDamageValue() <= 0) return false;
    if (!isItem(player.getOffhandSlot(), ids::Soap)) return false;
    ItemStack refilled = blower;
    refilled.setDamageValue(0);
    setMainhand(player, refilled);
    consumeOffhand(player, 1);
    int dim = static_cast<int>(player.getDimensionId());
    particle(dim, eyePos(player) + lookVector(player) * 0.8f, "complementaries:suds");
    playSound(dim, player.getPosition(), "bucket.fill_water", 0.6f, 1.4f);
    log().info("bubble blower refilled by {}", player.getRealName());
    return true;
}

bool useOnBlock(Player& player, BlockPos const& clicked, int face) {
    if (!config().bubbleBlower.enabled) return false;
    auto const& blower = player.getSelectedItem();
    if (!isItem(blower, ids::BubbleBlower)) return false;
    int cost = std::max(0, config().bubbleBlower.bubbleBlockCost);
    if (!isCreative(player) && charges(blower) < cost) {
        player.sendMessage(trFor(player, "message.supplementaries.bubble_blower.empty", "Needs soap"));
        return false;
    }
    auto off = faceOffset(face);
    BlockPos target{clicked.x + off.x, clicked.y + off.y, clicked.z + off.z};
    int lifetime = config().bubbleBlower.bubbleLifetime;
    if (stasisLevel(blower) > 0) lifetime *= kStasisMult;
    if (!placeBubble(static_cast<int>(player.getDimensionId()), target, lifetime)) return false;
    useCharges(player, cost);
    return true;
}

void tick() {
    auto& map = bubbles();
    if (map.empty()) return;
    auto t = now();
    std::vector<std::pair<int, BlockPos>> toPop;
    std::vector<Key> gone;
    for (auto& [key, b] : map) {
        auto [dim, x, y, z] = key;
        BlockPos pos{x, y, z};
        auto* region = regionOf(dim);
        if (!region || !region->hasChunksAt(pos, 0, false)) continue; // unloaded: keep waiting
        if (game::blockIdAt(*region, pos) != ids::BubbleBlock) {
            gone.push_back(key); // broken by hand / replaced
            particle(dim, center(pos), "complementaries:suds");
            continue;
        }
        if (b.expires <= t || (b.touched && t >= b.touched + kPopDelay)) {
            toPop.emplace_back(dim, pos);
            continue;
        }
        // entities standing on (or inside) the bubble
        AABB top{
            Vec3{static_cast<float>(x) - 0.05f, static_cast<float>(y) + 0.5f, static_cast<float>(z) - 0.05f},
            Vec3{static_cast<float>(x) + 1.05f, static_cast<float>(y) + 1.25f, static_cast<float>(z) + 1.05f}
        };
        for (Actor* a : region->fetchEntities(nullptr, top, true, false)) {
            if (!a->hasCategory(ActorCategory::Mob) || !a->isAlive()) continue;
            a->setFallDistance(0.0f); // soft landing
            if (config().bubbleBlower.breakWhenTouched && !b.touched && !featherFalling(*a)) b.touched = t;
        }
    }
    for (auto const& k : gone) map.erase(k);
    for (auto const& [dim, pos] : toPop) pop(dim, pos);
}

void load() {
    bubbles().clear();
    auto j = game::db::load(kDbKey);
    if (!j || !j->is_array()) return;
    auto t = now();
    for (auto const& e : *j) {
        if (!e.is_array() || e.size() < 5) continue;
        bubbles()[{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()}] =
            Bubble{t + e[4].get<std::uint64_t>(), 0};
    }
}

void save() {
    if (!game::db::available()) return;
    auto t   = now();
    auto arr = nlohmann::json::array();
    for (auto const& [key, b] : bubbles()) {
        auto [dim, x, y, z] = key;
        arr.push_back({dim, x, y, z, b.expires > t ? b.expires - t : 0});
    }
    if (arr.empty()) game::db::erase(kDbKey);
    else game::db::save(kDbKey, arr);
}

void clear() { bubbles().clear(); }

} // namespace cm::feature::tool::bubble
