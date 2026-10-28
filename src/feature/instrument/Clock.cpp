// Clock block: hands follow the time of day, comparator output by hour, time message (also for the
// vanilla clock item), random hands in dimensions where clocks don't work.

#include <algorithm>
#include <cstdio>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/instrument/Instrument.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::instrument::clock {

namespace {
bool canReadTime(BlockSource& region) { return isNaturalDimension(region) || config().worksInUnnaturalDimensions; }
} // namespace

int dayTime() {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    int t = level->getTime() % 24000;
    return t < 0 ? t + 24000 : t;
}

std::pair<int, int> hands(int time) {
    int hour   = (time / 1000 + 6) % 12;              // time 0 = 6:00
    int minute = (time % 1000) * 60 / 1000;           // 0..59
    return {hour, minute / 5};
}

void tick(Record& rec, BlockSource& region, BlockPos const& pos, Block const& block) {
    int  time    = dayTime();
    bool natural = canReadTime(region);
    int  hour = state(block, "complementaries:hour"), minute = state(block, "complementaries:minute");
    if (natural) {
        std::tie(hour, minute) = hands(time);
    } else if (gameTick() % 5 == 0) {
        hour   = static_cast<int>(random01() * 12) % 12;
        minute = static_cast<int>(random01() * 12) % 12;
    }
    apply(region, pos, block, withStates(block, {{"complementaries:hour", hour}, {"complementaries:minute", minute}}));
    if (natural && (gameTick() % 20 == 0 || !rec.initDone)) {
        int power = std::clamp(time / 1500, 0, 15);
        if (power != rec.power) {
            rec.power = power;
            rec.dirty = true;
        }
        if (rec.initDone) {
            int realMinute = (time % 1000) * 60 / 1000;
            playSound(
                region,
                pos,
                realMinute % 2 == 0 ? "complementaries.block.clock.tick_1" : "complementaries.block.clock.tick_2",
                0.08f,
                0.95f + static_cast<float>(random01() * 0.1)
            );
        }
        rec.initDone = true;
    }
    int signal = natural ? rec.power : 0;
    if (signal != rec.signal) {
        rec.signal = signal;
        updateComparators(region, pos);
    }
}

std::string timeText(Player const& player, BlockSource& region) {
    bool        readable = canReadTime(region);
    std::string ob, br;
    int         time;
    if (!readable) {
        time = static_cast<int>(random01() * 24000) % 24000;
        ob   = "§k";
        br   = "§r";
    } else {
        time = (dayTime() + 6000) % 24000;
    }
    int         minutes = static_cast<int>(((time % 1000) / 1000.f) * 60);
    int         hours   = time / 1000;
    std::string postfix;
    if (!config().clock24h) {
        postfix = time < 12000 ? trFor(player, "cm.instrument.am", " AM") : trFor(player, "cm.instrument.pm", " PM");
        hours %= 12;
        if (hours == 0) hours = 12;
    }
    char mm[8];
    std::snprintf(mm, sizeof(mm), "%02d", minutes);
    return ob + std::to_string(hours) + br + ":" + ob + mm + br + postfix;
}

void showTime(Player& player, BlockSource& region) {
    auto text = timeText(player, region);
    actionBar(player, text);
    log().info("instrument: time shown to {}: {}", player.getRealName(), text);
}

} // namespace cm::feature::instrument::clock
