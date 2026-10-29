// Cage: catch small mobs into the item, release them, place them in a cage block (shown by a
// frozen copy of the mob).

#include "feature/trap/Trap.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/utils/Base64Utils.h"

#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/entity/components_json_legacy/TameableComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/safety/RedactableString.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::trap {

/// `/supplementaries cage <entity>` parameters (external linkage for the command reflection).
struct TrapCageCommandParams {
    std::string entity;
};

namespace cage {
namespace {

struct Record {
    CompoundTag mob;
    std::string name;
    int64_t     display = 0;
    int         missing = 0; ///< maintenance checks the display was not found (not saved)
};

std::map<core::GlobalPos, Record> gCages;
std::set<std::string>             gCommandMobs;
bool                              gLoaded = false;

void ensureLoaded() {
    if (gLoaded || !game::db::available()) return;
    gLoaded = true;
    std::vector<std::string> keys;
    game::db::forEachKey("cm:trap:cage:", [&keys](std::string_view suffix) { keys.emplace_back(suffix); });
    for (auto const& suffix : keys) {
        auto pos = parseKeySuffix(suffix);
        auto j   = game::db::load("cm:trap:cage:" + suffix);
        if (!pos || !j) continue;
        auto tag = CompoundTag::fromBinaryNbt(ll::base64_utils::decode(j->value("mob", std::string{})));
        if (!tag) continue;
        gCages[*pos] = Record{std::move(*tag), j->value("name", std::string{}), j->value("display", int64_t{0})};
    }
}

void store(core::GlobalPos const& pos, Record const& rec) {
    game::db::save(
        dataKey("cage", pos),
        {{"mob", ll::base64_utils::encode(rec.mob.toBinaryNbt())}, {"name", rec.name}, {"display", rec.display}}
    );
}

std::string pretty(std::string id) {
    if (auto colon = id.find(':'); colon != std::string::npos) id = id.substr(colon + 1);
    bool up = true;
    for (auto& c : id) {
        if (c == '_') {
            c  = ' ';
            up = true;
        } else if (up) {
            c  = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            up = false;
        }
    }
    return id;
}

std::string mobName(Actor const& mob) {
    auto const& tag = mob.getNameTag();
    if (!tag.empty()) return tag;
    return pretty(mob.getTypeName());
}

bool contains(std::vector<std::string> const& list, std::string const& type) {
    return std::find(list.begin(), list.end(), type) != list.end();
}

/// The saved form of a mob for a container (Java `prepareMobTagForContainer`).
std::optional<CompoundTag> saveMob(Actor& mob) {
    CompoundTag tag;
    if (!mob.save(tag) || tag.empty()) return std::nullopt;
    for (auto key : {"UniqueID", "Pos", "Motion", "LeasherID", "LinksTag", "Tags", "IsAngry", "TargetID"}) tag.erase(key);
    if (config().cage.persistentMobs) tag["Persistent"] = static_cast<std::int8_t>(1);
    return tag;
}

Vec3 center(BlockPos const& pos) {
    return Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.0626f, static_cast<float>(pos.z) + 0.5f};
}

Actor* spawnDisplay(BlockSource& region, BlockPos const& pos, Record const& rec) {
    Actor* display = spawnFromTag(region, rec.mob, center(pos), 0.f);
    if (!display) return nullptr;
    display->addTag(kDisplayTag);
    display->setPersistent();
    SynchedActorDataAccess::setActorFlag(display->getEntityContext(), ActorFlags::Noai, true);
    return display;
}

void removeDisplay(int64_t id) {
    if (auto* display = actorByUid(id)) display->despawn();
}

ItemStack fullCage(CompoundTag const& mob, std::string const& name) {
    ItemStack item = makeItem(kCageId);
    auto&     tag  = userDataMut(item);
    tag["CmMob"]     = mob;
    tag["CmMobName"] = name;
    item.setCustomLore({
        "§7" + name,
        "§7§o" + tr("message.supplementaries.cage.tooltip", "[{}] to place", tr("cm.trap.sneak", "Sneak")),
    });
    return item;
}

} // namespace

void onWorldReady() {
    gCages.clear();
    gCommandMobs.clear();
    gLoaded = false;
    ensureLoaded();
}

void onWorldClosed() {
    gCages.clear();
    gCommandMobs.clear();
    gLoaded = false;
}

bool isDisplay(Actor const& actor) { return actor.hasTag(kDisplayTag); }

