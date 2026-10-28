#include "feature/lighting/Lighting.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ShortTag.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/projectile/ThrownPotion.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/lighting/LightingConfig.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("lighting", cm::feature::lighting::LightingConfig);

namespace cm::feature::lighting {

namespace {

LightingConfig const& cfg() { return config::get<LightingConfig>(); }

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

BlockPos offset(BlockPos const& pos, int facing) {
    switch (facing) {
    case 0:
        return {pos.x, pos.y - 1, pos.z};
    case 1:
        return {pos.x, pos.y + 1, pos.z};
    case 2:
        return {pos.x, pos.y, pos.z - 1};
    case 3:
        return {pos.x, pos.y, pos.z + 1};
    case 4:
        return {pos.x - 1, pos.y, pos.z};
    case 5:
        return {pos.x + 1, pos.y, pos.z};
    default:
        return pos;
    }
}

void playSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume = 1.f, float pitch = 1.f) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!pkt) return;
    auto& p    = static_cast<PlaySoundPacket&>(*pkt);
    p.mName    = name;
    p.mPos     = BlockPos{pos.x * 8 + 4, pos.y * 8 + 4, pos.z * 8 + 4}; // network blocks are 1/8 units
    p.mVolume  = volume;
    p.mPitch   = pitch;
    pkt->sendTo(pos, region.getDimensionId());
}

std::shared_ptr<Packet> particlePacket(int dim, Vec3 const& at, std::string const& name) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::SpawnParticleEffect);
    if (!pkt) return nullptr;
    auto& p               = static_cast<SpawnParticleEffectPacket&>(*pkt);
    p.mVanillaDimensionId = static_cast<uchar>(dim);
    p.mActorId->rawID     = -1;
    p.mPos                = at;
    p.mEffectName         = name;
    p.mMolangVariables    = std::nullopt;
    return pkt;
}

void particleAround(BlockSource& region, BlockPos const& pos, Vec3 const& at, std::string const& name) {
    if (auto pkt = particlePacket(static_cast<int>(region.getDimensionId()), at, name)) {
        pkt->sendTo(pos, region.getDimensionId());
    }
}

char const* flameParticle(Flame f) {
    switch (f) {
    case Flame::Soul:
        return "minecraft:blue_flame_particle";
    case Flame::Ender:
        return "complementaries:ender_flame";
    case Flame::Glow:
        return "complementaries:glow_flame";
    case Flame::Green:
        return "complementaries:green_flame";
    default:
        return "minecraft:basic_flame_particle";
    }
}

/// World position of the flame of a sconce (or of each candle of a candle holder).
std::vector<Vec3> flamePositions(Block const& block, BlockInfo info, BlockPos const& pos) {
    std::vector<Vec3> out;
    auto  face = stringState(block, kFace);
    float cx = pos.x + 0.5f, cz = pos.z + 0.5f;
    if (info.kind == Kind::Sconce || info.kind == Kind::SconceLever) {
        auto [dx, dz] = towardsSupport(face);
        out.push_back(Vec3{cx + dx * 0.125f, pos.y + 0.8f, cz + dz * 0.125f});
        return out;
    }
    if (info.kind != Kind::CandleHolder) return out;
    // Candle layout of the model (pixels from the centre: x, z, height); see the geometry.
    static constexpr float kPos[4][4][3] = {
        {{0, 0, 6}},
        {{-2, 0, 6}, {2, 0, 5}},
        {{0, -2, 6}, {-2, 1.5f, 5}, {2, 1.5f, 4}},
        {{-2, -2, 6}, {2, -2, 5}, {-2, 2, 5}, {2, 2, 4}},
    };
    int   n    = std::clamp(intState(block, kCandles, 1), 1, 4);
    float top  = face == "down" ? 8.f : 4.f;
    auto  rotF = face == "up" || face == "down" ? stringState(block, kCardinal) : face;
    int   rot  = rotF == "west" ? 90 : rotF == "south" ? 180 : rotF == "east" ? 270 : 0;
    float rad  = static_cast<float>(rot) * 3.14159265f / 180.f;
    float cs = std::cos(rad), sn = std::sin(rad);
    for (int i = 0; i < n; ++i) {
        float x = kPos[n - 1][i][0], z = kPos[n - 1][i][1], h = kPos[n - 1][i][2];
        float rx = x * cs + z * sn, rz = -x * sn + z * cs;
        out.push_back(Vec3{cx + rx / 16.f, pos.y + (top + h + 1.5f) / 16.f, cz + rz / 16.f});
    }
    return out;
}

