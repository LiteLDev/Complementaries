// U1 jar: `/supplementaries jardemo` (operators): builds a row of filled jars and two running
// faucets 3 blocks south of the player. Complementaries-only, used by the e2e screenshots.

#include <algorithm>

#include "ll/api/command/CommandHandle.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/jar/Jar.h"
#include "game/Blocks.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::jar {

namespace {

StoredMob demoMob(std::string type, int variant = 0, std::string bucket = {}) {
    StoredMob m;
    m.name    = prettyMobName(type);
    m.display = displayKindFor(type, variant, false);
    m.type    = std::move(type);
    m.bucket  = std::move(bucket);
    return m;
}

Contents demoFluid(std::string id, int n, int data = 0, std::string bottle = {}) {
    Contents c;
    c.fluid = {std::move(id), std::min(n, capacity()), data, std::move(bottle)};
    if (c.fluid.count <= 0) c.fluid = {};
    return c;
}

/// Places a block where nothing would be lost (air, grass, ...).
bool put(
    BlockSource&                                         region,
    core::GlobalPos const&                               p,
    std::string_view                                     id,
    std::vector<core::GlobalPos>&                        placed,
    std::vector<std::pair<std::string_view, int>> const& states = {}
) {
    auto bp = game::blockPos(p);
    if (!game::replaceable(region, bp)) return false;
    auto const* b = game::blockWith(id, states);
    if (!b || !game::setBlock(region, bp, *b)) return false;
    placed.push_back(p);
    return true;
}

} // namespace

std::vector<core::GlobalPos> buildJarDemo(Player& player) {
    std::vector<core::GlobalPos> placed;
    auto&                        region = player.getDimensionBlockSource();
    auto                         feet   = game::blockPosOf(player.getFeetPos());
    int                          dim    = static_cast<int>(region.getDimensionId());
    core::GlobalPos              o{dim, feet.x, feet.y, feet.z + 3};

    std::vector<Contents> row;
    row.push_back(demoFluid("water", 12));
    row.push_back(demoFluid("lava", 8));
    row.push_back(demoFluid("honey", 6));
    row.push_back(demoFluid("milk", 4));
    row.push_back(demoFluid("beetroot_soup", 6));
    row.push_back(demoFluid("potion", 10, 14, "minecraft:potion"));
    row.push_back(demoFluid("potion", 5, 21, "minecraft:potion"));
    row.push_back(demoFluid("experience", 12));
    Contents cookies;
    for (int i = 0; i < 7; ++i) cookies.items.push_back({"minecraft:cookie", 0, {}});
    row.push_back(cookies);
    for (auto const& m :
         {demoMob("minecraft:bee"),
          demoMob("minecraft:slime"),
          demoMob("minecraft:frog", 2),
          demoMob("minecraft:cod", 0, "minecraft:cod_bucket"),
          demoMob("minecraft:axolotl", 0, "minecraft:axolotl_bucket")}) {
        Contents c;
        c.mob = m;
        row.push_back(c);
    }
    int x0 = o.x - static_cast<int>(row.size()) / 2;
    for (size_t i = 0; i < row.size(); ++i) {
        core::GlobalPos p{dim, x0 + static_cast<int>(i), o.y, o.z};
        if (put(region, p, kJarId, placed)) setJar(p, row[i]);
    }

    // a faucet pouring lava from a jar into the jar below it, and one filling a cauldron
    core::GlobalPos lowJar{dim, x0 - 3, o.y, o.z};
    core::GlobalPos faucet1{dim, x0 - 3, o.y + 1, o.z};
    core::GlobalPos src1{dim, x0 - 3, o.y + 1, o.z + 1};
    if (put(region, lowJar, kJarId, placed)) setJar(lowJar, {});
    if (put(region, src1, kJarId, placed)) setJar(src1, demoFluid("lava", 12));
    if (put(region,
            faucet1,
            kFaucetId,
            placed,
            {
                {kStateFacing,  2},
                {kStateEnabled, 1}
    }))
        faucetPlaced(faucet1);

    core::GlobalPos cauldron{dim, x0 - 6, o.y, o.z};
    core::GlobalPos faucet2{dim, x0 - 6, o.y + 1, o.z};
    core::GlobalPos src2{dim, x0 - 6, o.y + 1, o.z + 1};
    put(region, cauldron, "minecraft:cauldron", placed);
    if (put(region, src2, kJarId, placed)) setJar(src2, demoFluid("water", 12));
    if (put(region,
            faucet2,
            kFaucetId,
            placed,
            {
                {kStateFacing,  2},
                {kStateEnabled, 1}
    }))
        faucetPlaced(faucet2);
    log().info("jar: demo built at {}", o.key());
    return placed;
}

} // namespace cm::feature::jar

CM_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("jardemo").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!cm::commands::isAdmin(origin)) return output.error("You need operator permission.");
        auto* player = cm::commands::playerOf(origin);
        if (!player) return output.error(cm::tr("cm.jar.demo.player", "Run it as a player."));
        cm::feature::jar::buildJarDemo(*player);
        output.success(cm::trFor(*player, "cm.jar.demo.done", "Jar demo built."));
    });
});
