// Selftests of the foundation: world storage, translations, configs, blocks and simulated players.

#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/ModConfig.h"
#include "selftest/SelfTest.h"

namespace {
using cm::selftest::Context;
}

CM_REGISTER_SELFTEST("foundation.world_db", [](Context& ctx) {
    namespace db = cm::game::db;
    ctx.require(db::available(), "a world is loaded");
    std::string const key = "cm:foundation:probe";
    db::save(key, {{"value", 42}});
    auto back = db::load(key);
    ctx.check(back && back->value("value", 0) == 42, "saved JSON reads back");
    int seen = 0;
    db::forEachKey("cm:foundation:", [&seen](std::string_view suffix) {
        if (suffix == "probe") ++seen;
    });
    ctx.check(seen == 1, "forEachKey finds the key by prefix");
    db::erase(key);
    ctx.check(!db::load(key), "erased key is gone");
});

CM_REGISTER_SELFTEST("foundation.i18n", [](Context& ctx) {
    ctx.check(cm::tr("cm.test.missing", "Fallback {}", 1) == "Fallback 1", "missing keys use the fallback");
    ctx.check(cm::translate("cm.mod.name", "?", "en_US") == "Complementaries", "en_US lang file is loaded");
    ctx.check(cm::translate("cm.mod.name", "?", "zh_CN") != "?", "zh_CN lang file is loaded");
    ctx.check(!cm::config::configDir().empty(), "config directory is set");
    ctx.check(cm::config::get<cm::ModConfig>().version >= 1, "mod config is loaded");
});

CM_REGISTER_SELFTEST("foundation.blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(10);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:gold_block"), "set a vanilla block");
    co_await ctx.ticks(1);
    auto& region = ctx.player().getDimensionBlockSource();
    ctx.check(cm::game::blockIdAt(region, cm::game::blockPos(pos)) == "minecraft:gold_block", "block reads back");
    ctx.check(cm::game::blockWith("complementaries:no_such_block") == nullptr, "unknown block ids give nullptr");
    // states are applied and read back by value
    auto const* wheat = cm::game::blockWith("minecraft:wheat", {{"growth", 5}});
    ctx.check(wheat && cm::game::intState(*wheat, "growth") == 5, "integer state round-trips");
    auto const* lever = cm::game::blockWith("minecraft:lever", {{"open_bit", 1}});
    ctx.check(lever && cm::game::intState(*lever, "open_bit") == 1, "boolean state round-trips");
    ctx.check(lever && !cm::game::intState(*lever, "no_such_state"), "unknown states read as nullopt");
});

CM_REGISTER_SELFTEST("foundation.players", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& a = ctx.player();
    auto& b = ctx.spawnPlayer();
    co_await ctx.ticks(2);
    ctx.check(&a != static_cast<Player*>(&b), "two distinct simulated players");
    ctx.check(!a.getRealName().empty(), "the simulated player has a name");
});