bool isCreative(Player const& p) { return p.isCreative(); }

/// Changes the item in `slot` of the player `id` (next tick, after a cancelled interaction).
void useItem(std::int64_t id, int slot, std::string const& itemId, bool damage) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto* player = level->getPlayer(ActorUniqueID{id});
    if (!player || isCreative(*player)) return;
    auto& inv = player->getInventory();
    if (slot < 0 || slot >= inv.getContainerSize()) return;
    ItemStack item = inv.getItem(slot);
    if (item.isNull() || item.getTypeName() != itemId) return;
    if (damage) {
        auto* type = item.mItem.get();
        int   max  = type ? type->getMaxDamage() : 0;
        int   next = item.getDamageValue() + 1;
        if (max > 0 && next >= max) item = ItemStack::EMPTY_ITEM(); // broke
        else item.setDamageValue(static_cast<short>(next));
    } else {
        item.remove(1);
        if (item.mCount == 0) item = ItemStack::EMPTY_ITEM();
    }
    inv.setItem(slot, item);
    player->refreshInventory();
}

std::string posText(BlockPos const& p) {
    return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z);
}

} // namespace

// ------------------------------------------------------------------------------------------------
// projectile hook

LL_TYPE_INSTANCE_HOOK(
    LightingProjectileHitHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    ::Actor&           owner,
    ::HitResult const& hitResult
) {
    if (!owner.getLevel().isClientSide()) Lighting::get().onProjectileHit(owner, hitResult);
    origin(owner, hitResult);
}

// ------------------------------------------------------------------------------------------------

Lighting& Lighting::get() {
    static Lighting instance;
    return instance;
}

BlockInfo Lighting::infoOf(BlockType const& type) {
    if (mTypes.empty()) rebuildTypes();
    auto it = mTypes.find(&type);
    return it == mTypes.end() ? BlockInfo{} : it->second;
}

void Lighting::rebuildTypes() {
    mTypes.clear();
    for (auto const& id : allBlockIds()) {
        if (auto type = BlockType::tryGetFromRegistry(HashedString(id))) mTypes[type.as_ptr()] = classify(id);
    }
}

void Lighting::reset() {
    mPending.clear();
    mSwings.clear();
    mTypes.clear();
    mLastUse.clear();
    mParticles = 0;
}

bool Lighting::setLit(BlockSource& region, BlockPos const& pos, bool lit, bool effects) {
    auto const& block = region.getBlock(pos);
    auto        info  = classify(block);
    if (!isLightable(info.kind)) return false;
    if (boolState(block, kLit) == lit) return false;
    if (lit && waterlogged(region, pos)) return false;
    auto const* next = withStates(block, {{kLit, lit}});
    if (!next || !game::setBlock(region, pos, *next)) return false;
    if (effects) {
        if (!lit) {
            bool candle = info.kind == Kind::CandleHolder;
            playSound(region, pos, candle ? "extinguish.candle" : "random.fizz", candle ? 1.f : 0.5f, 1.f);
            for (auto const& at : flamePositions(*next, info, pos)) {
                particleAround(region, pos, at, "minecraft:basic_smoke_particle");
            }
        }
    }
    log().info("lighting: {} {} at {}", lit ? "lit" : "extinguished", block.getTypeName(), posText(pos));
    return true;
}

bool Lighting::addCandle(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (classify(block).kind != Kind::CandleHolder) return false;
    int n = intState(block, kCandles, 1);
    if (n >= 4) return false;
    auto const* next = withStates(block, {{kCandles, n + 1}});
    if (!next || !game::setBlock(region, pos, *next)) return false;
    playSound(region, pos, "use.candle");
    log().info("lighting: candle holder at {} has {} candles", posText(pos), n + 1);
    return true;
}

bool Lighting::toggleLever(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (classify(block).kind != Kind::SconceLever) return false;
    bool on   = !boolState(block, kPowered);
    auto const* next = withStates(block, {{kPowered, on}});
    if (!next || !game::setBlock(region, pos, *next)) return false;
    playSound(region, pos, "random.click", 0.3f, on ? 0.6f : 0.5f);
    log().info("lighting: sconce lever at {} {}", posText(pos), on ? "on" : "off");
    return true;
}

bool Lighting::canHangOn(BlockSource& region, BlockPos const& support, int facing) {
    if (facing < 2 || facing > 5) return false;
    auto const& b = region.getBlock(support);
    if (b.isAir()) return false;
    return b.canProvideSupport(static_cast<uchar>(facing), BlockSupportType::Center);
}

