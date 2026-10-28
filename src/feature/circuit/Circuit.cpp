#include "feature/circuit/Circuit.h"

#include <deque>
#include <map>
#include <set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/components/BlockRedstoneConsumerComponent.h"
#include "mc/world/level/block/components/BlockRedstoneProducerComponent.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"
#include "mc/world/redstone/circuit/components/CircuitComponentType.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::feature::circuit {

CM_REGISTER_CONFIG("circuit", CircuitConfig);

namespace {

constexpr char const* kBlocksKey = "cm:circuit:blocks";

struct State {
    std::map<core::GlobalPos, Kind> blocks;
    bool                            dirty  = false;
    bool                            loaded = false;
    // BlockType pointers of our blocks (resolved lazily; the registry is global).
    BlockType const* relayer     = nullptr;
    BlockType const* speaker     = nullptr;
    BlockType const* display     = nullptr;
    BlockType const* illuminator = nullptr;
    bool             resolved    = false;
};
State& st() {
    static State s;
    return s;
}

void resolveTypes() {
    auto& s = st();
    if (s.resolved) return;
    auto get = [](std::string_view id) -> BlockType const* {
        auto t = BlockType::tryGetFromRegistry(HashedString(std::string(id)));
        return t ? t.as_ptr() : nullptr;
    };
    s.relayer     = get(kRelayer);
    s.speaker     = get(kSpeaker);
    s.display     = get(kDisplay);
    s.illuminator = get(kIlluminator);
    s.resolved    = s.relayer || s.speaker || s.display || s.illuminator;
}

BlockPos offsetPos(BlockPos const& p, int facing, int n = 1) {
    auto o = facingOffset(facing);
    return BlockPos{p.x + o.x * n, p.y + o.y * n, p.z + o.z * n};
}

int stateOr(Block const& block, std::string_view name, int fallback) {
    return game::intState(block, name).value_or(fallback);
}

Block const* withState(Block const& block, std::string_view name, int value) {
    auto const* state = block.getBlockType().getBlockState(HashedString(std::string(name)));
    if (!state) return nullptr;
    auto b = block.setState(*state, value);
    return b ? b.as_ptr() : nullptr;
}

/// Sets `name` = `value` on the block at `pos` if it differs. Returns true if it changed.
bool updateState(BlockSource& region, BlockPos const& pos, Block const& block, std::string_view name, int value) {
    if (stateOr(block, name, value) == value) return false;
    auto const* next = withState(block, name, value);
    if (!next) return false;
    return game::setBlock(region, pos, *next);
}

// ---- persistence --------------------------------------------------------------------------------
void loadBlocks() {
    auto& s = st();
    s.blocks.clear();
    s.loaded = true;
    auto j   = game::db::load(kBlocksKey);
    if (!j || !j->is_array()) return;
    for (auto const& e : *j) {
        if (!e.is_array() || e.size() != 5) continue;
        bool ints = true;
        for (auto const& v : e) ints = ints && v.is_number_integer();
        if (!ints) continue;
        int k = e[0].get<int>();
        if (k <= static_cast<int>(Kind::None) || k > static_cast<int>(Kind::Illuminator)) continue;
        s.blocks[core::GlobalPos{e[1].get<int>(), e[2].get<int>(), e[3].get<int>(), e[4].get<int>()}] =
            static_cast<Kind>(k);
    }
}

void saveBlocks() {
    auto& s = st();
    if (!s.dirty) return;
    nlohmann::json arr = nlohmann::json::array();
    for (auto const& [p, k] : s.blocks) arr.push_back({static_cast<int>(k), p.dim, p.x, p.y, p.z});
    game::db::save(kBlocksKey, arr);
    s.dirty = false;
}

void untrack(core::GlobalPos const& pos) {
    auto& s  = st();
    auto  it = s.blocks.find(pos);
    if (it == s.blocks.end()) return;
    if (it->second == Kind::Speaker) eraseSpeakerData(pos);
    s.blocks.erase(it);
    s.dirty = true;
}

// ---- circuit access -----------------------------------------------------------------------------
/// -1 unknown, 0 our std::hash<BlockPos> differs from the game's (linear search), 1 same.
int gHashMatches = -1;

BaseCircuitComponent* findComponent(Dimension& dim, BlockPos const& pos) {
    auto& sys = dim.mCircuitSystem;
    if (!sys) return nullptr;
    auto& map = sys->mSceneGraph->mAllComponents;
    if (map.empty()) return nullptr;
    if (gHashMatches < 0 && map.size() >= 8) {
        // our std::hash<BlockPos> must bucket like the game's: probe several stored keys
        int probed = 0;
        int found  = 0;
        for (auto it = map.begin(); it != map.end() && probed < 32; ++it, ++probed) {
            found += map.find(it->first) == it ? 1 : 0;
        }
        gHashMatches = found == probed ? 1 : 0;
        log().debug("circuit: component map lookup by {}", gHashMatches ? "hash" : "scan");
    }
    if (gHashMatches == 1) {
        auto it = map.find(pos);
        return it == map.end() ? nullptr : it->second.get();
    }
    for (auto const& [p, c] : map) {
        if (p == pos) return c.get();
    }
    return nullptr;
}

bool isConsumer(CircuitComponentType t) {
    auto v = static_cast<std::uint64_t>(t);
    return (v & static_cast<std::uint64_t>(CircuitComponentType::GroupMask))
        == static_cast<std::uint64_t>(CircuitComponentType::ConsumerComponent);
}

// ---- evaluation -----------------------------------------------------------------------------
void evaluateRelayers(Level& level, std::vector<core::GlobalPos> const& relayers) {
    std::deque<core::GlobalPos> work(relayers.begin(), relayers.end());
    int                         budget = static_cast<int>(relayers.size()) * 4 + 16; // chains settle quickly
    while (!work.empty() && budget-- > 0) {
        auto gp = work.front();
        work.pop_front();
        auto dim = level.getDimension(gp.dim).lock();
        if (!dim) continue;
        auto& region = dim->getBlockSourceFromMainChunkSource();
        auto  pos    = game::blockPos(gp);
        if (!region.hasChunksAt(pos, 0, false)) continue;
        auto const& block = region.getBlock(pos);
        if (kindOf(block) != Kind::Relayer) continue;
        // no producer yet (chunk just loaded, circuit not rebuilt): keep the state until it exists
        if (!findComponent(*dim, pos)) continue;
        int  facing  = facingOf(block);
        bool powered = relayerInput(region, pos, facing) > 0;
        if (updateState(region, pos, block, kPowered, powered ? 1 : 0)) {
            // a relayer in front reads our state directly: settle it in the same tick
            auto front = offsetPos(pos, facing);
            auto fgp   = game::globalPos(gp.dim, front);
            if (tracked(fgp)) work.push_back(fgp);
        }
    }
}

int displayRow(BlockSource& region, BlockPos const& pos, int front, int& indexOut) {
    auto same = [&](BlockPos const& p) {
        if (!region.hasChunksAt(p, 0, false)) return false;
        auto const& b = region.getBlock(p);
        return kindOf(b) == Kind::Display && facingOf(b) == front;
    };
    int left = 0;
    while (left < 64 && same(offsetPos(pos, viewerLeft(front), left + 1))) ++left;
    int right = 0;
    while (right < 64 && same(offsetPos(pos, viewerRight(front), right + 1))) ++right;
    indexOut = left;
    return left + right + 1;
}

void evaluateConsumer(Dimension& dim, BlockSource& region, core::GlobalPos const& gp, Kind kind) {
    auto        pos   = game::blockPos(gp);
    auto const& block = region.getBlock(pos);
    auto        input = componentStrength(dim, pos);
    if (!input) return; // the circuit has not rebuilt our component yet: keep the current state
    int strength = *input;
    switch (kind) {
    case Kind::Illuminator:
        updateState(region, pos, block, kPower, illuminatorLight(strength));
        break;
    case Kind::Display: {
        int index  = 0;
        int length = displayRow(region, pos, facingOf(block), index);
        int chain  = static_cast<int>(chainRole(index, length));
        if (stateOr(block, kPower, -1) != strength || stateOr(block, kChain, -1) != chain) {
            Block const* next = withState(block, kPower, std::clamp(strength, 0, 15));
            if (next) next = withState(*next, kChain, chain);
            if (next) game::setBlock(region, pos, *next);
        }
        break;
    }
    case Kind::Speaker: {
        bool powered = strength > 0;
        bool was     = stateOr(block, kPowered, 0) != 0;
        if (powered != was && updateState(region, pos, block, kPowered, powered ? 1 : 0) && powered) {
            speak(region, pos);
        }
        break;
    }
    default:
        break;
    }
}

/// A redstone component of one of our blocks is set up (placement, chunk load): make sure the
/// block is tracked, so blocks the block-change event never saw (structures, lost saves) work too.
void onComponentSetup(Block const& block, BlockSource& region, BlockPos const& pos) {
    Kind k = kindOf(block);
    if (k == Kind::None || region.getLevel().isClientSide()) return;
    auto gp = game::globalPos(region, pos);
    auto it = st().blocks.find(gp);
    if (it != st().blocks.end() && it->second == k) return;
    st().blocks[gp] = k;
    st().dirty      = true;
}

LL_TYPE_STATIC_HOOK(
    ConsumerSetupHook,
    ll::memory::HookPriority::Normal,
    BlockRedstoneConsumerComponent,
    &BlockRedstoneConsumerComponent::setupRedstoneComponent,
    void,
    Block const&    block,
    BlockSource&    region,
    BlockPos const& pos
) {
    origin(block, region, pos);
    onComponentSetup(block, region, pos);
}

LL_TYPE_STATIC_HOOK(
    ProducerSetupHook,
    ll::memory::HookPriority::Normal,
    BlockRedstoneProducerComponent,
    &BlockRedstoneProducerComponent::setupRedstoneComponent,
    void,
    Block const&    block,
    BlockSource&    region,
    BlockPos const& pos
) {
    origin(block, region, pos);
    onComponentSetup(block, region, pos);
}

class CircuitModule {
public:
    static CircuitModule& get() {
        static CircuitModule m;
        return m;
    }

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mChanged  = bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            Kind was = kindOf(ev.previousBlock());
            Kind now = kindOf(ev.newBlock());
            if (was == now) return;
            auto gp = game::globalPos(region, ev.pos());
            if (was != Kind::None) untrack(gp);
            if (now != Kind::None) {
                st().blocks[gp] = now;
                st().dirty      = true;
            }
        });
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
            evaluateAll();
            tickCarts();
        });
        game::db::onWorldReady([] {
            st().resolved = false;
            gHashMatches  = -1;
            loadBlocks();
        });
        game::db::onWorldSave([] { saveBlocks(); });
        game::db::onWorldClosed([] {
            st().blocks.clear();
            st().loaded   = false;
            st().resolved = false;
            gHashMatches  = -1;
            clearSpeakerCache();
            clearCartCache();
        });
        if (game::db::available()) loadBlocks(); // BDS: the world already exists
        ConsumerSetupHook::hook();
        ProducerSetupHook::hook();
        installSpeaker();
        installCart();
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mChanged);
        bus.removeListener(mTick);
        ConsumerSetupHook::unhook();
        ProducerSetupHook::unhook();
        uninstallSpeaker();
        uninstallCart();
        if (game::db::available()) saveBlocks();
    }

