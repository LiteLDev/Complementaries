// Minecart with Dispenser: a chest-type minecart entity (BP entities/dispenser_minecart.json) with a
// 9-slot inventory. It fires one item, with that item's dispense behaviour, every time it rolls onto
// a powered activator rail. Breaking it drops the cart item and its contents.

#include <cmath>
#include <random>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/item/MinecartChest.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/DispenserBlock.h"

#include "feature/circuit/Circuit.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace cm::feature::circuit {

namespace {

/// Per cart (unique id): was it on a powered activator rail last tick?
std::unordered_map<std::int64_t, bool>& activeCarts() {
    static std::unordered_map<std::int64_t, bool> m;
    return m;
}

bool isCart(Actor const& actor) { return actor.getTypeName() == kCart; }

/// The block the cart rides on: its own block, or the one below when it sits on the rail's top.
bool onPoweredActivatorRail(BlockSource& region, Vec3 const& pos) {
    for (float dy : {0.0f, -0.4f}) {
        auto bp = game::blockPosOf(Vec3{pos.x, pos.y + dy, pos.z});
        if (!region.hasChunksAt(bp, 0, false)) return false;
        auto const& b = region.getBlock(bp);
        if (b.getTypeName() == "minecraft:activator_rail") return game::intState(b, "rail_data_bit").value_or(0) != 0;
    }
    return false;
}

/// Facing (2 north, 3 south, 4 west, 5 east) along the cart's motion, or its yaw when still.
int travelFacing(Actor const& actor) {
    auto const& d = actor.getPosDelta();
    if (std::abs(d.x) > 1e-3f || std::abs(d.z) > 1e-3f) {
        if (std::abs(d.x) > std::abs(d.z)) return d.x > 0 ? 5 : 4;
        return d.z > 0 ? 3 : 2;
    }
    switch (game::playerFacing(actor)) { // Java 2D facing: 0 S, 1 W, 2 N, 3 E
    case 0:
        return 3;
    case 1:
        return 4;
    case 2:
        return 2;
    default:
        return 5;
    }
}

} // namespace

/// Fires one item from the cart. Returns true if something was dispensed.
bool dispenseFromCart(Actor& cart) {
    auto container = cart.getEntityContext().tryGetComponent<ContainerComponent>();
    if (!container) return false;
    Container&       inv    = *container->mContainer;
    auto&            region = cart.getDimensionBlockSource();
    auto&            level  = region.getLevel();
    auto const       pos    = cart.getPosition();
    std::vector<int> slots;
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (!inv.getItem(i).isNull()) slots.push_back(i);
    }
    if (slots.empty()) {
        level.broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::SoundClickFail, pos, 1200, nullptr);
        return false;
    }
    static std::mt19937 rng{std::random_device{}()};
    int                 slot   = slots[std::uniform_int_distribution<size_t>(0, slots.size() - 1)(rng)];
    int                 facing = travelFacing(cart);
    auto                o      = facingOffset(facing);
    Vec3                at{pos.x + o.x * 0.9f, pos.y + 0.5f, pos.z + o.z * 0.9f};
    ItemStack           item = inv.getItem(slot);
    auto const*         it   = item.mItem.get();
    bool                done = it && it->dispense(region, inv, slot, at, static_cast<uchar>(facing));
    if (!done) DispenserBlock::ejectItem(region, at, static_cast<uchar>(facing), item, inv, slot, 1);
    level.broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::SoundClick, pos, 1000, nullptr);
    log().info("dispenser minecart fired {} towards {}", item.getTypeName(), facing);
    return true;
}

void tickCarts() {
    if (!config::get<CircuitConfig>().dispenserMinecartEnabled) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    auto&                                  active = activeCarts();
    std::unordered_map<std::int64_t, bool> seen;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || !isCart(*actor)) continue;
        auto id  = actor->getOrCreateUniqueID().rawID;
        bool now = onPoweredActivatorRail(actor->getDimensionBlockSource(), actor->getPosition());
        auto it  = active.find(id);
        bool was = it != active.end() && it->second;
        if (now && !was) dispenseFromCart(*actor);
        seen[id] = now;
    }
    active = std::move(seen);
}

void clearCartCache() { activeCarts().clear(); }

LL_TYPE_INSTANCE_HOOK(
    CartDestroyHook,
    ll::memory::HookPriority::Normal,
    MinecartChest,
    &MinecartChest::$destroy,
    void,
    ActorDamageSource const& source,
    bool                     dropMinecartComponents
) {
    if (!isCart(*this)) return origin(source, dropMinecartComponents);
    auto& region = getDimensionBlockSource();
    auto  pos    = getPosition();
    if (auto container = getEntityContext().tryGetComponent<ContainerComponent>()) {
        container->dropContents(region, pos, true);
    }
    if (dropMinecartComponents) {
        ItemStack item;
        item.reinit(kCart, 1, 0);
        if (!item.isNull()) BlockType::popResource(region, game::blockPosOf(pos), item, this);
    }
    remove();
}

void installCart() { CartDestroyHook::hook(); }
void uninstallCart() { CartDestroyHook::unhook(); }

} // namespace cm::feature::circuit