bool Lighting::placeWallLantern(BlockSource& region, BlockPos const& support, int facing, int lantern) {
    if (lantern < 0 || lantern >= static_cast<int>(std::size(kLanterns))) return false;
    if (!canHangOn(region, support, facing)) return false;
    auto at = offset(support, facing);
    if (!game::replaceable(region, at)) return false;
    auto const* block = makeBlock(
        "complementaries:wall_lantern",
        {{kFace, std::string(faceName(facing))}, {kLantern, lantern}, {kSwing, 0}}
    );
    if (!block || !game::setBlock(region, at, *block)) return false;
    playSound(region, at, "block.lantern.place");
    log().info("lighting: wall lantern ({}) placed at {}", kLanterns[lantern], posText(at));
    return true;
}

void Lighting::push(int dim, BlockPos const& pos, float impulse) {
    auto& s = mSwings[core::GlobalPos{dim, pos.x, pos.y, pos.z}];
    s.vel   = std::clamp(s.vel + impulse, -6.f, 6.f);
}

void Lighting::onProjectileHit(Actor const& projectile, HitResult const& hit) {
    auto type    = projectile.getEntityTypeId();
    bool water   = false;
    bool burning = false;
    if (type == ActorType::ThrownPotion) {
        CompoundTag tag;
        static_cast<ThrownPotion const&>(projectile).$addAdditionalSaveData(tag);
        auto it = tag.mTags.find("PotionId");
        water   = it != tag.mTags.end() && it->second.hold<ShortTag>() && it->second.get<ShortTag>().data == 0;
    } else {
        burning = projectile.isOnFire() || type == ActorType::SmallFireball || type == ActorType::LargeFireball;
    }
    if (!water && !burning) return;
    onProjectileHit(
        static_cast<int>(projectile.getDimensionId()),
        hit.mBlock,
        hit.mType == HitResultType::Tile,
        hit.mPos,
        burning,
        water
    );
}

void Lighting::onProjectileHit(int dim, BlockPos const& hitBlock, bool hitTile, Vec3 const& impact, bool burning, bool water) {
    auto const& c = cfg();
    if (water && c.waterPotionsExtinguish) {
        BlockPos center = game::blockPosOf(impact);
        defer([dim, center] {
            auto* region = regionOf(dim);
            if (!region) return;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        BlockPos p{center.x + dx, center.y + dy, center.z + dz};
                        if (region->hasChunksAt(p, 0, false)) setLit(*region, p, false);
                    }
        });
        return;
    }
    if (!burning || !hitTile || !c.fireProjectilesLight) return;
    BlockPos pos = hitBlock;
    defer([dim, pos] {
        if (auto* region = regionOf(dim)) setLit(*region, pos, true);
    });
}

// ------------------------------------------------------------------------------------------------
// ticking

void Lighting::tick() {
    ++mTick;
    if (!mPending.empty()) {
        auto work = std::move(mPending);
        mPending.clear();
        for (auto& fn : work) fn();
    }
    tickSwings();
    if (cfg().wallLanternSwing && (mTick & 1) == 0) tickCollisions();
    if (cfg().particles) tickParticles();
}

void Lighting::tickSwings() {
    for (auto it = mSwings.begin(); it != mSwings.end();) {
        auto& [gp, s] = *it;
        auto* region  = regionOf(gp.dim);
        BlockPos pos{gp.x, gp.y, gp.z};
        if (!region || !region->hasChunksAt(pos, 0, false)) {
            it = mSwings.erase(it);
            continue;
        }
        auto const& block = region->getBlock(pos);
        if (infoOf(block.getBlockType()).kind != Kind::WallLantern) {
            it = mSwings.erase(it);
            continue;
        }
        // damped pendulum (period ~1.3 s, settles in ~3 s)
        s.vel += -0.06f * s.angle - 0.05f * s.vel;
        s.angle = std::clamp(s.angle + s.vel, -22.f, 22.f);
        bool done = std::abs(s.angle) < 1.5f && std::abs(s.vel) < 0.3f;
        int  step = done ? 0 : std::clamp(static_cast<int>(std::lround(s.angle / 5.f)), -4, 4);
        int  frame = step >= 0 ? step : 4 - step;
        if (frame != intState(block, kSwing)) {
            if (auto const* next = withStates(block, {{kSwing, frame}})) game::setBlock(*region, pos, *next);
        }
        it = done ? mSwings.erase(it) : std::next(it);
    }
}

