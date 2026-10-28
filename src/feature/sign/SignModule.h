#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ll/api/event/ListenerBase.h"

#include "mc/world/level/BlockPos.h"

class ItemStack;
class Player;

namespace cm::feature::sign {

class SignModule {
public:
    static SignModule& get();
    void               install();
    void               uninstall();

    [[nodiscard]] std::uint64_t tick() const { return mTick; }

private:
    struct LastClick {
        std::uint64_t tick = 0;
        BlockPos      pos{0, -10000, 0};
    };
    std::vector<ll::event::ListenerPtr>                mListeners;
    std::uint64_t                                      mTick = 0;
    std::unordered_map<std::int64_t, LastClick>        mLastClick;
};

/// Where the player's selected compass points to in `dim` (x, z), nullopt when it spins.
[[nodiscard]] std::optional<std::pair<double, double>> compassTarget(Player& player, int dim);
/// The lodestone (x, z) a lodestone compass is linked to, when it is in `dim`.
[[nodiscard]] std::optional<std::pair<int, int>> lodestoneOf(ItemStack const& compass, int dim);

/// Way sign generation step (every 5 s).
void tickWaySigns();

} // namespace cm::feature::sign
