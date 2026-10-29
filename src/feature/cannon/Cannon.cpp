// U15 `cannon`: the cannon block (contents, aim, firing, redstone, barrel entity, screen and
// manoeuvre mode). See docs/spec/cannon.md.

#include "feature/cannon/Cannon.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerSwingEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/AnimateEntityPacket.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"
#include "mc/world/redstone/circuit/components/CircuitComponentType.h"

#include "feature/cannon/CannonConfig.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("cannon", cm::feature::cannon::CannonConfig);

namespace cm::feature::cannon {

namespace {

using core::GlobalPos;
namespace db = game::db;

constexpr char const* kDbPrefix = "cm:cannon:c:";

CannonConfig const& cfg() { return config::get<CannonConfig>(); }

std::string dbKey(GlobalPos const& p) { return kDbPrefix + p.key(); }

std::optional<GlobalPos> parseKey(std::string_view s) {
    GlobalPos p;
    if (std::sscanf(std::string(s).c_str(), "%d:%d:%d:%d", &p.dim, &p.x, &p.y, &p.z) != 4) return std::nullopt;
    return p;
}

Vec3     center(GlobalPos const& p) { return {p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }
Vec3     toVec(math::V3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }
math::V3 toV3(Vec3 const& v) { return {v.x, v.y, v.z}; }

std::string uuidOf(Player const& p) { return p.getUuid().asString(); }

Player* playerByUuid(std::string const& uuid) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Player* found = nullptr;
    level->forEachPlayer([&](Player& p) {
        if (uuidOf(p) == uuid) {
            found = &p;
            return false;
        }
        return true;
    });
    return found;
}

bool sneaking(Actor const& a) { return a.getStatusFlag(ActorFlags::Sneaking); }

int maxPower() { return std::max(1, cfg().maxPower); }

/// Muzzle speed (blocks/tick) of a shot at power.
double shotSpeed(int power) { return power * 0.75 * static_cast<double>(cfg().firePower); }

/// Work deferred to the next server tick (world edits after a cancelled interaction, ...).
std::vector<std::function<void()>>& deferred() {
    static std::vector<std::function<void()>> v;
    return v;
}
void defer(std::function<void()> fn) { deferred().push_back(std::move(fn)); }

std::string itemLabel(ItemStack const& s) {
    if (s.isNull()) return {};
    auto name = s.getName();
    return name.empty() ? s.getTypeName() : name;
}

CannonData fromJson(nlohmann::json const& j) {
    CannonData d;
    d.fuel       = std::clamp(j.value("fuel", 0), 0, kMaxFuel);
    d.projectile = itemFromSnbt(j.value("projectile", std::string()));
    d.yaw        = j.value("yaw", 0.0f);
    d.pitch      = j.value("pitch", 30.0f);
    d.power      = std::clamp(j.value("power", 1), 1, maxPower());
    d.powered    = j.value("powered", false);
    d.barrel     = j.value("barrel", static_cast<int64_t>(0));
    return d;
}

std::vector<ll::event::ListenerPtr> gListeners;
std::map<std::string, uint64_t>     gLastInteract; ///< player uuid + pos -> tick (double-click guard)

} // namespace

Cannons& Cannons::get() {
    static Cannons instance;
    return instance;
}

// ---- records -------------------------------------------------------------------------------------

bool Cannons::isCannon(GlobalPos const& pos) const {
    auto* region = regionOf(pos.dim);
    return region && game::blockIdAt(*region, game::blockPos(pos)) == kCannon;
}

int Cannons::face(GlobalPos const& pos) const {
    auto* region = regionOf(pos.dim);
    if (!region) return 1;
    auto f = game::intState(region->getBlock(game::blockPos(pos)), "minecraft:block_face");
    return f && *f >= 0 && *f < 6 ? *f : 1;
}

CannonData* Cannons::at(GlobalPos const& pos) {
    if (auto it = mCannons.find(pos); it != mCannons.end()) return &it->second;
    if (!isCannon(pos)) return nullptr;
    auto& d = mCannons[pos];
    d.dirty = true;
    return &d;
}

math::V3 Cannons::direction(GlobalPos const& pos) {
    auto* d = at(pos);
    return d ? math::localToWorld(face(pos), d->yaw, d->pitch) : math::V3{0, 1, 0};
}

int Cannons::insert(GlobalPos const& pos, ItemStack const& stack) {
    auto* d = at(pos);
    if (!d || stack.isNull()) return 0;
    int count = stack.mCount;
    if (stack.getTypeName() == "minecraft:gunpowder") {
        int n = std::min(count, kMaxFuel - d->fuel);
        if (n <= 0) return 0;
        d->fuel += n;
        d->dirty = true;
        return n;
    }
    int max = std::max<int>(1, stack.getMaxStackSize());
    if (d->projectile.isNull()) {
        int n        = std::min(count, max);
        d->projectile = stack;
        d->projectile.set(n);
        d->dirty = true;
        return n;
    }
    if (!d->projectile.sameItemAndAuxAndBlockData(stack)) return 0;
    int n = std::min(count, max - static_cast<int>(d->projectile.mCount));
    if (n <= 0) return 0;
    d->projectile.add(n);
    d->dirty = true;
    return n;
}

void Cannons::setAim(GlobalPos const& pos, float yaw, float pitch, std::optional<int> power) {
    auto* d = at(pos);
    if (!d) return;
    d->yaw   = static_cast<float>(math::wrapYaw(yaw));
    d->pitch = static_cast<float>(math::clampPitch(pitch));
    if (power) d->power = std::clamp(*power, 1, maxPower());
    d->dirty = true;
    syncBarrel(pos, *d);
}

// ---- barrel entity -----------------------------------------------------------------------------

Actor* Cannons::barrel(GlobalPos const& pos) {
    auto* d      = at(pos);
    auto* region = regionOf(pos.dim);
    if (!d || !region || !region->hasChunksAt(game::blockPos(pos), 0, false)) return nullptr;
    Vec3   c = center(pos);
    Actor* keep = nullptr;
    for (auto* a : region->getEntities(AABB{c - Vec3{0.45f, 0.45f, 0.45f}, c + Vec3{0.45f, 0.45f, 0.45f}}, 0.0f)) {
        if (!a || a->getTypeName() != kBarrel) continue;
        if (!keep) keep = a;
        else a->remove(); // duplicates (e.g. a respawn raced the chunk's own entities)
    }
    if (!keep) keep = spawnActor(*region, kBarrel, c, Vec3{0, 0, 0});
    if (keep) {
        d->barrel = keep->getOrCreateUniqueID().rawID;
        syncBarrel(pos, *d, keep);
    }
    return keep;
}

void Cannons::syncBarrel(GlobalPos const& pos, CannonData& data, Actor* actor) {
    if (!actor) {
        auto level = ll::service::getLevel();
        if (!level || !data.barrel) return;
        actor = level->fetchEntity(ActorUniqueID{data.barrel}, false);
        if (!actor) return;
    }
    auto [yaw, pitch] = math::worldAngles(math::localToWorld(face(pos), data.yaw, data.pitch));
    auto props        = actor->getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) return;
    ActorPropertyUtils::tryQueueFloatValue(*props, *actor, HashedString("complementaries:yaw").getHash(), static_cast<float>(yaw));
    ActorPropertyUtils::tryQueueFloatValue(*props, *actor, HashedString("complementaries:pitch").getHash(), static_cast<float>(pitch));
}

