// U1 jar: capturing mobs in jars, releasing them, and the helper entity that draws them.

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/jar/Jar.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::jar {

namespace {

// Jar inner size (Supplementaries JarItem: 0.625 x 0.875 blocks).
constexpr float kJarWidth  = 0.625f;
constexpr float kJarHeight = 0.875f;

struct FishInfo {
    std::string_view type;
    std::string_view bucket;
    int              display;
};
constexpr FishInfo kFish[] = {
    {"minecraft:cod",           "minecraft:cod_bucket",           20},
    {"minecraft:salmon",        "minecraft:salmon_bucket",        21},
    {"minecraft:pufferfish",    "minecraft:pufferfish_bucket",    22},
    {"minecraft:tropicalfish",  "minecraft:tropical_fish_bucket", 23},
    {"minecraft:tropical_fish", "minecraft:tropical_fish_bucket", 23},
    {"minecraft:axolotl",       "minecraft:axolotl_bucket",       24},
    {"minecraft:tadpole",       "minecraft:tadpole_bucket",       25},
};

FishInfo const* fishByType(std::string_view type) {
    for (auto const& f : kFish) {
        if (f.type == type) return &f;
    }
    return nullptr;
}

FishInfo const* fishByBucket(std::string_view bucket) {
    for (auto const& f : kFish) {
        if (f.bucket == bucket) return &f;
    }
    return nullptr;
}

/// Mobs that jars catch (Supplementaries `jar_catchable`, vanilla entries).
bool jarCatchable(std::string_view type) {
    return type == "minecraft:slime" || type == "minecraft:bee" || type == "minecraft:magma_cube"
        || type == "minecraft:allay" || type == "minecraft:frog";
}

bool isBoat(std::string_view type) { return type == "minecraft:boat" || type == "minecraft:chest_boat"; }

std::string prettyName(std::string_view type) {
    auto        pos = type.find(':');
    std::string name(pos == std::string_view::npos ? type : type.substr(pos + 1));
    bool        up = true;
    for (auto& ch : name) {
        if (ch == '_') {
            ch = ' ';
            up = true;
        } else if (up) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            up = false;
        }
    }
    return name;
}

int tagInt(CompoundTag const& tag, std::string_view key, int fallback) {
    if (!tag.contains(key)) return fallback;
    auto const& v = tag[key];
    if (!v.is_number_integer()) return fallback;
    return static_cast<int>(static_cast<int64>(v));
}

/// y offset of the drawn mob above the jar bottom, and its light.
struct DisplayInfo {
    bool floating = false;
    int  light    = 0;
};
DisplayInfo displayInfo(int kind) {
    switch (kind) {
    case 1: // bee
    case 4: // allay
    case 8: // vex
        return {true, 0};
    case 10: // endermite
        return {false, 5};
    default:
        return {kind >= 20, 0}; // fish float in water
    }
}

std::map<core::GlobalPos, ActorUniqueID> gDisplays; ///< jar -> its helper entity (this session)
std::set<core::GlobalPos>                gWanted;   ///< jars whose mob should be drawn

Vec3 displayPos(core::GlobalPos const& pos, int kind) {
    float y = static_cast<float>(pos.y) + (displayInfo(kind).floating ? 0.25f : 0.0625f);
    return Vec3{static_cast<float>(pos.x) + 0.5f, y, static_cast<float>(pos.z) + 0.5f};
}

Actor* fetch(ActorUniqueID id) {
    auto level = ll::service::getLevel();
    return level ? level->fetchEntity(id, false) : nullptr;
}

} // namespace

int displayKindFor(std::string const& type, int variant, bool baby) {
    if (type == "minecraft:bee") return 1;
    if (type == "minecraft:slime") return 2;
    if (type == "minecraft:magma_cube") return 3;
    if (type == "minecraft:allay") return 4;
    if (type == "minecraft:frog") return 5 + std::clamp(variant, 0, 2);
    if (type == "minecraft:vex") return 8;
    if (type == "minecraft:silverfish") return 9;
    if (type == "minecraft:endermite") return 10;
    if (type == "minecraft:rabbit") return 11 + std::clamp(variant, 0, 5);
    if (type == "minecraft:axolotl") {
        static constexpr int kAxolotl[5] = {24, 26, 27, 28, 29}; // lucy, cyan, gold, wild, blue
        return kAxolotl[std::clamp(variant, 0, 4)];
    }
    if (auto const* f = fishByType(type)) return f->display;
    (void)baby;
    return 0;
}

std::string prettyMobName(std::string_view type) { return prettyName(type); }

