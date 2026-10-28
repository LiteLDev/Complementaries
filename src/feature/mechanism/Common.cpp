// Shared helpers of the mechanism unit: block states, redstone reading, effects.

#include <algorithm>
#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/entity/components/PlayerPositionModeComponent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/MovePlayerPacket.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/redstone/circuit/components/CircuitComponentList.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"
#include "mc/world/redstone/circuit/components/CircuitComponentType.h"

#include "feature/mechanism/Mechanism.h"
#include "game/Blocks.h"
#include "mod/Config.h"

namespace cm::feature::mechanism {

namespace rot = rotation;

MechanismConfig const& config() {
    static MechanismConfig c;
    c        = cm::config::get<MechanismConfig>();
    auto clf = [](float v, float lo, float hi) { return std::clamp(std::isfinite(v) ? v : lo, lo, hi); };
    c.springLauncher.launcherVelocity   = clf(c.springLauncher.launcherVelocity, 0.f, 16.f);
    c.springLauncher.fallHeightRequired = clf(c.springLauncher.fallHeightRequired, 0.f, 512.f);
    c.bellows.period                    = std::clamp(c.bellows.period, 10, 512);
    c.bellows.power                     = clf(c.bellows.power, 0.f, 16.f);
    c.bellows.range                     = std::clamp(c.bellows.range, 1, 16);
    c.turnTable.period                  = std::clamp(c.turnTable.period, 1, 256);
    return c;
}

// ---- kinds --------------------------------------------------------------------------------------

Kind kindOf(Block const& block) {
    std::string_view n = block.getTypeName();
    if (!n.starts_with("complementaries:")) return Kind::None;
    if (n == ids::kLauncher) return Kind::Launcher;
    if (n == ids::kHead) return Kind::Head;
    if (n == ids::kArm) return Kind::Arm;
    if (n == ids::kBellows) return Kind::Bellows;
    if (n == ids::kTurnTable) return Kind::TurnTable;
    if (n == ids::kCog) return Kind::Cog;
    if (n == ids::kCrank) return Kind::Crank;
    return Kind::None;
}

Kind kindAt(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return Kind::None;
    if (!region.hasChunksAt(pos, 0, false)) return Kind::None;
    return kindOf(region.getBlock(pos));
}

// ---- states -------------------------------------------------------------------------------------

StateList statesOf(Block const& block) {
    StateList           out;
    CompoundTag const& tag = block.mSerializationId;
    if (!tag.contains("states")) return out;
    auto const& states = tag.at("states");
    if (!states.is_object()) return out;
    for (auto const& [name, v] : states.items()) {
        switch (v.getId()) {
        case Tag::Type::Byte:
            out.emplace_back(name, rot::Value{v.get<ByteTag>().data != 0});
            break;
        case Tag::Type::Int:
            out.emplace_back(name, rot::Value{static_cast<int>(v.get<IntTag>().data)});
            break;
        case Tag::Type::String:
            out.emplace_back(name, rot::Value{std::string(static_cast<std::string const&>(v.get<StringTag>()))});
            break;
        default:
            break;
        }
    }
    return out;
}

std::optional<rot::Value> stateOf(Block const& block, std::string_view name) {
    for (auto& [n, v] : statesOf(block))
        if (n == name) return v;
    return std::nullopt;
}

int intStateOf(Block const& block, std::string_view name, int fallback) {
    auto v = stateOf(block, name);
    if (!v) return fallback;
    if (auto* i = std::get_if<int>(&*v)) return *i;
    if (auto* b = std::get_if<bool>(&*v)) return *b ? 1 : 0;
    return fallback;
}

bool boolStateOf(Block const& block, std::string_view name, bool fallback) {
    auto v = stateOf(block, name);
    if (!v) return fallback;
    if (auto* b = std::get_if<bool>(&*v)) return *b;
    if (auto* i = std::get_if<int>(&*v)) return *i != 0;
    return fallback;
}

namespace {
/// Looks a block up by its serialization id (`{name, states, version}`), with `changes` applied to
/// the states: the same path structures and `/setblock` take, so string states work too.
Block const* lookup(CompoundTag tag, StateList const& changes) {
    if (!tag.contains("states") || !tag.at("states").is_object()) tag["states"] = CompoundTag{};
    auto& states = tag.at("states");
    for (auto const& [n, v] : changes) {
        if (auto const* i = std::get_if<int>(&v)) states[n] = IntTag(*i);
        else if (auto const* b = std::get_if<bool>(&v)) states[n] = ByteTag(*b ? 1 : 0);
        else states[n] = StringTag(std::get<std::string>(v));
    }
    auto b = Block::tryGetFromRegistry(tag);
    return b ? b.as_ptr() : nullptr;
}
} // namespace

Block const* blockFrom(std::string_view id, StateList const& states) {
    auto type = BlockType::tryGetFromRegistry(HashedString(std::string(id)));
    if (!type || !type->mDefaultState) return nullptr;
    return lookup(type->mDefaultState->mSerializationId, states);
}

Block const* withStates(Block const& block, StateList const& changes) {
    return lookup(block.mSerializationId, changes);
}

namespace {
int dirOfName(std::string const* s) {
    if (!s) return -1;
    for (int d = 0; d < 6; ++d)
        if (rot::detail::kDirNames[static_cast<std::size_t>(d)] == *s) return d;
    return -1;
}
std::string nameOfDir(int d) { return std::string(rot::detail::kDirNames[static_cast<std::size_t>(d) % 6]); }
} // namespace

int facingOf(Block const& block) {
    switch (kindOf(block)) {
    case Kind::Launcher:
    case Kind::Bellows:
    case Kind::TurnTable: {
        auto v = stateOf(block, "minecraft:facing_direction");
        int  d = v ? dirOfName(std::get_if<std::string>(&*v)) : -1;
        return d < 0 ? -1 : rot::opposite(d);
    }
    case Kind::Head:
    case Kind::Arm:
        return std::clamp(intStateOf(block, "complementaries:facing", rot::kUp), 0, 5);
    case Kind::Crank: {
        auto v = stateOf(block, "minecraft:block_face");
        return v ? dirOfName(std::get_if<std::string>(&*v)) : -1;
    }
    default:
        return -1;
    }
}

StateList facingStates(Kind kind, int dir) {
    switch (kind) {
    case Kind::Launcher:
    case Kind::Bellows:
    case Kind::TurnTable:
        return {{"minecraft:facing_direction", rot::Value{nameOfDir(rot::opposite(dir))}}};
    case Kind::Head:
    case Kind::Arm:
        return {{"complementaries:facing", rot::Value{dir}}};
    case Kind::Crank:
        return {{"minecraft:block_face", rot::Value{nameOfDir(dir)}}};
    default:
        return {};
    }
}

BlockPos offset(BlockPos const& pos, int dir, int n) {
    auto v = rot::dirVec(dir);
    return BlockPos{pos.x + v.x * n, pos.y + v.y * n, pos.z + v.z * n};
}

bool place(BlockSource& region, BlockPos const& pos, Block const& block) { return game::setBlock(region, pos, block); }

bool placeAir(BlockSource& region, BlockPos const& pos) { return game::clearBlock(region, pos); }

void dropItem(BlockSource& region, BlockPos const& pos, std::string_view id) {
    ItemStack item;
    item.reinit(id, 1, 0);
    if (item.isNull()) return;
    BlockType::popResource(region, pos, item, nullptr);
}

// ---- redstone -----------------------------------------------------------------------------------

namespace {
BaseCircuitComponent* componentAt(BlockSource& region, BlockPos const& pos) {
    auto&                           dim = region.getDimension();
    std::unique_ptr<CircuitSystem>& up  = dim.mCircuitSystem;
    auto*                           cs  = up.get();
    if (!cs) return nullptr;
    auto& all = cs->mSceneGraph->mAllComponents;
    auto  it  = all.find(pos);
    return it == all.end() ? nullptr : it->second.get();
}
} // namespace

int consumerPower(BlockSource& region, BlockPos const& pos) {
    auto* c = componentAt(region, pos);
    return c ? std::clamp(c->getStrength(), 0, 15) : 0;
}

std::pair<int, int> componentInfo(BlockSource& region, BlockPos const& pos) {
    auto* c = componentAt(region, pos);
    return c ? std::pair{c->getStrength(), c->getDirection()} : std::pair{-1, -1};
}

namespace {
/// Strength of `c` counting only sources that `ignore` does not reject.
int strengthWithout(BaseCircuitComponent& c, std::function<bool(BlockPos const&)> const& ignore) {
    int s = std::clamp(c.getStrength(), 0, 15);
    if (!ignore || s <= 0) return s;
    auto const& items = c.mSources->mComponents;
    if (items.empty()) return s;
    int  best    = 0;
    bool skipped = false;
    for (auto const& item : items) {
        if (ignore(item.mPos)) {
            skipped = true;
            continue;
        }
        if (item.mComponent) best = std::max(best, item.mComponent->getStrength() - item.mDampening);
    }
    return skipped ? std::clamp(best, 0, s) : s;
}
} // namespace

int signalInto(BlockSource& region, BlockPos const& pos, int dir, std::function<bool(BlockPos const&)> const& ignore) {
    BlockPos np = offset(pos, dir);
    auto*    c  = componentAt(region, np);
    if (!c) return 0;
    int s = std::clamp(c->getStrength(), 0, 15);
    if (s <= 0) return 0;
    if (kindAt(region, np) == Kind::Cog) return 0; // cogs are resolved per cluster
    auto type  = c->getCircuitComponentType();
    auto group = static_cast<CircuitComponentType>(
        static_cast<uint64>(type) & static_cast<uint64>(CircuitComponentType::GroupMask)
    );
    switch (type) {
    case CircuitComponentType::TransporterComponent:
        // redstone dust powers the block below it and the blocks it runs into, never the one above
        return dir == rot::kDown ? 0 : strengthWithout(*c, ignore);
    case CircuitComponentType::PoweredBlockComponent:
        return strengthWithout(*c, ignore);
    case CircuitComponentType::RedstoneTorchCapacitor: {
        // a torch powers everything but the block it stands on
        Block const& torch = region.getBlock(np);
        auto         v     = stateOf(torch, "torch_facing_direction");
        auto*        name  = v ? std::get_if<std::string>(&*v) : nullptr;
        int          d     = name ? dirOfName(name) : -1;
        BlockPos     att   = (name && *name == "top") ? offset(np, rot::kDown) : d >= 2 ? offset(np, rot::opposite(d)) : np;
        return att == pos ? 0 : s;
    }
    case CircuitComponentType::RepeaterCapacitor:
    case CircuitComponentType::ComparatorCapacitor:
        // only through their output side: the component's direction points back to the input
        return offset(np, rot::opposite(c->getDirection() % 6)) == pos ? s : 0;
    case CircuitComponentType::PulseCapacitor: {
        // an observer outputs at its back
        Block const& obs = region.getBlock(np);
        auto         v   = stateOf(obs, "minecraft:facing_direction");
        int          d   = v ? dirOfName(std::get_if<std::string>(&*v)) : -1;
        if (d < 0) d = intStateOf(obs, "facing_direction", -1);
        return (d >= 0 && offset(np, rot::opposite(d)) == pos) ? s : 0;
    }
    default:
        break;
    }
    if (group == CircuitComponentType::ProducerComponent || group == CircuitComponentType::PoweredBlockComponent
        || group == CircuitComponentType::CapacitorComponent)
        return s;
    return 0;
}

std::vector<Actor*> entitiesIn(BlockSource& region, AABB const& box) {
    std::vector<Actor*> out;
    for (Actor* a : region.getEntities(box)) {
        if (a) out.push_back(a);
    }
    auto overlaps = [&](AABB const& b) {
        return b.min.x < box.max.x && b.max.x > box.min.x && b.min.y < box.max.y && b.max.y > box.min.y
            && b.min.z < box.max.z && b.max.z > box.min.z;
    };
    if (auto level = ll::service::getLevel()) {
        auto dim = region.getDimensionId();
        level->forEachPlayer([&](Player& p) {
            if (p.getDimensionId() == dim && overlaps(p.getAABB())
                && std::find(out.begin(), out.end(), static_cast<Actor*>(&p)) == out.end())
                out.push_back(&p);
            return true;
        });
    }
    return out;
}

// ---- effects ------------------------------------------------------------------------------------

Vec3 centerOf(BlockPos const& pos) {
    return Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
}

void playSound(BlockSource& region, Vec3 const& pos, std::string const& name, float volume, float pitch) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!pkt) return;
    auto& p    = static_cast<PlaySoundPacket&>(*pkt);
    p.mName    = name;
    p.mPos     = BlockPos{
        static_cast<int>(std::floor(pos.x * 8.f)),
        static_cast<int>(std::floor(pos.y * 8.f)),
        static_cast<int>(std::floor(pos.z * 8.f))
    };
    p.mVolume  = volume;
    p.mPitch   = pitch;
    p.sendTo(game::blockPosOf(pos), region.getDimensionId());
}

