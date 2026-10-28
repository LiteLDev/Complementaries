// Clock and compass information on use (docs/spec/tweak.md §2.3) and bottling experience (§2.4).

#include <cmath>
#include <cstdio>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::tweak {

namespace {
std::string twoDigits(int v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", v);
    return buf;
}

/// The level of a player, recovered from the points the next level needs (Bedrock's formula).
int levelOf(Player& player) {
    int need = player.getXpNeededForNextLevel();
    if (need <= 7 + 2 * 14) return std::max(0, (need - 7) / 2);
    if (need <= 37 + 5 * 14) return 15 + (need - 37) / 5;
    return 30 + (need - 112) / 9;
}
} // namespace

std::string clockText(int dayTicks) {
    int t       = ((dayTicks % 24000) + 24000) % 24000;
    int hours   = (t / 1000 + 6) % 24;
    int minutes = (t % 1000) * 60 / 1000;
    return tr("message.supplementaries.clock", "{0}:{1}", twoDigits(hours), twoDigits(minutes));
}

bool infoUse(Player& player, std::string const& itemId) {
    auto const& cfg = config();
    if (itemId == "minecraft:clock" && cfg.clockRightClick) {
        int t = ((player.getLevel().getTime() % 24000) + 24000) % 24000;
        showTip(
            player,
            trFor(player, "message.supplementaries.clock", "{0}:{1}", twoDigits((t / 1000 + 6) % 24), twoDigits((t % 1000) * 60 / 1000))
        );
        return true;
    }
    if (itemId == "minecraft:compass" && cfg.compassRightClick) {
        int dim = static_cast<int>(player.getDimensionBlockSource().getDimensionId());
        if (dim != 0) {
            showTip(player, trFor(player, "message.supplementaries.compass.spinning", "The compass is spinning wildly!"));
            return true;
        }
        auto const& p = player.getPosition();
        showTip(
            player,
            trFor(
                player,
                "message.supplementaries.compass",
                "X: {0} Y: {1} Z: {2}",
                static_cast<int>(std::floor(p.x)),
                static_cast<int>(std::floor(p.y - 1.62f)),
                static_cast<int>(std::floor(p.z))
            )
        );
        return true;
    }
    return false;
}

long long totalXp(Player& player) {
    int lvl = levelOf(player);
    return Player::getXpNeededForLevelRange(0, lvl) + player.getXpEarnedAtCurrentLevel();
}

bool bottleXpUseOn(UseOn const& use) {
    if (!config().bottleXp || use.itemId != "minecraft:glass_bottle" || blockName(use.block) != "minecraft:enchanting_table") {
        return false;
    }
    auto playerId = use.player.getOrCreateUniqueID();
    auto pos      = use.pos;
    defer([=] {
        auto* player = findPlayer(playerId);
        if (!player || itemId(player->getSelectedItem()) != "minecraft:glass_bottle") return;
        int const cost = std::max(0, config().bottleXpCost);
        if (!player->isCreative()) {
            long long total = totalXp(*player);
            if (total < cost) {
                showTip(*player, trFor(*player, "message.supplementaries.bottle_xp.not_enough", "Not enough experience"));
                return;
            }
            player->resetPlayerLevel();
            if (total - cost > 0) player->addExperience(static_cast<int>(total - cost));
        }
        consumeSelected(*player);
        giveOrDrop(*player, makeItem("minecraft:experience_bottle"));
        playSound(player->getDimensionBlockSource(), Sound::Fizz, Vec3{pos.x + 0.5f, pos.y + 1.0f, pos.z + 0.5f});
        log().info("tweak: {} bottled experience", player->getRealName());
    });
    return true;
}

} // namespace cm::feature::tweak