CaptureFail canCatch(Actor& mob, Player const* player) {
    if (!mob.isAlive() || mob.isPlayer()) return CaptureFail::NotCatchable;
    auto const& conf = cfg();
    auto        type = mob.getTypeName();
    bool listed      = conf.allowAllMobs
                    || std::find(conf.extraCatchable.begin(), conf.extraCatchable.end(), type) != conf.extraCatchable.end();
    if (!listed) {
        bool ok =
            jarCatchable(type) || (type == "minecraft:rabbit" && mob.isBaby()) || fishByType(type) || isBoat(type);
        if (!ok && conf.autoDetect && !mob.hasFamily(HashedString("monster"))) {
            auto const& box    = mob.getAABB();
            float       margin = 0.125f;
            float       w      = (box.max.x - box.min.x) - margin;
            float       h      = (box.max.y - box.min.y) - margin;
            ok                 = w < kJarWidth && h < kJarHeight;
        }
        if (!ok) return CaptureFail::NotCatchable;
    }
    if (conf.requireTaming && mob.isTame() && player && mob.getOwnerId() != player->getOrCreateUniqueID()) {
        return CaptureFail::Tamed;
    }
    if (conf.healthThreshold < 100 && !isBoat(type)) {
        int hp = mob.getHealth(), max = mob.getMaxHealth();
        if (max > 0
            && static_cast<float>(hp) > static_cast<float>(max) * (static_cast<float>(conf.healthThreshold) / 100.f)) {
            return CaptureFail::Health;
        }
    }
    return CaptureFail::None;
}

std::optional<StoredMob> captureMob(Actor& mob) {
    StoredMob m;
    m.type = mob.getTypeName();
    m.name = mob.getNameTag().empty() ? prettyName(m.type) : mob.getNameTag();
    m.baby = mob.isBaby();
    CompoundTag tag;
    if (!mob.save(tag) || tag.empty()) return std::nullopt;
    tag.erase("Passengers");
    tag.erase("LinksTag");
    m.nbt = toHex(tag.toBinaryNbt());
    if (auto const* f = fishByType(m.type)) m.bucket = std::string(f->bucket);
    m.display = displayKindFor(m.type, tagInt(tag, "Variant", 0), m.baby);
    m.light   = displayInfo(m.display).light;
    if (m.type == "minecraft:glow_squid") m.light = 3;
    return m;
}

std::optional<StoredMob> mobFromBucket(ItemStack const& bucket) {
    if (bucket.isNull()) return std::nullopt;
    auto const* f = fishByBucket(bucket.getTypeName());
    if (!f) return std::nullopt;
    StoredMob m;
    m.type    = std::string(f->type == "minecraft:tropical_fish" ? "minecraft:tropicalfish" : f->type);
    m.name    = prettyName(m.type);
    m.bucket  = std::string(f->bucket);
    m.display = f->display;
    return m;
}

Actor* releaseMob(BlockSource& region, StoredMob const& m, Vec3 const& pos, float yaw) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Actor* actor = nullptr;
    if (!m.nbt.empty()) {
        if (auto tag = CompoundTag::fromBinaryNbt(fromHex(m.nbt))) {
            (*tag)["Pos"]      = ListTag{pos.x, pos.y, pos.z};
            (*tag)["Motion"]   = ListTag{0.f, 0.f, 0.f};
            (*tag)["Rotation"] = ListTag{yaw, 0.f};
            (*tag)["UniqueID"] = static_cast<int64>(level->getNewUniqueID().rawID);
            if (auto a = region.spawnActor(*tag)) actor = a.as_ptr();
        }
    }
    if (!actor) {
        auto type = m.type == "minecraft:tropical_fish" ? std::string("minecraft:tropicalfish") : m.type;
        actor =
            level->getSpawner().spawnMob(region, ActorDefinitionIdentifier(type), nullptr, pos, false, false, false);
    }
    if (actor && cfg().persistentMobs) actor->setPersistent();
    return actor;
}

bool useJarItemOnMob(Player& player, Actor& target) {
    auto const& conf = cfg();
    if (!conf.enabled || !conf.jarCapture) return false;
    ItemStack held = player.getSelectedItem();
    if (!isItem(held, kJarId)) return false;
    auto contents = itemContents(held).value_or(Contents{});
    if (contents.mob) return false; // already full
    auto fail = canCatch(target, &player);
    if (fail != CaptureFail::None) {
        switch (fail) {
        case CaptureFail::Tamed:
            player.sendMessage(
                trFor(player, "message.supplementaries.cage.fail_tamed", "You must tame this animal first")
            );
            break;
        case CaptureFail::Health:
            player.sendMessage(trFor(
                player,
                "message.supplementaries.cage.fail_health",
                "You can only catch animals with less than {} health",
                std::to_string(conf.healthThreshold) + "%"
            ));
            break;
        default:
            if (!target.isPlayer()) {
                player.sendMessage(trFor(player, "message.supplementaries.cage.fail", "You can't catch this animal"));
            }
            break;
        }
        return true;
    }
    auto type = target.getTypeName();
    if (isBoat(type)) {
        // "Boat in a Jar" belongs to the tableware unit: only when that item exists
        auto boat = makeItem(kJarBoatId);
        if (boat.isNull()) {
            player.sendMessage(trFor(player, "message.supplementaries.cage.fail", "You can't catch this animal"));
            return true;
        }
        target.remove();
        swapHeld(player, boat);
        playSound(player.getDimensionBlockSource(), player.getPosition(), "random.glass");
        log().info("jar: {} put a boat in a jar", player.getRealName());
        return true;
    }
    auto stored = captureMob(target);
    if (!stored) return false;
    target.remove();
    contents.mob   = std::move(*stored);
    ItemStack full = held;
    full.set(1);
    setItemContents(full, contents, &player);
    swapHeld(player, full);
    playSound(player.getDimensionBlockSource(), player.getPosition(), "random.glass", 1.f, 1.f);
    log().info("jar: {} caught a {}", player.getRealName(), type);
    return true;
}

