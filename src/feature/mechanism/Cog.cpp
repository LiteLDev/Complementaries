// Cog block: a redstone conductor in block form. Every connected cluster is resolved at once:
// a cog's power is its strongest non-cog input, or a neighbouring cog's power minus one.

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <vector>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/mechanism/Mechanism.h"

namespace cm::feature::mechanism::cog {

namespace {
constexpr std::size_t kMaxCluster = 4096;
using Key                         = std::tuple<int, int, int>;
Key keyOf(BlockPos const& p) { return {p.x, p.y, p.z}; }
} // namespace

void tickAll(BlockSource& region, std::vector<BlockPos> const& cogs) {
    bool          enabled = config().cogBlock.enabled;
    std::set<Key> visited;
    for (auto const& start : cogs) {
        if (visited.contains(keyOf(start)) || kindAt(region, start) != Kind::Cog) continue;
        // collect the cluster
        std::vector<BlockPos> nodes;
        std::map<Key, int>    index;
        std::vector<BlockPos> stack{start};
        visited.insert(keyOf(start));
        while (!stack.empty() && nodes.size() < kMaxCluster) {
            BlockPos p = stack.back();
            stack.pop_back();
            index[keyOf(p)] = static_cast<int>(nodes.size());
            nodes.push_back(p);
            for (int d = 0; d < 6; ++d) {
                BlockPos n = offset(p, d);
                if (visited.contains(keyOf(n)) || kindAt(region, n) != Kind::Cog) continue;
                visited.insert(keyOf(n));
                stack.push_back(n);
            }
        }
        // over the cap: the rest is resolved as clusters of its own
        for (auto const& rest : stack) visited.erase(keyOf(rest));
        // power a dust line or block got from this cluster is no input (no self-latching)
        auto ownCog = [&index](BlockPos const& q) { return index.contains(keyOf(q)); };
        // external inputs, then spread with a loss of one per cog (strongest first)
        std::vector<int>                         power(nodes.size(), 0);
        std::priority_queue<std::pair<int, int>> queue;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (!enabled) continue;
            int best = 0;
            for (int d = 0; d < 6; ++d) best = std::max(best, signalInto(region, nodes[i], d, ownCog));
            power[i] = best;
            if (best > 0) queue.emplace(best, static_cast<int>(i));
        }
        while (!queue.empty()) {
            auto [p, i] = queue.top();
            queue.pop();
            if (p < power[static_cast<std::size_t>(i)] || p <= 1) continue;
            for (int d = 0; d < 6; ++d) {
                auto it = index.find(keyOf(offset(nodes[static_cast<std::size_t>(i)], d)));
                if (it == index.end()) continue;
                auto j = static_cast<std::size_t>(it->second);
                if (power[j] < p - 1) {
                    power[j] = p - 1;
                    queue.emplace(p - 1, it->second);
                }
            }
        }
        // write back the changes
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            Block const& b       = region.getBlock(nodes[i]);
            int          cur     = intStateOf(b, "complementaries:power", 0);
            bool         twisted = boolStateOf(b, "complementaries:twisting");
            if (cur == power[i] && twisted == (power[i] > 0)) continue;
            if (auto const* nb = withStates(
                    b,
                    {
                        {"complementaries:power",    rotation::Value{power[i]}    },
                        {"complementaries:twisting", rotation::Value{power[i] > 0}}
            }
                ))
                place(region, nodes[i], *nb);
        }
    }
}

} // namespace cm::feature::mechanism::cog