bool isFull(ItemStack const& item) {
    auto const* tag = userData(item);
    return itemId(item) == kCageId && tag && tag->contains("CmMob");
}

std::string catchFailure(Actor& mob, Player& player) {
    auto const& cfg  = config().cage;
    std::string type = mob.getTypeName();
    if (!mob.isAlive() || mob.isPlayer() || isDisplay(mob)) return "-";
    if (cfg.allowAllMobs || gCommandMobs.contains(type)) return {};
    std::string const fail = trFor(player, "message.supplementaries.cage.fail", "You can't catch this animal");
    bool living = mob.hasCategory(ActorCategory::Mob);
    if (!living) return "-";
    if (type == "minecraft:ender_dragon" || type == "minecraft:wither" || type == "minecraft:warden") return fail;
    bool baby  = mob.isBaby();
    bool can   = false;
    if (cfg.autoDetect) {
        AABB const& box = mob.getAABB();
        float       w   = box.max.x - box.min.x - 0.125f;
        float       h   = box.max.y - box.min.y - 0.125f;
        can             = w < 0.875f && h < 1.f;
    }
    can = can || (cfg.allowAllBabies && baby) || contains(cfg.catchable, type) || (baby && contains(cfg.babyCatchable, type));
    if (!can) return fail;
    if (cfg.requireTaming) {
        bool tameable = mob.getEntityContext().hasComponent<TameableComponent>() || mob.isTame();
        if (tameable && (!mob.isTame() || mob.getOwnerId() != player.getOrCreateUniqueID())) {
            return trFor(player, "message.supplementaries.cage.fail_tamed", "You must tame this animal first");
        }
    }
    int pct = cfg.healthThreshold;
    if (pct < 100 && static_cast<float>(mob.getHealth()) > static_cast<float>(mob.getMaxHealth()) * (static_cast<float>(pct) / 100.f)) {
        return trFor(player, "message.supplementaries.cage.fail_health", "You can only catch animals with less than {} health", std::to_string(pct) + "%");
    }
    return {};
}

bool tryCatch(Player& player, Actor& mob) {
    if (!config().cage.enabled) return false;
    ItemStack const& held = player.getSelectedItem();
    if (itemId(held) != kCageId || isFull(held)) return false;
    auto failure = catchFailure(mob, player);
    if (!failure.empty()) {
        if (failure != "-") actionBar(player, failure);
        return false;
    }
    auto tag = saveMob(mob);
    if (!tag) return false;
    std::string name = mobName(mob);
    auto&       region = mob.getDimensionBlockSource();
    Vec3        at     = mob.getPosition();
    swapHeld(player, fullCage(*tag, name));
    mob.despawn();
    playSound(region, at, "block.chain.fall", 1.f, 0.7f);
    log().info("trap: {} caught {} in a cage", player.getRealName(), mob.getTypeName());
    return true;
}

bool release(Player& player, Vec3 const& pos) {
    if (!config().cage.enabled) return false;
    ItemStack held = player.getSelectedItem();
    if (!isFull(held)) return false;
    auto const& data = *userData(held);
    CompoundTag mob  = data["CmMob"].get<CompoundTag>();
    auto&       region = player.getDimensionBlockSource();
    Actor*      actor  = spawnFromTag(region, mob, pos, player.getRotation().y);
    if (!actor) {
        log().warn("trap: failed to release a caged mob");
        return false;
    }
    if (config().cage.persistentMobs) actor->setPersistent();
    playSound(region, pos, "mob.chicken.plop", 1.f, 0.05f);
    if (!isCreative(player)) swapHeld(player, makeItem(kCageId));
    log().info("trap: {} released {} from a cage", player.getRealName(), actor->getTypeName());
    return true;
}

Actor* spawnFromTag(BlockSource& region, CompoundTag tag, Vec3 const& pos, float yaw) {
    tag.erase("UniqueID");
    tag["Pos"]      = ListTag{pos.x, pos.y, pos.z};
    tag["Motion"]   = ListTag{0.f, 0.f, 0.f};
    tag["Rotation"] = ListTag{yaw, 0.f};
    auto&                 level = region.getLevel();
    DefaultDataLoadHelper helper;
    auto                  entity = level.getActorFactory().loadActor(&tag, helper, *region.getDimension().mHeightRange, nullptr);
    if (!entity) return nullptr;
    return level.addEntity(region, std::move(entity));
}