void Cannons::removeCannon(GlobalPos const& pos, bool dropContents) {
    auto it = mCannons.find(pos);
    auto* region = regionOf(pos.dim);
    if (region && region->hasChunksAt(game::blockPos(pos), 0, false)) {
        Vec3 c = center(pos);
        for (auto* a : region->getEntities(AABB{c - Vec3{0.45f, 0.45f, 0.45f}, c + Vec3{0.45f, 0.45f, 0.45f}}, 0.0f)) {
            if (a && a->getTypeName() == kBarrel) a->remove();
        }
        if (it != mCannons.end() && dropContents) {
            auto& d = it->second;
            int dropped = 0;
            if (d.fuel > 0 && dropItem(*region, makeItem("minecraft:gunpowder", d.fuel), c)) ++dropped;
            if (!d.projectile.isNull() && dropItem(*region, d.projectile, c)) ++dropped;
            log().info("Cannon removed at {} ({} stack(s) dropped)", pos.key(), dropped);
        }
    }
    for (auto m = mManoeuvres.begin(); m != mManoeuvres.end();) {
        if (m->second.pos == pos) m = mManoeuvres.erase(m);
        else ++m;
    }
    if (it != mCannons.end()) mCannons.erase(it);
    if (db::available()) db::erase(dbKey(pos));
}

