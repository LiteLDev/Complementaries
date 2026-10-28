// The flag cloth helper entity and the stick pole.

#include "feature/flag/Flag.h"

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"

#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::flag {

namespace detail {
/// Set while the mod itself swaps flag blocks (pole), so the block-change listener ignores them.
bool gMovingFlag = false;
} // namespace detail

namespace {
void setIntProperty(Actor& actor, std::string_view name, int value) {
    auto props = actor.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) return;
    ActorPropertyUtils::tryQueueIntValue(*props, actor, HashedString::computeHash(name), value);
}

bool sameBlock(Actor const& a, BlockPos const& pos) {
    auto const& p = a.getPosition();
    return static_cast<int>(std::floor(p.x)) == pos.x && static_cast<int>(std::floor(p.z)) == pos.z
        && std::abs(p.y - static_cast<float>(pos.y)) < 0.6f;
}
} // namespace

Actor* findCloth(BlockSource& region, BlockPos const& pos) {
    AABB box{
        Vec3{pos.x,     pos.y,     pos.z    },
        Vec3{pos.x + 1, pos.y + 1, pos.z + 1}
    };
    for (auto* a : region.getEntities(box, 1.0f)) {
        if (a && a->getTypeName() == kClothId && sameBlock(*a, pos) && a->isAlive()) return a;
    }
    return nullptr;
}

Actor* syncCloth(BlockSource& region, BlockPos const& pos, FlagData const& data) {
    Actor* cloth = findCloth(region, pos);
    if (!cloth) {
        ActorUniqueID id{};
        Vec3          at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + 0.5f};
        cloth = CommandUtils::spawnEntityAt(region, at, ActorDefinitionIdentifier(std::string(kClothId)), id, nullptr);
        if (!cloth) {
            log().warn("flag: could not spawn the cloth at {} {} {}", pos.x, pos.y, pos.z);
            return nullptr;
        }
    }
    setIntProperty(*cloth, "complementaries:base", data.color);
    setIntProperty(*cloth, "complementaries:facing", data.facing);
    for (int i = 0; i < kMaxLayers; ++i) {
        int v = i < static_cast<int>(data.layers.size()) ? layerProperty(data.layers[i]) : 0;
        setIntProperty(*cloth, "complementaries:l" + std::to_string(i), v);
    }
    return cloth;
}

void removeCloth(BlockSource& region, BlockPos const& pos) {
    AABB box{
        Vec3{pos.x,     pos.y,     pos.z    },
        Vec3{pos.x + 1, pos.y + 1, pos.z + 1}
    };
    for (auto* a : region.getEntities(box, 1.0f)) {
        if (a && a->getTypeName() == kClothId && sameBlock(*a, pos)) a->remove();
    }
}

bool moveFlagAlongPole(BlockSource& region, BlockPos const& pos, bool up) {
    BlockPos next = offset(pos, up ? Up : Down);
    if (!isStick(game::blockIdAt(region, next))) return false;
    auto const& flagBlock  = region.getBlock(pos);
    auto const& stickBlock = region.getBlock(next);
    if (!isFlag(game::blockId(flagBlock))) return false;
    auto from           = game::globalPos(region, pos);
    auto to             = game::globalPos(region, next);
    auto data           = loadFlag(from).value_or(FlagData{flagColor(game::blockId(flagBlock))});
    detail::gMovingFlag = true;
    game::setBlock(region, next, flagBlock);
    game::setBlock(region, pos, stickBlock);
    detail::gMovingFlag = false;
    eraseFlag(from);
    saveFlag(to, data);
    removeCloth(region, pos);
    syncCloth(region, next, data);
    return true;
}

} // namespace cm::feature::flag