void onPlaced(BlockSource& region, BlockPos const& pos, CompoundTag const& data) {
    auto const& block = region.getBlock(pos);
    if (!isBlock(block, kCageId) || !data.contains("CmMob")) return;
    ensureLoaded();
    auto   gp = game::globalPos(region, pos);
    Record rec{data["CmMob"].get<CompoundTag>(), data.contains("CmMobName") ? std::string(data["CmMobName"]) : std::string{}, 0};
    setState(region, pos, kFullState, 1);
    if (auto* display = spawnDisplay(region, pos, rec)) rec.display = uid(*display);
    store(gp, rec);
    gCages[gp] = std::move(rec);
    log().info("trap: cage with {} placed at {}", gCages[gp].name, gp.key());
}

void onRemoved(BlockSource& region, BlockPos const& pos, bool drop) {
    ensureLoaded();
    auto gp = game::globalPos(region, pos);
    auto it = gCages.find(gp);
    if (it == gCages.end()) return;
    Record rec = std::move(it->second);
    gCages.erase(it);
    game::db::erase(dataKey("cage", gp));
    removeDisplay(rec.display);
    if (drop) spawnItem(region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, fullCage(rec.mob, rec.name));
    log().info("trap: cage with {} removed at {}", rec.name, gp.key());
}

CompoundTag const* contents(core::GlobalPos const& pos) {
    ensureLoaded();
    auto it = gCages.find(pos);
    return it == gCages.end() ? nullptr : &it->second.mob;
}

Actor* displayAt(core::GlobalPos const& pos) {
    ensureLoaded();
    auto it = gCages.find(pos);
    return it == gCages.end() ? nullptr : actorByUid(it->second.display);
}

void addCommandMob(std::string const& type) { gCommandMobs.insert(type); }

void maintain(Level& level) {
    if (!game::db::available()) return;
    ensureLoaded();
    // displays of placed cages: present and in place
    for (auto it = gCages.begin(); it != gCages.end();) {
        auto const& gp  = it->first;
        auto&       rec = it->second;
        auto        dim = level.getDimension(gp.dim).lock();
        if (!dim) {
            ++it;
            continue;
        }
        auto&    region = dim->getBlockSourceFromMainChunkSource();
        BlockPos pos    = game::blockPos(gp);
        if (!region.hasChunksAt(pos, 0, false)) {
            ++it;
            continue;
        }
        if (!isBlock(region.getBlock(pos), kCageId)) { // removed without an event: keep the mob
            removeDisplay(rec.display);
            spawnItem(region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, fullCage(rec.mob, rec.name));
            game::db::erase(dataKey("cage", gp));
            it = gCages.erase(it);
            continue;
        }
        Actor* display = actorByUid(rec.display);
        if (!display || !display->isAlive()) {
            // entities load after their chunk: give a saved display a few seconds to appear
            if (++rec.missing < 3 && rec.display != 0) {
                ++it;
                continue;
            }
            rec.missing = 0;
            if (auto* fresh = spawnDisplay(region, pos, rec)) {
                rec.display = uid(*fresh);
                store(gp, rec);
            }
        } else {
            rec.missing = 0;
            if ((display->getPosition() - center(pos)).length() > 0.05) display->teleport(center(pos), display->getDimensionId());
        }
        ++it;
    }
    // orphaned displays (their cage is gone)
    std::set<int64_t> known;
    for (auto const& [gp, rec] : gCages) known.insert(rec.display);
    for (Actor* actor : level.getRuntimeActorList()) {
        if (actor && isDisplay(*actor) && !known.contains(uid(*actor))) actor->despawn();
    }
}

} // namespace cage
} // namespace cm::feature::trap

CM_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using cm::feature::trap::TrapCageCommandParams;
    cmd.overload<TrapCageCommandParams>().text("cage").required("entity").execute(
        [](CommandOrigin const& origin, CommandOutput& output, TrapCageCommandParams const& params) {
            if (!cm::commands::isAdmin(origin)) return output.error(cm::tr("cm.trap.need_op", "You need operator permission."));
            std::string type = params.entity;
            if (type.find(':') == std::string::npos) type = "minecraft:" + type;
            cm::feature::trap::cage::addCommandMob(type);
            output.success(cm::tr("commands.supplementaries.cage", "Added {} to temporary Cage and Jars catchable list", type));
        }
    );
});