void Cannons::giveOrDrop(Player& player, GlobalPos const& pos, ItemStack const& stack) {
    if (stack.isNull()) return;
    ItemStack copy = stack;
    if (!player.addAndRefresh(copy) && !copy.isNull() && copy.mCount > 0) {
        if (auto* region = regionOf(pos.dim)) dropItem(*region, copy, center(pos) + Vec3{0, 0.6f, 0});
    }
}

// ---- firing --------------------------------------------------------------------------------------

bool Cannons::ignite(GlobalPos const& pos, Player* by) {
    auto* d = at(pos);
    if (!d) return false;
    if (d->fuse > 0 || d->cooldown > 0) return false;
    if (d->fuel < d->power || d->projectile.isNull()) {
        if (by) actionBar(*by, trFor(*by, "cm.cannon.not_loaded", "The cannon is not loaded"));
        return false;
    }
    d->fuse    = std::max(1, cfg().fuseTime);
    d->igniter = by ? by->getOrCreateUniqueID().rawID : 0;
    playSound(pos.dim, center(pos), "complementaries.cannon.ignite", 0.8f, 1.0f);
    particle(pos.dim, center(pos) + Vec3{0, 0.4f, 0}, "minecraft:basic_smoke_particle");
    log().info("Cannon ignited at {}", pos.key());
    return true;
}

bool Cannons::fireNow(GlobalPos const& pos) {
    auto* d = at(pos);
    if (!d || d->fuel < d->power || d->projectile.isNull()) return false;
    d->fuse     = 0;
    d->cooldown = 0;
    fire(pos, *d);
    return true;
}

void Cannons::fire(GlobalPos const& pos, CannonData& d) {
    auto* region = regionOf(pos.dim);
    if (!region || d.fuel < d.power || d.projectile.isNull()) return;
    math::V3  dir    = direction(pos);
    math::V3  muzzle = toV3(center(pos)) + dir * 1.1;
    double    speed  = shotSpeed(d.power);
    ItemStack one    = d.projectile;
    one.set(1);
    std::string what = one.getTypeName();
    launch(*region, one, toVec(muzzle), toVec(dir * speed), d.igniter);

    d.fuel -= d.power;
    if (d.projectile.mCount <= 1) d.projectile = ItemStack();
    else d.projectile.remove(1);
    d.cooldown = std::max(0, cfg().cooldown);
    d.igniter  = 0;
    d.dirty    = true;

    playSound(pos.dim, center(pos), "complementaries.cannon.fire", 2.0f, 1.0f);
    for (int i = 1; i <= 3; ++i) particle(pos.dim, toVec(muzzle + dir * (0.3 * i)), "minecraft:basic_smoke_particle");
    particle(pos.dim, toVec(muzzle), "minecraft:large_explosion");
    if (auto* b = barrel(pos)) {
        if (auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::TriggerAnimation)) {
            auto& pkt                  = static_cast<AnimateEntityPacket&>(*packet);
            pkt.mRuntimeIds            = std::vector<ActorRuntimeID>{b->getRuntimeID()};
            pkt.mAnimation             = std::string("animation.complementaries.cannon_barrel.fire");
            pkt.mNextState             = std::string("default");
            pkt.mStopExpression        = std::string("query.any_animation_finished");
            pkt.mStopExpressionVersion = MolangVersion::Latest;
            pkt.mBlendOutTime          = 0.0f;
            pkt.sendTo(*b);
        }
    }
    log().info("Cannon fired {} at {} (power {})", what, pos.key(), d.power);
}