bool releaseFromItem(Player& player, BlockPos const& clicked, Vec3 const& clickPos) {
    ItemStack held = player.getSelectedItem();
    if (!isItem(held, kJarId)) return false;
    auto contents = itemContents(held);
    if (!contents || !contents->mob) return false;
    auto& region = player.getDimensionBlockSource();
    Vec3  at     = clickPos;
    // the event's click position may be relative to the block
    if (at.x >= 0.f && at.x <= 1.f && at.y >= 0.f && at.y <= 1.f && at.z >= 0.f && at.z <= 1.f) {
        at = Vec3{
            static_cast<float>(clicked.x) + at.x,
            static_cast<float>(clicked.y) + at.y,
            static_cast<float>(clicked.z) + at.z
        };
    }
    float yaw   = player.getRotation().y + 180.f;
    auto* actor = releaseMob(region, *contents->mob, at, yaw);
    if (!actor) return false;
    if (!player.isCreative() && contents->mob->type == "minecraft:bee") actor->setTarget(&player);
    playSound(region, at, "mob.chicken.plop", 1.f, 0.05f);
    log().info("jar: {} released a {}", player.getRealName(), contents->mob->type);
    if (!player.isCreative()) {
        contents->mob.reset();
        ItemStack empty = held;
        empty.set(1);
        setItemContents(empty, *contents, &player);
        swapHeld(player, empty);
    }
    return true;
}

// ---- display entity ------------------------------------------------------------------------------

void removeDisplay(core::GlobalPos const& pos) {
    gWanted.erase(pos);
    auto it = gDisplays.find(pos);
    if (it == gDisplays.end()) return;
    if (auto* a = fetch(it->second)) a->despawn();
    gDisplays.erase(it);
}

void syncDisplay(core::GlobalPos const& pos, Contents const& c) {
    int  kind = c.mob ? c.mob->display : 0;
    auto tag  = "cm_jar_mob_" + std::to_string(kind);
    auto it   = gDisplays.find(pos);
    if (it != gDisplays.end()) {
        auto* a = fetch(it->second);
        if (a && kind != 0 && a->hasTag(tag)) return; // already right
        removeDisplay(pos);
    }
    if (kind == 0) {
        gWanted.erase(pos);
        return;
    }
    gWanted.insert(pos);
    auto* region = regionOf(pos.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level || !region->hasChunksAt(game::blockPos(pos), 0, false)) return;
    ActorDefinitionIdentifier id(std::string(kJarMobId) + "<complementaries:display_" + std::to_string(kind) + ">");
    auto* mob = level->getSpawner().spawnMob(*region, id, nullptr, displayPos(pos, kind), false, false, false);
    if (!mob) {
        log().warn("jar: could not spawn the mob display at {}", pos.key());
        return;
    }
    mob->addTag(tag);
    gDisplays[pos] = mob->getOrCreateUniqueID();
}

void wantDisplay(core::GlobalPos const& pos) { gWanted.insert(pos); }

void clearDisplays() {
    gDisplays.clear();
    gWanted.clear();
}

bool hasDisplay(core::GlobalPos const& pos) {
    auto it = gDisplays.find(pos);
    return it != gDisplays.end() && fetch(it->second) != nullptr;
}

void reconcileDisplays() {
    auto level = ll::service::getLevel();
    if (!level) return;
    // drop helpers that no jar owns (left over from another session or a broken jar)
    std::vector<ActorUniqueID> owned;
    for (auto const& [pos, id] : gDisplays) owned.push_back(id);
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || a->getTypeName() != kJarMobId) continue;
        auto id = a->getOrCreateUniqueID();
        if (std::find(owned.begin(), owned.end(), id) == owned.end()) a->despawn();
    }
    // spawn missing ones of loaded mob jars
    std::vector<core::GlobalPos> todo;
    for (auto const& pos : gWanted) {
        if (!hasDisplay(pos)) todo.push_back(pos);
    }
    for (auto const& pos : todo) {
        auto* region = regionOf(pos.dim);
        if (!region || !region->hasChunksAt(game::blockPos(pos), 0, false)) continue;
        if (!isBlock(region->getBlock(game::blockPos(pos)), kJarId)) {
            gWanted.erase(pos);
            continue;
        }
        gDisplays.erase(pos);
        syncDisplay(pos, jarAt(pos));
    }
}

} // namespace cm::feature::jar
