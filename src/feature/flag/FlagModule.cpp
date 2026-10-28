// U14 flag module: event wiring for flags, awnings and buntings.

#include "feature/flag/Flag.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("flag", cm::feature::flag::FlagConfig);

namespace cm::feature::flag {

namespace detail {
extern bool gMovingFlag;
}

namespace {

using GP = core::GlobalPos;

struct GPLess {
    bool operator()(GP const& a, GP const& b) const {
        return std::tie(a.dim, a.x, a.y, a.z) < std::tie(b.dim, b.x, b.y, b.z);
    }
};

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

Player* playerByUuid(mce::UUID const& id) {
    auto level = ll::service::getLevel();
    return level ? level->getPlayer(id) : nullptr;
}

bool ourBlock(std::string_view id) {
    return id.starts_with("complementaries:awning") || id.starts_with("complementaries:bunting_");
}

/// Uses one item from the main hand (not in creative).
void consumeHeld(Player& player, std::string_view id) {
    if (player.isCreative()) return;
    ItemStack it(player.getSelectedItem());
    if (it.isNull() || it.getTypeName() != id) return;
    if (it.mCount <= 1) it = ItemStack();
    else it.remove(1);
    player.setSelectedItem(it);
    player.refreshInventory();
}

/// Survival/creative players with the build ability (not adventure / spectator).
bool mayBuild(Player& player) {
    auto t = player.getPlayerGameType();
    return t != GameType::Adventure && t != GameType::Spectator && player.canUseAbility(AbilitiesIndex::Build);
}

/// A player or mob stands in the block (Java's `isUnobstructed`).
bool occupied(BlockSource& region, BlockPos const& pos) {
    AABB box{Vec3{pos.x, pos.y, pos.z}, Vec3{pos.x + 1, pos.y + 1, pos.z + 1}};
    for (auto* a : region.getEntities(box, 0.f)) {
        if (!a || !(a->isType(ActorType::Player) || a->hasCategory(ActorCategory::Mob))) continue;
        if (a->getTypeName() == kClothId) continue;
        auto const& bb = a->getAABB();
        if (bb.min.x < box.max.x && bb.max.x > box.min.x && bb.min.y < box.max.y && bb.max.y > box.min.y
            && bb.min.z < box.max.z && bb.max.z > box.min.z)
            return true;
    }
    return false;
}

struct PendingFlag {
    FlagData data;
    uint64   tick = 0;
};

struct RemovedFlag {
    GP       pos;
    FlagData data;
    uint64   tick = 0;
};


} // namespace

void playBlockSound(BlockSource& region, BlockPos const& pos, std::string const& sound, float volume, float pitch) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!pkt) return;
    auto& p   = static_cast<PlaySoundPacket&>(*pkt);
    p.mName   = sound;
    p.mPos    = BlockPos{pos.x * 8 + 4, pos.y * 8 + 4, pos.z * 8 + 4};
    p.mVolume = volume;
    p.mPitch  = pitch;
    p.sendTo(pos, region.getDimensionId());
}

class FlagModule {
public:
    static FlagModule& get() {
        static FlagModule m;
        return m;
    }

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [this](ll::event::PlayerInteractBlockEvent& ev) { onInteract(ev); }
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([this](ll::event::BlockChangedEvent& ev) {
            onBlockChanged(ev);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) { onTick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ActorHurtEvent>([this](ll::event::ActorHurtEvent& ev) {
            onHurt(ev);
        }));
        game::db::onWorldClosed([this] { reset(); });
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        reset();
    }

    /// Runs `fn` at the start of the next server tick (after a cancelled interaction rolled back).
    void later(std::function<void()> fn) { mTasks.push_back(std::move(fn)); }
    [[nodiscard]] uint64 tick() const { return mTick; }

private:
    void reset() {
        mTasks.clear();
        mPending.clear();
        mRemoved.clear();
        mSurvival.clear();
        mFalling.clear();
        mBounced.clear();
    }