private:
    ll::event::ListenerPtr mChanged;
    ll::event::ListenerPtr mTick;
};

} // namespace

Kind kindOf(Block const& block) {
    resolveTypes();
    auto& s = st();
    if (!s.resolved) return Kind::None;
    auto const* t = &block.getBlockType();
    if (t == s.relayer) return Kind::Relayer;
    if (t == s.speaker) return Kind::Speaker;
    if (t == s.display) return Kind::Display;
    if (t == s.illuminator) return Kind::Illuminator;
    return Kind::None;
}

int facingOf(Block const& block) {
    switch (kindOf(block)) {
    case Kind::Relayer:
        return std::clamp(stateOr(block, kFacing6, 2), 0, 5);
    case Kind::Speaker:
    case Kind::Display:
        return cardinalToFacing(stateOr(block, kFacing4, 0));
    default:
        return 2;
    }
}

std::optional<int> componentStrength(Dimension& dim, BlockPos const& pos) {
    auto* c = findComponent(dim, pos);
    if (!c) return std::nullopt;
    return std::clamp(c->getStrength(), 0, 15);
}

std::string describeComponent(Dimension& dim, BlockPos const& pos) {
    auto* c = findComponent(dim, pos);
    if (!c) return "none";
    return "type=" + std::to_string(static_cast<std::uint64_t>(c->getCircuitComponentType()))
         + " dir=" + std::to_string(c->getDirection()) + " strength=" + std::to_string(c->getStrength());
}