std::vector<math::V3> Cannons::predict(GlobalPos const& pos, bool* blocked) {
    auto* d      = at(pos);
    auto* region = regionOf(pos.dim);
    if (!d || !region) return {};
    math::V3 dir    = direction(pos);
    math::V3 muzzle = toV3(center(pos)) + dir * 1.1;
    math::V3 vel    = dir * shotSpeed(d->power);
    // gravity / drag of what is loaded (Bedrock projectile definitions)
    double      gravity = 0.05, drag = 0.99;
    std::string id      = d->projectile.isNull() ? std::string(kCannonball) : d->projectile.getTypeName();
    if (id == "minecraft:snowball" || id == "minecraft:egg" || id == "minecraft:ender_pearl") gravity = 0.03;
    else if (id == "minecraft:fire_charge" || id == "minecraft:wind_charge") gravity = 0.0;
    auto solidAt = [region](math::V3 p) {
        BlockPos bp{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
        if (bp.y < region->getMinHeight() || bp.y >= region->getMaxHeight()) return false;
        if (!region->hasChunksAt(bp, 0, false)) return true;
        auto const& b = region->getBlock(bp);
        return !b.isAir() && b.mCachedComponentData->mIsSolid;
    };
    auto hit = [&](math::V3 a, math::V3 b) {
        double len   = (b - a).length();
        int    steps = std::max(1, static_cast<int>(std::ceil(len / 0.2)));
        for (int i = 1; i <= steps; ++i) {
            double f = static_cast<double>(i) / steps;
            if (solidAt(a + (b - a) * f)) return f;
        }
        return 2.0;
    };
    return math::trajectory(muzzle, vel, gravity, drag, 200, hit, blocked);
}

bool Cannons::redstonePowered(GlobalPos const& pos) const {
    auto level = ll::service::getLevel();
    if (!level) return false;
    auto dim = level->getDimension(DimensionType(pos.dim)).lock();
    if (!dim) return false;
    std::unique_ptr<CircuitSystem>& system = dim->mCircuitSystem;
    if (!system) return false;
    CircuitSceneGraph& graph = system->mSceneGraph;
    auto&              all   = graph.mAllComponents;
    BlockPos bp = game::blockPos(pos);
    // the block's own consumer component (minecraft:redstone_consumer)
    if (auto it = all.find(bp); it != all.end() && it->second) return it->second->getStrength() > 0;
    // fallback (consumer not registered yet): a power source or a powered block next to the cannon
    static constexpr int kN[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (auto const& n : kN) {
        auto it = all.find(BlockPos{bp.x + n[0], bp.y + n[1], bp.z + n[2]});
        if (it == all.end() || !it->second || it->second->getStrength() <= 0) continue;
        auto group = static_cast<uint64_t>(static_cast<CircuitComponentType>(it->second->mCircuitComponentType))
                   & static_cast<uint64_t>(CircuitComponentType::GroupMask);
        if (group == static_cast<uint64_t>(CircuitComponentType::ProducerComponent)
            || group == static_cast<uint64_t>(CircuitComponentType::PoweredBlockComponent)) {
            return true;
        }
    }
    return false;
}

// ---- players --------------------------------------------------------------------------------------

void Cannons::interact(Player& player, GlobalPos const& pos) {
    if (!at(pos)) return;
    ItemStack held = player.getSelectedItem();
    if (!held.isNull()) {
        std::string what  = held.getTypeName();
        int         taken = insert(pos, held);
        if (taken > 0) {
            held.remove(taken);
            if (held.mCount <= 0) held = ItemStack();
            player.setSelectedItem(held);
            player.sendInventory(false);
            playSound(pos.dim, center(pos), "random.click", 0.6f, 1.2f);
            actionBar(player, trFor(player, "cm.cannon.loaded", "Cannon loaded"));
            log().info("Cannon loaded at {} with {} x{}", pos.key(), what, taken);
            return;
        }
    }
    openScreen(player, pos);
}

void Cannons::openScreen(Player& player, GlobalPos const& pos) {
    auto* d = at(pos);
    if (!d) return;
    auto t = [&](std::string_view key, std::string_view fb, auto const&... args) { return trFor(player, key, fb, args...); };
    std::string projectile = d->projectile.isNull()
                               ? t("cm.cannon.projectile_empty", "Projectile: none")
                               : t("cm.cannon.projectile", "Projectile: {0} × {1}", itemLabel(d->projectile), static_cast<int>(d->projectile.mCount));
    std::vector<std::string> powers;
    for (int i = 1; i <= maxPower(); ++i) powers.push_back(std::to_string(i));
    std::vector<std::string> actions = {
        t("cm.cannon.apply", "Apply"),
        t("cm.cannon.fire", "Fire"),
        t("gui.supplementaries.cannon.maneuver", "Maneuver"),
        t("cm.cannon.take_fuel", "Take out gunpowder"),
        t("cm.cannon.take_projectile", "Take out projectile"),
    };
    ll::form::CustomForm form(t("gui.supplementaries.cannon", "Cannon"));
    form.appendLabel(t("cm.cannon.fuel", "Gunpowder: {0} / {1}", d->fuel, kMaxFuel) + "\n" + projectile);
    form.appendSlider("yaw", t("cm.cannon.yaw", "Yaw"), -180.0, 180.0, 1.0, std::round(d->yaw));
    form.appendSlider("pitch", t("cm.cannon.pitch", "Pitch"), math::kMinPitch, math::kMaxPitch, 1.0, std::round(d->pitch));
    form.appendStepSlider("power", t("cm.cannon.power_level", "Power"), powers, static_cast<size_t>(std::clamp(d->power, 1, maxPower()) - 1));
    form.appendDropdown("action", t("cm.cannon.action", "Action"), actions, 0);
    log().info("Cannon screen opened at {}", pos.key());
    form.sendTo(player, [pos](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) return;
        auto& self = Cannons::get();
        if (!self.at(pos)) return;
        if (static_cast<int>(p.getDimensionId()) != pos.dim || p.getPosition().distanceTo(center(pos)) > 8.0) return;
        auto num = [&](std::string const& key, double fallback) {
            auto it = result->find(key);
            if (it == result->end()) return fallback;
            if (auto const* v = std::get_if<double>(&it->second)) return *v;
            if (auto const* v = std::get_if<uint64>(&it->second)) return static_cast<double>(*v);
            return fallback;
        };
        auto* d = self.at(pos);
        self.setAim(pos, static_cast<float>(num("yaw", d->yaw)), static_cast<float>(num("pitch", d->pitch)), static_cast<int>(num("power", d->power - 1)) + 1);
        switch (static_cast<int>(num("action", 0))) {
        case 1:
            self.ignite(pos, &p);
            break;
        case 2:
            self.startManoeuvre(p, pos);
            break;
        case 3:
            if (d->fuel > 0) {
                self.giveOrDrop(p, pos, makeItem("minecraft:gunpowder", d->fuel));
                d->fuel  = 0;
                d->dirty = true;
            }
            break;
        case 4:
            if (!d->projectile.isNull()) {
                self.giveOrDrop(p, pos, d->projectile);
                d->projectile = ItemStack();
                d->dirty      = true;
            }
            break;
        default:
            break;
        }
    });
}

void Cannons::startManoeuvre(Player& player, GlobalPos const& pos) {
    if (!at(pos)) return;
    mManoeuvres[uuidOf(player)] = Manoeuvre{pos, player.getSelectedItemSlot(), 0};
    log().info("Cannon manoeuvre started at {}", pos.key());
}

void Cannons::stopManoeuvre(Player& player, bool notify) {
    if (mManoeuvres.erase(uuidOf(player)) && notify) {
        actionBar(player, trFor(player, "cm.cannon.maneuver_end", "Stopped maneuvering the cannon"));
    }
}

std::optional<GlobalPos> Cannons::manoeuvring(Player const& player) const {
    if (auto it = mManoeuvres.find(uuidOf(player)); it != mManoeuvres.end()) return it->second.pos;
    return std::nullopt;
}

void Cannons::tickManoeuvres() {
    for (auto it = mManoeuvres.begin(); it != mManoeuvres.end();) {
        auto&   m = it->second;
        Player* p = playerByUuid(it->first);
        auto*   d = p ? at(m.pos) : nullptr;
        bool    stop = !p || !d || static_cast<int>(p->getDimensionId()) != m.pos.dim
                 || p->getPosition().distanceTo(center(m.pos)) > 6.0 || sneaking(*p);
        if (stop) {
            if (p) actionBar(*p, trFor(*p, "cm.cannon.maneuver_end", "Stopped maneuvering the cannon"));
            it = mManoeuvres.erase(it);
            continue;
        }
        // aim along the player's view
        int  f            = face(m.pos);
        auto [yaw, pitch] = math::worldToLocal(f, toV3(p->getViewVector(1.0f)));
        pitch             = math::clampPitch(pitch);
        if (std::abs(math::wrapYaw(yaw - d->yaw)) > 0.25 || std::abs(pitch - d->pitch) > 0.25) {
            setAim(m.pos, static_cast<float>(yaw), static_cast<float>(pitch));
        }
        // scrolling the hotbar changes the power
        int slot = p->getSelectedItemSlot();
        if (m.slot >= 0 && slot != m.slot) {
            int step  = ((slot - m.slot + 9) % 9) <= 4 ? 1 : -1;
            int power = d->power + step;
            if (power > maxPower()) power = 1;
            if (power < 1) power = maxPower();
            d->power = power;
            d->dirty = true;
        }
        m.slot = slot;
        if (m.ticks++ % 4 == 0) {
            bool blocked = false;
            auto pts     = predict(m.pos, &blocked);
            for (size_t i = 1; i < pts.size(); ++i) {
                if (i + 1 == pts.size() && blocked) particleFor(*p, toVec(pts[i]), "complementaries:cannon_target");
                else particleFor(*p, toVec(pts[i]), "complementaries:cannon_trajectory");
            }
            actionBar(*p, trFor(*p, "message.supplementaries.cannon.maneuver", "Press {0} to Dismount, {1} to Fire", trFor(*p, "cm.cannon.key.sneak", "Sneak"), trFor(*p, "cm.cannon.key.attack", "Attack")) + "  |  " + trFor(*p, "cm.cannon.power", "Power: {0} (scroll to change)", d->power));
        }
        ++it;
    }
}

// ---- tick / world ----------------------------------------------------------------------------------

void Cannons::tick() {
    ++mTick;
    if (!mLoaded && db::available()) loadWorld();
    auto work = std::move(deferred());
    deferred().clear();
    for (auto& fn : work) fn();

    std::vector<GlobalPos> gone;
    for (auto& [pos, d] : mCannons) {
        auto* region = regionOf(pos.dim);
        if (!region || !region->hasChunksAt(game::blockPos(pos), 0, false)) continue;
        if (game::blockIdAt(*region, game::blockPos(pos)) != kCannon) {
            gone.push_back(pos);
            continue;
        }
        if (d.cooldown > 0) --d.cooldown;
        if (d.fuse > 0 && --d.fuse == 0) fire(pos, d);
        bool powered = redstonePowered(pos);
        if (powered && !d.powered) ignite(pos, nullptr);
        if (powered != d.powered) {
            d.powered = powered;
            d.dirty   = true;
        }
        if (mTick % 40 == static_cast<uint64_t>(pos.x & 31)) barrel(pos); // keep the barrel entity there
    }
    for (auto const& pos : gone) removeCannon(pos, true);
    tickManoeuvres();
}

bool Cannons::reload(GlobalPos const& pos) {
    auto j = db::available() ? db::load(dbKey(pos)) : std::nullopt;
    if (!j) return false;
    mCannons[pos] = fromJson(*j);
    return true;
}

void Cannons::loadWorld() {
    mCannons.clear();
    mManoeuvres.clear();
    deferred().clear();
    gLastInteract.clear();
    if (!db::available()) return;
    mLoaded = true;
    std::vector<std::string> keys;
    db::forEachKey(kDbPrefix, [&](std::string_view suffix) { keys.emplace_back(suffix); });
    for (auto const& k : keys) {
        auto pos = parseKey(k);
        auto j   = db::load(kDbPrefix + k);
        if (!pos || !j) continue;
        mCannons[*pos] = fromJson(*j);
    }
    if (!mCannons.empty()) log().info("Loaded {} cannon(s)", mCannons.size());
}

void Cannons::saveWorld() {
    if (!db::available()) return;
    for (auto& [pos, d] : mCannons) {
        if (!d.dirty) continue;
        db::save(
            dbKey(pos),
            {{"fuel", d.fuel},
             {"projectile", itemToSnbt(d.projectile)},
             {"yaw", d.yaw},
             {"pitch", d.pitch},
             {"power", d.power},
             {"powered", d.powered},
             {"barrel", d.barrel}}
        );
        d.dirty = false;
    }
}

void Cannons::closeWorld() {
    clearProjectiles();
    mCannons.clear();
    mManoeuvres.clear();
    deferred().clear();
    gLastInteract.clear();
    mLoaded = false;
}

void Cannons::install() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent&) {
        tick();
        tickProjectiles();
        tickRedMerchant(mTick);
    }));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([this](ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide()) return;
        auto block = ev.block();
        if (!block || game::blockId(*block) != kCannon) return;
        if (sneaking(player) && !ev.item().isNull()) return; // vanilla: place against the cannon
        ev.cancel();
        GlobalPos   pos = game::globalPos(player.getDimensionBlockSource(), ev.blockPos());
        std::string uuid = uuidOf(player);
        std::string key  = uuid + "@" + pos.key();
        if (auto it = gLastInteract.find(key); it != gLastInteract.end() && mTick - it->second < 4) return;
        if (gLastInteract.size() > 256) std::erase_if(gLastInteract, [this](auto const& e) { return mTick - e.second > 20; });
        gLastInteract[key] = mTick;
        defer([this, uuid, pos] {
            if (auto* p = playerByUuid(uuid)) interact(*p, pos);
        });
    }));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerSwingEvent>([this](ll::event::PlayerSwingEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide()) return;
        if (auto pos = manoeuvring(player)) {
            std::string uuid = uuidOf(player);
            GlobalPos   at   = *pos;
            defer([this, uuid, at] {
                if (auto* p = playerByUuid(uuid)) ignite(at, p);
            });
        }
    }));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& ev) {
        mManoeuvres.erase(uuidOf(ev.self()));
    }));
    gListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([this](ll::event::BlockChangedEvent& ev) {
        bool was = game::blockId(ev.previousBlock()) == kCannon;
        bool is  = game::blockId(ev.newBlock()) == kCannon;
        if (was == is) return;
        GlobalPos pos = game::globalPos(ev.blockSource(), ev.pos());
        if (is) defer([this, pos] { barrel(pos); });
        else defer([this, pos] {
            if (!isCannon(pos)) removeCannon(pos, true);
        });
    }));
    installProjectiles();
    if (db::available() && !mLoaded) loadWorld();
}

void Cannons::uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
    uninstallProjectiles();
    saveWorld();
    closeWorld();
}

namespace {

struct CannonModule {
    void install() { Cannons::get().install(); }
    void uninstall() { Cannons::get().uninstall(); }
};

struct WorldHooks {
    WorldHooks() {
        db::onWorldReady([] { Cannons::get().loadWorld(); });
        db::onWorldSave([] { Cannons::get().saveWorld(); });
        db::onWorldClosed([] { Cannons::get().closeWorld(); });
    }
};
WorldHooks gWorldHooks;

} // namespace

} // namespace cm::feature::cannon

CM_REGISTER_MODULE(cm::feature::cannon::CannonModule);