    // ---- interactions -------------------------------------------------------------------------------
    void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide()) return;
        auto const& cfg    = config::get<FlagConfig>();
        auto&       region = player.getDimensionBlockSource();
        auto const& item   = ev.item();
        BlockPos    pos    = ev.blockPos();
        int         face   = static_cast<int>(ev.face());
        std::string itemId = item.isNull() ? std::string{} : item.getTypeName();
        std::string blockId = ev.block() ? game::blockId(*ev.block()) : game::blockIdAt(region, pos);
        int         dim    = static_cast<int>(region.getDimensionId());
        auto        uuid   = player.getUuid();
        bool        sneaking = player.isSneaking();

        // one action per click (the event can fire more than once for the same use)
        auto key = std::make_tuple(pos.x, pos.y, pos.z, face, mTick);
        if (mLastClick.contains(uuid) && mLastClick[uuid] == key) {
            if (isAwning(itemId) || isBuntingItem(itemId) || (item.isNull() && (isFlag(blockId) || isAwning(blockId)))
                || (isFlag(itemId) && blockId == "minecraft:loom" && !sneaking))
                ev.cancel();
            return;
        }
        mLastClick[uuid] = key;

        // a block's own use comes first (doors, levers, containers, ...), like in Java
        bool interactive = !sneaking && ev.block()
                        && (ev.block()->getBlockType().isInteractiveBlock() || region.tryGetContainer(pos).has_value());

        if (isFlag(itemId) && cfg.flagEnabled) {
            if (blockId == "minecraft:loom" && !sneaking) {
                ev.cancel();
                later([uuid] {
                    if (auto* p = playerByUuid(uuid)) openLoom(*p);
                });
                return;
            }
            BlockPos target = game::replaceable(region, pos) ? pos : offset(pos, face);
            FlagData d;
            d.color  = flagColor(itemId);
            d.facing = game::opposite(game::playerFacing(player));
            d.layers = readLayers(item);
            d.name   = item.getCustomName();
            mPending[game::globalPos(dim, target)] = {d, mTick};
            log().debug("flag: pending flag at {} {} {} (tick {})", target.x, target.y, target.z, mTick);
            return;
        }
        if (item.isNull() && isFlag(blockId) && cfg.flagEnabled && cfg.stickPole) {
            ev.cancel();
            later([dim, pos, up = !sneaking] {
                if (auto* r = regionOf(dim); r && moveFlagAlongPole(*r, pos, up))
                    log().info("flag: moved a flag {} at {} {} {}", up ? "up" : "down", pos.x, pos.y, pos.z);
            });
            return;
        }
        if (item.isNull() && isAwning(blockId) && cfg.awningEnabled && cfg.awningSlant) {
            ev.cancel();
            later([dim, pos] {
                if (auto* r = regionOf(dim); r && toggleAwningSlant(*r, pos))
                    log().info("flag: toggled awning slant at {} {} {}", pos.x, pos.y, pos.z);
            });
            return;
        }
        if (interactive) return;
        if ((isAwning(itemId) && cfg.awningEnabled) || (isBuntingItem(itemId) && cfg.buntingsEnabled)) {
            ev.cancel();
            auto   click = ev.clickPos();
            double rel   = click.y - pos.y;
            if (rel < -0.01 || rel > 1.01) rel = click.y; // relative click positions
            auto looking = nearestLookingDirections(player);
            later([uuid, dim, pos, face, rel, looking, itemId] {
                auto* p = playerByUuid(uuid);
                auto* r = regionOf(dim);
                if (!p || !r) return;
                BlockPos target = game::replaceable(*r, pos) ? pos : offset(pos, face);
                if (!game::replaceable(*r, target) || !mayBuild(*p) || occupied(*r, target)) return;
                if (isAwning(itemId)) {
                    auto s = awningPlacement(*r, target, face, rel, looking);
                    if (!s) return;
                    if (!place(*r, target, itemId, {{"complementaries:facing", s->facing2d}, {"complementaries:bottom", s->bottom ? 1 : 0}, {"complementaries:slanted", s->slanted ? 1 : 0}}))
                        return;
                    playBlockSound(*r, target, "use.cloth", 1.f, 0.8f);
                    log().info(
                        "flag: placed {} facing {} bottom {} slanted {}",
                        itemId,
                        s->facing2d,
                        s->bottom,
                        s->slanted
                    );
                } else {
                    auto b = buntingPlacement(*r, target, buntingColor(itemId), looking);
                    if (!b) return;
                    bool wall = isBuntingWall(b->id);
                    if (!place(*r, target, b->id, {{wall ? "complementaries:facing" : "complementaries:axis", b->state}})) return;
                    playBlockSound(*r, target, "use.cloth", 1.f, 0.8f);
                    log().info("flag: placed {}", b->id);
                }
                consumeHeld(*p, itemId);
            });
        }
    }

    // ---- block changes ------------------------------------------------------------------------------
    void onBlockChanged(ll::event::BlockChangedEvent& ev) {
        if (ev.layer() != 0) return;
        auto& region = ev.blockSource();
        if (region.getLevel().isClientSide()) return;
        auto const& pos    = ev.pos();
        auto        prevId = game::blockId(ev.previousBlock());
        auto        newId  = game::blockId(ev.newBlock());
        int         dim    = static_cast<int>(region.getDimensionId());
        GP          gp     = game::globalPos(dim, pos);

        if (!detail::gMovingFlag && prevId != newId) {
            if (isFlag(newId) && config::get<FlagConfig>().flagEnabled) {
                FlagData d;
                d.color          = flagColor(newId);
                bool fromPlayer  = false;
                if (auto it = mPending.find(gp); it != mPending.end() && mTick - it->second.tick <= 2) {
                    d          = it->second.data;
                    d.color    = flagColor(newId);
                    fromPlayer = true;
                    mPending.erase(it);
                }
                later([gp, d, fromPlayer] {
                    auto* r = regionOf(gp.dim);
                    if (!r || !isFlag(game::blockIdAt(*r, game::blockPos(gp)))) return;
                    // the change can be reported more than once: keep what a placement stored
                    auto stored = loadFlag(gp);
                    if (stored && !fromPlayer) {
                        syncCloth(*r, game::blockPos(gp), *stored);
                        return;
                    }
                    saveFlag(gp, d);
                    syncCloth(*r, game::blockPos(gp), d);
                    log().info("flag: flag placed at {} {} {} with {} pattern(s)", gp.x, gp.y, gp.z, d.layers.size());
                });
            }
            if (isFlag(prevId)) {
                auto data = loadFlag(gp).value_or(FlagData{flagColor(prevId)});
                eraseFlag(gp);
                mRemoved.push_back({gp, data, mTick});
                later([gp] {
                    if (auto* r = regionOf(gp.dim)) removeCloth(*r, game::blockPos(gp));
                });
            }
        }

        // supports: awnings/buntings next to the change (and awnings resting on a slanted one above)
        auto consider = [&](BlockPos const& p) {
            if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight()) return;
            if (!region.hasChunksAt(p, 0, false)) return;
            if (ourBlock(game::blockId(region.getBlock(p)))) mSurvival.insert(game::globalPos(dim, p));
        };
        for (int d = 0; d < 6; ++d) consider(offset(pos, d));
        BlockPos below = offset(pos, Down);
        for (int d = North; d <= East; ++d) consider(offset(below, d));
    }

    void checkSurvival() {
        if (mSurvival.empty()) return;
        auto todo = std::move(mSurvival);
        mSurvival.clear();
        for (auto const& gp : todo) {
            auto* r = regionOf(gp.dim);
            if (!r) continue;
            BlockPos pos = game::blockPos(gp);
            if (!r->hasChunksAt(pos, 0, false)) continue;
            auto const& b  = r->getBlock(pos);
            auto        id = game::blockId(b);
            bool        ok = true;
            if (auto s = awningState(b)) ok = awningCanSurvive(*r, pos, *s);
            else if (isBuntingWall(id)) ok = buntingWallCanSurvive(*r, pos, game::intState(b, "complementaries:facing").value_or(2));
            else if (isBuntingCeiling(id)) ok = buntingCeilingCanSurvive(*r, pos);
            if (!ok) {
                breakWithDrops(*r, pos);
                log().info("flag: {} at {} {} {} lost its support", id, pos.x, pos.y, pos.z);
            }
        }
    }

    /// Puts the patterns/name of broken flags back on their dropped items.
    void patchDrops() {
        std::erase_if(mRemoved, [this](RemovedFlag const& rf) {
            if (mTick == rf.tick) return false; // drops spawn after the block change
            if (rf.data.layers.empty() && rf.data.name.empty()) return true;
            auto* r = regionOf(rf.pos.dim);
            if (!r) return true;
            BlockPos    pos = game::blockPos(rf.pos);
            AABB        box{Vec3{pos.x - 1, pos.y - 1, pos.z - 1}, Vec3{pos.x + 2, pos.y + 2, pos.z + 2}};
            std::string id  = "complementaries:flag_" + std::string(kColorNames[rf.data.color]);
            for (auto* a : r->getEntities(box, 1.0f, ActorType::ItemEntity)) {
                if (!a || !a->isType(ActorType::ItemEntity)) continue;
                auto& item = static_cast<ItemActor*>(a)->item();
                if (static_cast<ItemActor*>(a)->mAge > 5) continue; // only drops of this break
                if (item.isNull() || item.getTypeName() != id || !readLayers(item).empty()) continue;
                if (!rf.data.layers.empty()) writeLayers(item, rf.data.layers);
                if (!rf.data.name.empty()) item.setCustomName(Bedrock::Safety::RedactableString(rf.data.name));
                log().info("flag: dropped flag keeps {} pattern(s)", rf.data.layers.size());
                break;
            }
            return true;
        });
    }

    /// Cloths whose flag is gone (moved by commands, structures, other mods) despawn.
    void validateCloths() {
        auto level = ll::service::getLevel();
        if (!level) return;
        for (auto* a : level->getRuntimeActorList()) {
            if (!a || a->getTypeName() != kClothId) continue;
            auto* r = regionOf(static_cast<int>(a->getDimensionId()));
            if (!r) continue;
            auto const& p = a->getPosition();
            BlockPos    pos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y + 0.01f)), static_cast<int>(std::floor(p.z))};
            if (!r->hasChunksAt(pos, 0, false)) continue;
            if (!isFlag(game::blockIdAt(*r, pos))) a->remove();
        }
    }

    // ---- awning bounce / fall through -------------------------------------------------------------------
    void sendMotion(Actor& actor, std::array<double, 3> const& v) {
        Vec3 m{static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
        const_cast<Vec3&>(actor.getPosDelta()) = m;
        if (actor.isType(ActorType::Player)) {
            auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
            if (!pkt) return;
            auto& mp      = static_cast<SetActorMotionPacket&>(*pkt);
            mp.mRuntimeId = actor.getRuntimeID();
            mp.mMotion    = m;
            mp.sendTo(actor);
        }
    }

    void tickAwnings() {
        auto const& cfg = config::get<FlagConfig>();
        if (!cfg.awningEnabled) return;
        auto level = ll::service::getLevel();
        if (!level) return;
        std::array<BlockSource*, 3> regions{};
        std::array<bool, 3>         looked{};
        // Only falling actors (and actors standing on an awning) need work: the previous tick's
        // velocity of fast fallers is kept for the landing tick.
        std::unordered_map<int64, std::array<double, 3>> falling;
        for (auto* a : level->getRuntimeActorList()) {
            if (!a) continue;
            bool isPlayer = a->isType(ActorType::Player);
            if (!isPlayer && !a->hasCategory(ActorCategory::Mob)) continue;
            auto const& delta = a->getPosDelta();
            int64       id    = a->getOrCreateUniqueID().rawID;
            auto        prev  = mFalling.find(id);
            bool        wasFalling = prev != mFalling.end();
            if (delta.y < -0.32f) falling[id] = {delta.x, delta.y, delta.z};
            if (!wasFalling && !(isPlayer && a->isSneaking() && cfg.awningShiftThrough)) continue;
            if (!a->isAlive()) continue;
            int dim = static_cast<int>(a->getDimensionId());
            if (dim < 0 || dim > 2) continue;
            if (!looked[dim]) {
                regions[dim] = regionOf(dim);
                looked[dim]  = true;
            }
            auto* region = regions[dim];
            if (!region) continue;
            auto     feet = a->getFeetPos();
            BlockPos below{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y - 0.01f)), static_cast<int>(std::floor(feet.z))};
            if (!region->hasChunksAt(below, 0, false)) continue;
            auto s = awningState(region->getBlock(below));
            if (!s) continue;
            float top = static_cast<float>(below.y) + (s->bottom ? 0.5f : 1.0f);
            if (feet.y < top - 0.08f || feet.y > top + 0.12f) continue;
            if (a->isSneaking()) {
                if (cfg.awningShiftThrough && isPlayer) fallThrough(*a, *region, below, *s);
                continue;
            }
            if (!wasFalling) continue;
            if (auto b = mBounced.find(id); b != mBounced.end() && mTick - b->second <= 4) continue;
            auto v = reflect(prev->second, awningNormal(*s, cfg.awningBounceAngle));
            sendMotion(*a, v);
            a->setFallDistance(0);
            mBounced[id] = mTick;
            falling.erase(id);
            playBlockSound(*region, below, "complementaries.block.awning.bounce", 1.f, 1.f);
            log().info("flag: {} bounced off an awning", a->getTypeName());
        }
        mFalling = std::move(falling);
        if (mTick % 20 == 0) std::erase_if(mBounced, [this](auto const& kv) { return mTick - kv.second > 20; });
    }

    void fallThrough(Actor& actor, BlockSource& region, BlockPos const& awning, AwningState const& s) {
        // the collision box starts 1 px above the half's bottom; put the head just under it
        float boxBottom = static_cast<float>(awning.y) + (s.bottom ? 1.f / 16.f : 9.f / 16.f);
        float feetY     = boxBottom - 1.82f;
        int   lowest    = static_cast<int>(std::floor(feetY));
        for (int y = lowest; y < awning.y; ++y) {
            BlockPos p{awning.x, y, awning.z};
            if (!game::replaceable(region, p) && game::blockIdAt(region, p) != "minecraft:air") return;
        }
        auto pos = actor.getFeetPos();
        actor.teleport(Vec3{pos.x, feetY, pos.z}, actor.getDimensionId());
        log().info("flag: {} fell through an awning", actor.getTypeName());
    }

    void onHurt(ll::event::ActorHurtEvent& ev) {
        if (ev.source().mCause != SharedTypes::Legacy::ActorDamageCause::Fall) return;
        auto& a  = ev.self();
        int64 id = a.getOrCreateUniqueID().rawID;
        if (auto it = mBounced.find(id); it != mBounced.end() && mTick - it->second <= 3) {
            ev.cancel();
            return;
        }
        auto* region = regionOf(static_cast<int>(a.getDimensionId()));
        if (!region) return;
        auto     feet = a.getFeetPos();
        BlockPos below{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y - 0.01f)), static_cast<int>(std::floor(feet.z))};
        if (region->hasChunksAt(below, 0, false) && awningState(region->getBlock(below)) && !a.isSneaking()) ev.cancel();
    }

    // ---- tick --------------------------------------------------------------------------------------------
    void onTick() {
        ++mTick;
        if (!mTasks.empty()) {
            auto tasks = std::move(mTasks);
            mTasks.clear();
            for (auto& t : tasks) t();
        }
        checkSurvival();
        patchDrops();
        tickAwnings();
        std::erase_if(mPending, [this](auto const& kv) { return mTick - kv.second.tick > 20; });
        if (mTick % 100 == 0) validateCloths();
        if (mTick % 200 == 0) mLastClick.clear();
    }

    std::vector<ll::event::ListenerPtr>          mListeners;
    std::vector<std::function<void()>>           mTasks;
    std::map<GP, PendingFlag, GPLess>            mPending;
    std::vector<RemovedFlag>                     mRemoved;
    std::set<GP, GPLess>                         mSurvival;
    std::unordered_map<int64, std::array<double, 3>> mFalling; ///< last velocity of fast fallers
    std::unordered_map<int64, uint64>                mBounced; ///< tick of the last bounce
    std::map<mce::UUID, std::tuple<int, int, int, int, uint64>> mLastClick;
    uint64                                       mTick = 0;
};

} // namespace cm::feature::flag

CM_REGISTER_MODULE(cm::feature::flag::FlagModule);