void Lighting::tickCollisions() {
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor) continue;
        auto type = actor->getEntityTypeId();
        if (type == ActorType::ItemEntity || type == ActorType::Experience) continue;
        auto const& d = actor->getPosDelta();
        float       speed = std::sqrt(d.x * d.x + d.z * d.z);
        if (speed < 0.02f) continue;
        auto const& box = actor->getAABB();
        auto&       region = actor->getDimensionBlockSource();
        int         dim    = static_cast<int>(actor->getDimensionId());
        for (int x = static_cast<int>(std::floor(box.min.x)); x <= static_cast<int>(std::floor(box.max.x - 1e-3f)); ++x)
            for (int y = static_cast<int>(std::floor(box.min.y)); y <= static_cast<int>(std::floor(box.max.y - 1e-3f)); ++y)
                for (int z = static_cast<int>(std::floor(box.min.z)); z <= static_cast<int>(std::floor(box.max.z - 1e-3f)); ++z) {
                    BlockPos p{x, y, z};
                    if (!region.hasChunksAt(p, 0, false)) continue;
                    auto const& b = region.getBlock(p);
                    if (infoOf(b.getBlockType()).kind != Kind::WallLantern) continue;
                    auto  face  = stringState(b, kFace);
                    bool  alongX = face == "north" || face == "south";
                    float along  = alongX ? d.x : d.z;
                    if (face == "south" || face == "west") along = -along;
                    float impulse = std::clamp(speed * 15.f, 1.f, 4.f) * (along >= 0 ? 1.f : -1.f);
                    push(dim, p, impulse);
                }
    }
}

void Lighting::tickParticles() {
    auto level = ll::service::getLevel();
    if (!level) return;
    static std::mt19937 rng{std::random_device{}()};
    int                 samples = std::clamp(cfg().particleSamplesPerTick, 0, 4000);
    if (samples == 0) return;
    level->forEachPlayer([&](Player& player) {
        auto&    region = player.getDimensionBlockSource();
        int      dim    = static_cast<int>(player.getDimensionId());
        BlockPos at     = game::blockPosOf(player.getPosition());
        std::uniform_int_distribution<int> off(-16, 16);
        for (int i = 0; i < samples; ++i) {
            BlockPos p{at.x + off(rng), at.y + off(rng), at.z + off(rng)};
            if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight()) continue;
            if (!region.hasChunksAt(p, 0, false)) continue;
            auto const& b    = region.getBlock(p);
            auto        info = infoOf(b.getBlockType());
            if (!isLightable(info.kind) || !boolState(b, kLit)) continue;
            for (auto const& v : flamePositions(b, info, p)) {
                bool candle = info.kind == Kind::CandleHolder;
                if (candle && (rng() % 3) != 0) continue; // candles: the flame is in the model, smoke only now and then
                if (auto pkt = particlePacket(dim, Vec3{v.x, v.y + 0.12f, v.z}, "minecraft:basic_smoke_particle")) {
                    pkt->sendTo(player);
                    ++mParticles;
                }
                if (!candle) {
                    if (auto pkt = particlePacket(dim, v, flameParticle(info.flame))) {
                        pkt->sendTo(player);
                        ++mParticles;
                    }
                }
            }
        }
        return true;
    });
}

// ------------------------------------------------------------------------------------------------
// install