int relayerInput(BlockSource& region, BlockPos const& pos, int facing) {
    auto back = offsetPos(pos, oppositeFacing(facing));
    if (!region.hasChunksAt(back, 0, false)) return 0;
    auto const& behind = region.getBlock(back);
    switch (kindOf(behind)) {
    case Kind::Relayer: // a relayer powers only the block its front faces
        return facingOf(behind) == facing && stateOr(behind, kPowered, 0) ? 15 : 0;
    case Kind::None:
        break;
    default: // our consumers never output
        return 0;
    }
    auto* c = findComponent(region.getDimension(), back);
    if (!c) return 0;
    auto type = c->getCircuitComponentType();
    if (isConsumer(type)) return 0; // lamps, pistons, ... only take power
    int strength = std::clamp(c->getStrength(), 0, 15);
    if (strength <= 0) return 0;
    switch (type) {
    case CircuitComponentType::RepeaterCapacitor:
    case CircuitComponentType::ComparatorCapacitor:
    case CircuitComponentType::PulseCapacitor:
        // directional outputs: only when they point at us. Their direction is the side they read
        // from (a repeater facing "west" takes power from the west and outputs to the east).
        return oppositeFacing(c->getDirection()) == facing ? strength : 0;
    default:
        return strength;
    }
}

