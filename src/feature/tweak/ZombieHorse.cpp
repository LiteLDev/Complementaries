// Skeleton horses fed with rotten flesh turn into zombie horses (docs/spec/tweak.md §3.3).

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::tweak {

namespace {
std::string dbKey(ActorUniqueID id) { return "cm:tweak:horse:" + std::to_string(id.rawID); }

void convert(ActorUniqueID horseId, ActorUniqueID playerId) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Actor* horse = level->fetchEntity(horseId, false);
    if (!horse || horse->getTypeName() != "minecraft:skeleton_horse") return;
    auto&       region = horse->getDimensionBlockSource();
    Vec3        pos    = horse->getPosition();
    std::string name   = horse->getNameTag();
    bool        tame   = horse->isTame() || horse->getStatusFlag(ActorFlags::Tamed);
    bool        saddle = horse->getStatusFlag(ActorFlags::Saddled);

    ActorDefinitionIdentifier id("minecraft", "zombie_horse", tame ? "minecraft:on_tame" : "");
    Mob* zombie = level->getSpawner().spawnMob(region, id, nullptr, pos, false, false, false);
    if (!zombie) return;
    if (!name.empty()) zombie->setNameTag(name);
    if (tame) {
        if (auto* player = findPlayer(playerId)) zombie->setOwner(player->getOrCreateUniqueID());
    }
    if (saddle) region.getLevel().getSpawner().spawnItem(region, makeItem("minecraft:saddle"), nullptr, pos, 10);
    horse->remove();
    game::db::erase(dbKey(horseId));
    playSound(region, Sound::ConvertToZombified, pos);
    log().info("tweak: a skeleton horse turned into a zombie horse");
}
} // namespace

bool zombieHorseInteract(Player& player, Actor& target) {
    auto const& cfg = config();
    if (!cfg.zombieHorseConversion) return false;
    if (itemId(player.getSelectedItem()) != "minecraft:rotten_flesh") return false;
    auto const& type = target.getTypeName();
    auto&       region = player.getDimensionBlockSource();

    if (type == "minecraft:zombie_horse") {
        if (target.getHealth() >= target.getMaxHealth()) return false;
        target.heal(2);
        consumeSelected(player);
        playSound(region, Sound::Eat, target.getPosition());
        return true;
    }
    if (type != "minecraft:skeleton_horse") return false;

    auto        horseId = target.getOrCreateUniqueID();
    auto const  key     = dbKey(horseId);
    int         fed     = 0;
    if (auto data = game::db::load(key)) fed = data->value("fed", 0);
    ++fed;
    consumeSelected(player);
    playSound(region, Sound::Eat, target.getPosition());
    int const need = std::max(1, cfg.zombieHorseFleshCount);
    if (fed >= need) {
        game::db::erase(key);
        auto playerId = player.getOrCreateUniqueID();
        defer([horseId, playerId] { convert(horseId, playerId); });
    } else {
        game::db::save(key, {{"fed", fed}});
        showTip(player, trFor(player, "message.supplementaries.zombie_horse.progress", "This skeleton horse craves more flesh ({0}/{1})", fed, need));
    }
    return true;
}

} // namespace cm::feature::tweak