void Lighting::install() {
    if (mInstalled) return;
    mInstalled = true;
    static bool lifecycle = false;
    if (!lifecycle) {
        lifecycle = true;
        game::db::onWorldReady([] { Lighting::get().reset(); });
        game::db::onWorldClosed([] { Lighting::get().reset(); });
    }
    auto& bus = ll::event::EventBus::getInstance();

    mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent&) {
        tick();
    }));

    mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
        [this](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player = ev.self();
            if (player.getLevel().isClientSide()) return;
            auto&       region = player.getDimensionBlockSource();
            BlockPos    pos    = ev.blockPos();
            int         face   = static_cast<int>(ev.face());
            auto const& block  = ev.block() ? *ev.block() : region.getBlock(pos);
            auto        info   = infoOf(block.getBlockType());
            auto&       item   = ev.item();
            std::string itemId = item.isNull() ? std::string{} : item.getTypeName();
            int         dim    = static_cast<int>(player.getDimensionId());
            auto        uid    = player.getOrCreateUniqueID().rawID;
            int         slot   = player.getSelectedItemSlot();

            // The client often sends the same click twice: one action per click and block.
            auto debounce = [&]() {
                auto& last = mLastUse[uid];
                bool  dup  = std::get<0>(last) + 4 > mTick && std::get<1>(last) == pos.x && std::get<2>(last) == pos.y
                         && std::get<3>(last) == pos.z;
                last = {mTick, pos.x, pos.y, pos.z};
                return dup;
            };

            if (isLightable(info.kind)) {
                bool lit = boolState(block, kLit);
                if (info.kind == Kind::CandleHolder && !itemId.empty() && itemId == block.getTypeName()) {
                    if (intState(block, kCandles, 1) >= 4) return; // full: place a new holder as usual
                    ev.cancel();
                    if (debounce()) return;
                    defer([dim, pos, uid, slot, itemId] {
                        auto* r = regionOf(dim);
                        if (r && addCandle(*r, pos)) useItem(uid, slot, itemId, false);
                    });
                    return;
                }
                bool flint = itemId == "minecraft:flint_and_steel";
                bool charge = itemId == "minecraft:fire_charge";
                if ((flint || charge) && !lit) {
                    ev.cancel();
                    if (debounce() || waterlogged(region, pos)) return;
                    defer([dim, pos, uid, slot, itemId, flint] {
                        auto* r = regionOf(dim);
                        if (!r || !setLit(*r, pos, true)) return;
                        playSound(*r, pos, flint ? "fire.ignite" : "mob.ghast.fireball", flint ? 1.f : 0.5f, 1.f);
                        useItem(uid, slot, itemId, flint);
                    });
                    return;
                }
                if (itemId.empty()) {
                    if (info.kind == Kind::SconceLever) {
                        ev.cancel();
                        if (debounce()) return;
                        defer([dim, pos] {
                            if (auto* r = regionOf(dim)) toggleLever(*r, pos);
                        });
                    } else if (lit) {
                        ev.cancel();
                        if (debounce()) return;
                        defer([dim, pos] {
                            if (auto* r = regionOf(dim)) setLit(*r, pos, false);
                        });
                    }
                }
                return;
            }

            // Lanterns on the side of a block hang on the wall.
            int lantern = lanternIndex(itemId);
            if (lantern < 0 || face < 2 || face > 5 || !cfg().wallLanterns) return;
            auto const& bl = cfg().wallLanternBlacklist;
            if (std::find(bl.begin(), bl.end(), itemId) != bl.end()) return;
            if (block.getBlockType().isInteractiveBlock() && !player.isSneaking()) return;
            if (!canHangOn(region, pos, face) || !game::replaceable(region, offset(pos, face))) return;
            ev.cancel();
            if (debounce()) return;
            defer([dim, pos, face, lantern, uid, slot, itemId] {
                auto* r = regionOf(dim);
                if (r && placeWallLantern(*r, pos, face, lantern)) useItem(uid, slot, itemId, false);
            });
        },
        ll::event::EventPriority::High
    ));

    // Placed in water: unlit.
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([this](ll::event::PlayerPlacedBlockEvent& ev) {
        auto& player = ev.self();
        if (player.getLevel().isClientSide()) return;
        if (!isLightable(infoOf(ev.placedBlock().getBlockType()).kind)) return;
        int      dim = static_cast<int>(player.getDimensionId());
        BlockPos pos = ev.pos();
        defer([dim, pos] {
            auto* r = regionOf(dim);
            if (r && waterlogged(*r, pos)) setLit(*r, pos, false, false);
        });
    }));

    // Water flowing in (liquid layer) puts the flame out.
    mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([this](ll::event::BlockChangedEvent& ev) {
        auto& region = ev.blockSource();
        bool  check  = false;
        if (ev.layer() == 1) {
            auto const& n = ev.newBlock().getTypeName();
            if (n == "minecraft:water" || n == "minecraft:flowing_water") {
                auto const& main = region.getBlock(ev.pos());
                check = isLightable(infoOf(main.getBlockType()).kind) && boolState(main, kLit);
            }
        } else if (ev.layer() == 0) {
            check = isLightable(infoOf(ev.newBlock().getBlockType()).kind) && boolState(ev.newBlock(), kLit);
        }
        if (!check || region.getLevel().isClientSide()) return;
        int      dim = static_cast<int>(region.getDimensionId());
        BlockPos pos = ev.pos();
        defer([dim, pos] {
            auto* r = regionOf(dim);
            if (r && waterlogged(*r, pos)) setLit(*r, pos, false);
        });
    }));

    LightingProjectileHitHook::hook();
}

void Lighting::uninstall() {
    if (!mInstalled) return;
    mInstalled = false;
    LightingProjectileHitHook::unhook();
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : mListeners) bus.removeListener(l);
    mListeners.clear();
    reset();
}

} // namespace cm::feature::lighting

CM_REGISTER_MODULE(cm::feature::lighting::Lighting);