void evaluateAll() {
    auto level = ll::service::getLevel();
    if (!level || !st().loaded) return;
    std::vector<core::GlobalPos>                  relayers;
    std::vector<std::pair<core::GlobalPos, Kind>> consumers;
    for (auto const& [gp, kind] : st().blocks) {
        if (kind == Kind::Relayer) relayers.push_back(gp);
        else consumers.emplace_back(gp, kind);
    }
    // drop entries whose block is gone (outside a block change, e.g. a world edited offline)
    std::vector<core::GlobalPos> gone;
    for (auto const& [gp, kind] : consumers) {
        auto dim = level->getDimension(gp.dim).lock();
        if (!dim) continue;
        auto& region = dim->getBlockSourceFromMainChunkSource();
        auto  pos    = game::blockPos(gp);
        if (!region.hasChunksAt(pos, 0, false)) continue;
        if (kindOf(region.getBlock(pos)) != kind) {
            gone.push_back(gp);
            continue;
        }
        evaluateConsumer(*dim, region, gp, kind);
    }
    for (auto const& gp : relayers) {
        auto dim = level->getDimension(gp.dim).lock();
        if (!dim) continue;
        auto& region = dim->getBlockSourceFromMainChunkSource();
        auto  pos    = game::blockPos(gp);
        if (region.hasChunksAt(pos, 0, false) && kindOf(region.getBlock(pos)) != Kind::Relayer) gone.push_back(gp);
    }
    for (auto const& gp : gone) untrack(gp);
    evaluateRelayers(*level, relayers);
}

void track(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) return;
    Kind k = kindOf(region.getBlock(pos));
    if (k == Kind::None) return;
    st().blocks[game::globalPos(region, pos)] = k;
    st().dirty                                = true;
}

bool tracked(core::GlobalPos const& pos) { return st().blocks.contains(pos); }

} // namespace cm::feature::circuit

CM_REGISTER_MODULE(cm::feature::circuit::CircuitModule);