void spawnParticle(BlockSource& region, Vec3 const& pos, std::string const& effect) {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->spawnParticleEffect(effect, pos, &region.getDimension());
}

void setMotion(Actor& actor, Vec3 const& motion) {
    const_cast<Vec3&>(actor.getPosDelta()) = motion;
    if (!actor.isPlayer() || actor.isSimulatedPlayer()) return;
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
    if (!pkt) return;
    auto& p      = static_cast<SetActorMotionPacket&>(*pkt);
    p.mRuntimeId = actor.getRuntimeID();
    p.mMotion    = motion;
    p.sendTo(static_cast<Player&>(actor));
}

void turnEntity(Actor& actor, float degrees) {
    Vec2 r = actor.getRotation();
    r.y += degrees;
    r.y = std::remainder(r.y, 360.f);
    const_cast<Vec2&>(actor.getRotation()) = r;
    actor.setYHeadRot(actor.getRotation().y);
    if (!actor.isPlayer() || actor.isSimulatedPlayer()) return;
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::MovePlayer);
    if (!pkt) return;
    auto& p          = static_cast<MovePlayerPacket&>(*pkt);
    p.mPlayerID      = actor.getRuntimeID();
    p.mPos           = actor.getPosition();
    p.mRot           = actor.getRotation();
    p.mYHeadRot      = actor.getRotation().y;
    p.mResetPosition = PlayerPositionModeComponent::PositionMode::OnlyHeadRot;
    p.mOnGround      = actor.isOnGround();
    p.sendTo(static_cast<Player&>(actor));
}

void actionBar(Player& player, std::string const& text) {
    if (player.isSimulatedPlayer()) return;
    auto                          pkt = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(text);
    pkt.mBody = message;
    pkt.sendTo(player);
}

} // namespace cm::feature::mechanism
