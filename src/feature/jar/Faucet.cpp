// U1 jar: the faucet. Moves soft fluids from the block behind it into the block below, one
// bottle per 20 ticks, while open (right click toggles, redstone power inverts).

#include <algorithm>
#include <map>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"

#include "feature/jar/Jar.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace cm::feature::jar {

namespace {

constexpr int         kCooldownPerBottle = 20;
constexpr char const* kFaucetsKey        = "cm:jar:faucets";

struct FaucetRuntime {
    int  cooldown = 0;
    bool dirty    = true;
};
std::map<core::GlobalPos, FaucetRuntime> gFaucets;
int                                      gTick = 0;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

// ---- vanilla block state access ---------------------------------------------------------------

/// The default state of `id` with `states` changed (from its serialization tag, so every vanilla
/// state type works).
Block const* vanillaBlock(std::string_view id, Block::BlockStatesType const& states = {}) {
    auto base = Block::tryGetFromRegistry(HashedString(std::string(id)));
    if (!base) return nullptr;
    if (states.empty()) return base.as_ptr();
    CompoundTag tag = *base->mSerializationId;
    if (!tag.contains("states", Tag::Type::Compound)) return base.as_ptr();
    auto& s = tag["states"].get<CompoundTag>();
    for (auto const& [name, value] : states) {
        if (!s.contains(name)) continue;
        std::visit(
            [&](auto const& v) {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, std::string>) {
                    s[name] = v;
                } else if constexpr (std::is_same_v<V, bool>) {
                    s[name] = static_cast<uchar>(v ? 1 : 0);
                } else if constexpr (std::is_same_v<V, int>) {
                    s[name] = v;
                }
            },
            value
        );
    }
    auto b = Block::tryGetFromRegistry(tag);
    if (!b) log().warn("faucet: no block state {} {}", id, tag.toSnbt());
    return b ? b.as_ptr() : nullptr;
}

bool place(BlockSource& region, BlockPos const& pos, Block const* block) {
    return block && game::setBlock(region, pos, *block);
}

Block const* cauldron(int javaLevel, std::string const& liquid) {
    if (javaLevel <= 0)
        return vanillaBlock(
            "minecraft:cauldron",
            {
                {"fill_level",      0                   },
                {"cauldron_liquid", std::string("water")}
        }
        );
    return vanillaBlock(
        "minecraft:cauldron",
        {
            {"fill_level", std::min(javaLevel, 3) * 2},
            {"cauldron_liquid", liquid}
    }
    );
}

/// Java cauldron level (0..3) of a Bedrock cauldron (fill_level 0..6).
int cauldronLevel(Block const& b) { return (vanillaInt(b, "fill_level") + 1) / 2; }

bool isBeehive(Block const& b) {
    auto n = b.getTypeName();
    return n == "minecraft:beehive" || n == "minecraft:bee_nest";
}

// ---- facing -------------------------------------------------------------------------------------

/// Java 2D facing (0 S, 1 W, 2 N, 3 E) of the spout.
int facingOf(Block const& b) { return stateOf(b, kStateFacing, 2) & 3; }

BlockPos behindOf(BlockPos const& pos, int facing) {
    auto [dx, dz] = game::facingOffset(facing);
    return BlockPos{pos.x - dx, pos.y, pos.z - dz};
}

// ---- redstone -----------------------------------------------------------------------------------

bool powered(BlockSource& region, BlockPos const& pos) {
    auto& dim = region.getDimension();
    auto* cs  = dim.mCircuitSystem.get();
    if (!cs) return false;
    auto const&          all     = cs->mSceneGraph->mAllComponents;
    static constexpr int d[6][3] = {
        {0,  -1, 0 },
        {0,  1,  0 },
        {0,  0,  -1},
        {0,  0,  1 },
        {-1, 0,  0 },
        {1,  0,  0 }
    };
    for (auto const& o : d) {
        BlockPos n{pos.x + o[0], pos.y + o[1], pos.z + o[2]};
        auto     it = all.find(n);
        if (it == all.end() || !it->second) continue;
        auto& c = *it->second;
        if (c.getStrength() <= 0) continue;
        auto type = c.getCircuitComponentType();
        if (type == CircuitComponentType::RepeaterCapacitor || type == CircuitComponentType::ComparatorCapacitor) {
            // these only power the block they face: facing id 2 N, 3 S, 4 W, 5 E
            static constexpr int f[6][3] = {
                {0,  -1, 0 },
                {0,  1,  0 },
                {0,  0,  -1},
                {0,  0,  1 },
                {-1, 0,  0 },
                {1,  0,  0 }
            };
            int dir = c.getDirection();
            if (dir < 0 || dir > 5) continue;
            if (n.x + f[dir][0] != pos.x || n.y + f[dir][1] != pos.y || n.z + f[dir][2] != pos.z) continue;
        }
        return true;
    }
    return false;
}

// ---- sources ------------------------------------------------------------------------------------

struct Offer {
    FluidStack fluid;
    int        minAmount = 1;
};

enum class SourceKind { None, Cauldron, Beehive, Jar, Brewing, Liquid };

struct Source {
    SourceKind kind = SourceKind::None;
    Offer      offer;
    int        slot = -1;
};

bool jarAcceptsFluid(Contents const& c) { return cfg().jarLiquids && c.items.empty() && !c.mob; }

Source findSource(BlockSource& region, BlockPos const& at) {
    auto const& b = region.getBlock(at);
    if (b.isAir()) return {};
    auto name = b.getTypeName();
    if (name == "minecraft:cauldron") {
        int  lvl    = cauldronLevel(b);
        auto liquid = vanillaString(b, "cauldron_liquid");
        if (lvl > 0) {
            if (liquid == "water")
                return {
                    SourceKind::Cauldron,
                    {{"water", lvl}, 1}
                };
            if (liquid == "lava" && lvl >= 3)
                return {
                    SourceKind::Cauldron,
                    {{"lava", kBucket}, kBucket}
                };
            if (liquid == "powder_snow")
                return {
                    SourceKind::Cauldron,
                    {{"powder_snow", lvl}, 1}
                };
        }
        return {};
    }
    if (isBeehive(b)) {
        if (vanillaInt(b, "honey_level") >= 5)
            return {
                SourceKind::Beehive,
                {{"honey", 1}, 1}
            };
        return {};
    }
    if (name == kJarId) {
        auto c = jarAt(game::globalPos(region, at));
        if (jarAcceptsFluid(c) && !c.fluid.empty())
            return {
                SourceKind::Jar,
                {c.fluid, 1}
            };
        return {};
    }
    if (name == "minecraft:brewing_stand") {
        if (auto cont = region.tryGetContainer(at)) {
            for (int i = 1; i <= 3 && i < cont->getContainerSize(); ++i) {
                if (auto d = fluidFromItem(refOf(cont->getItem(i)))) {
                    d->fluid.count = 1;
                    return {
                        SourceKind::Brewing,
                        {d->fluid, 1},
                        i
                    };
                }
            }
        }
        return {};
    }
    // a source block of water (also a waterlogged block) or lava
    auto const& liquid = region.getLiquidBlock(at);
    auto        ln     = liquid.getTypeName();
    if ((ln == "minecraft:water" || ln == "minecraft:flowing_water") && vanillaInt(liquid, "liquid_depth", 1) == 0) {
        return {
            SourceKind::Liquid,
            {{"water", kBucket}, 1}
        };
    }
    if ((ln == "minecraft:lava" || ln == "minecraft:flowing_lava") && vanillaInt(liquid, "liquid_depth", 1) == 0) {
        return {
            SourceKind::Liquid,
            {{"lava", kBucket}, kBucket}
        };
    }
    return {};
}

void drain(BlockSource& region, BlockPos const& at, Source const& s, int amount) {
    auto const& b = region.getBlock(at);
    switch (s.kind) {
    case SourceKind::Cauldron: {
        auto liquid = vanillaString(b, "cauldron_liquid");
        int  left   = liquid == "lava" ? 0 : cauldronLevel(b) - amount;
        place(region, at, cauldron(std::max(left, 0), liquid));
        break;
    }
    case SourceKind::Beehive:
        place(
            region,
            at,
            vanillaBlock(
                b.getTypeName(),
                {
                    {"honey_level", 0},
                    {"direction", vanillaInt(b, "direction")}
        }
            )
        );
        break;
    case SourceKind::Jar: {
        auto pos       = game::globalPos(region, at);
        auto c         = jarAt(pos);
        c.fluid.count -= std::min(amount, c.fluid.count);
        if (c.fluid.count <= 0) c.fluid = {};
        setJar(pos, c);
        break;
    }
    case SourceKind::Brewing:
        if (auto cont = region.tryGetContainer(at)) cont->setItem(s.slot, makeItem("minecraft:glass_bottle"));
        break;
    case SourceKind::Liquid:
        if (s.offer.fluid.fluid == "lava") game::clearBlock(region, at);
        break;
    default:
        break;
    }
}

// ---- targets ------------------------------------------------------------------------------------

/// Pours `offer` into the block below. nullopt = this block takes no fluid; 0 = blocked.
std::optional<int> fillBelow(BlockSource& region, BlockPos const& below, Offer const& offer) {
    auto const& b    = region.getBlock(below);
    auto        name = b.getTypeName();
    auto const& f    = offer.fluid;
    if (name == "minecraft:cauldron") {
        int  lvl    = cauldronLevel(b);
        auto liquid = vanillaString(b, "cauldron_liquid");
        if (lvl == 0) {
            if (f.fluid == "water" || f.fluid == "powder_snow") {
                place(region, below, cauldron(std::min(offer.minAmount, 3), f.fluid));
                return offer.minAmount;
            }
            if (f.fluid == "lava") {
                if (f.count < kBucket && offer.minAmount < kBucket) return std::nullopt;
                place(region, below, cauldron(3, "lava"));
                return kBucket;
            }
            return std::nullopt;
        }
        if (liquid == "water") {
            if (f.fluid == "water" && lvl < 3) {
                int am = std::min(f.count, 3 - lvl);
                place(region, below, cauldron(lvl + am, "water"));
                return std::max(offer.minAmount, am);
            }
            return 0;
        }
        if (liquid == "powder_snow") {
            if (f.fluid == "powder_snow" && lvl < 3) {
                int am = std::min(f.count, 3 - lvl);
                place(region, below, cauldron(lvl + am, "powder_snow"));
                return std::max(offer.minAmount, am);
            }
            if (f.fluid == "water") {
                place(region, below, cauldron(lvl, "water"));
                return offer.minAmount;
            }
            return 0;
        }
        return std::nullopt;
    }
    if (isBeehive(b)) {
        if (f.fluid == "honey" && offer.minAmount == 1 && vanillaInt(b, "honey_level") == 0) {
            place(
                region,
                below,
                vanillaBlock(
                    name,
                    {
                        {"honey_level", 5},
                        {"direction", vanillaInt(b, "direction")}
            }
                )
            );
            return offer.minAmount;
        }
        return 0;
    }
    if (name == kJarId) {
        auto pos = game::globalPos(region, below);
        auto c   = jarAt(pos);
        if (!jarAcceptsFluid(c)) return std::nullopt;
        Tank       t{c.fluid, capacity()};
        FluidStack add = f;
        add.count      = offer.minAmount;
        int added      = t.add(add, true);
        if (added < offer.minAmount) return 0;
        t.add(add, false);
        c.fluid = t.fluid;
        setJar(pos, c);
        return added;
    }
    if (name == "minecraft:brewing_stand") {
        auto cont = region.tryGetContainer(below);
        if (!cont) return std::nullopt;
        int                                  need = offer.minAmount;
        std::vector<std::pair<int, ItemRef>> put;
        FluidStack                           pool = f;
        for (int i = 1; i <= 3 && i < cont->getContainerSize() && need > 0; ++i) {
            if (!isItem(cont->getItem(i), "minecraft:glass_bottle")) continue;
            FluidStack one = pool;
            one.count      = 1;
            if (auto filled = fillItem(one, "minecraft:glass_bottle")) {
                put.emplace_back(i, *filled);
                --need;
            }
        }
        if (need != 0) return std::nullopt;
        for (auto const& [slot, item] : put) cont->setItem(slot, makeItem(item.id, 1, item.aux));
        return offer.minAmount;
    }
    if (name == "minecraft:sponge") return offer.minAmount;
    if (b.isAir() && f.fluid == "experience") {
        int xp = 0;
        for (int i = 0; i < offer.minAmount; ++i)
            xp += 3 + static_cast<int>(rng()() % 5) + static_cast<int>(rng()() % 5);
        Vec3 at{
            static_cast<float>(below.x) + 0.5f,
            static_cast<float>(below.y) + 0.875f,
            static_cast<float>(below.z) + 0.5f
        };
        ExperienceOrb::spawnOrbs(region, at, xp, ExperienceOrb::DropType::FromBlock, nullptr);
        playSound(
            region,
            at,
            "mob.chicken.plop",
            0.3f,
            0.5f + (static_cast<float>(rng()() % 100) / 100.f - 0.5f) / 4.f
        );
        return offer.minAmount;
    }
    if (f.fluid == "water") {
        if (name.ends_with("_concrete_powder")) {
            auto solid = name.substr(0, name.size() - std::string_view("_powder").size());
            place(region, below, vanillaBlock(solid));
            return offer.minAmount;
        }
    }
    if (name == "minecraft:dirt") {
        if (f.fluid != "water") return 0;
        place(region, below, vanillaBlock("minecraft:mud"));
        return offer.minAmount;
    }
    return std::nullopt;
}

// ---- items --------------------------------------------------------------------------------------

/// Takes one item out of a container behind the faucet (hopper-like). Empty stack when none.
ItemStack extractItem(BlockSource& region, BlockPos const& at) {
    auto cont = region.tryGetContainer(at);
    if (!cont) return {};
    int  size  = cont->getContainerSize();
    int  first = 0, last = size - 1;
    auto name = region.getBlock(at).getTypeName();
    if (name == "minecraft:furnace" || name == "minecraft:lit_furnace" || name == "minecraft:blast_furnace"
        || name == "minecraft:lit_blast_furnace" || name == "minecraft:smoker" || name == "minecraft:lit_smoker") {
        first = last = 1; // pulled from the side: the fuel slot
    }
    for (int i = first; i <= last && i < size; ++i) {
        auto const& s = cont->getItem(i);
        if (s.isNull()) continue;
        ItemStack one = s;
        one.set(1);
        ItemStack rest = s;
        rest.remove(1);
        cont->setItem(i, rest.mCount > 0 ? rest : ItemStack::EMPTY_ITEM());
        return one;
    }
    return {};
}

bool giveToEntityBelow(BlockSource& region, BlockPos const& pos, ItemStack const& item) {
    AABB box{
        Vec3{static_cast<float>(pos.x),       static_cast<float>(pos.y) - 0.75f, static_cast<float>(pos.z)      },
        Vec3{static_cast<float>(pos.x) + 1.f, static_cast<float>(pos.y) + 0.25f, static_cast<float>(pos.z) + 1.f}
    };
    auto list = region.getEntities(box, 0.f);
    std::shuffle(list.begin(), list.end(), rng());
    for (auto* e : list) {
        if (!e || e->isPlayer() || !e->isAlive() || e->getTypeName() == kJarMobId) continue;
        ItemStack copy = item;
        if (e->add(copy)) return true;
    }
    return false;
}

// ---- one faucet ---------------------------------------------------------------------------------

struct FaucetView {
    BlockSource* region = nullptr;
    BlockPos     pos;
    Block const* block = nullptr;
};

std::optional<FaucetView> view(core::GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    if (!region) return std::nullopt;
    auto bp = game::blockPos(p);
    if (!region->hasChunksAt(bp, 0, false)) return std::nullopt;
    auto const& b = region->getBlock(bp);
    if (!isBlock(b, kFaucetId)) return std::nullopt;
    return FaucetView{region, bp, &b};
}

bool connectsBelow(BlockSource& region, BlockPos const& pos) {
    auto const& below = region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z});
    return isBlock(below, kJarId) || isBeehive(below);
}

/// Refreshes `connected`, `powered` and `content` (the fluid behind).
void refresh(FaucetView const& v) {
    auto& region = *v.region;
    int   facing = facingOf(*v.block);
    auto  src    = findSource(region, behindOf(v.pos, facing));
    auto  states = contentStates(src.kind == SourceKind::None ? 0 : contentFor(src.offer.fluid));
    states.emplace_back(kStateConnected, connectsBelow(region, v.pos) ? 1 : 0);
    states.emplace_back(kStatePowered, powered(region, v.pos) ? 1 : 0);
    setStates(region, v.pos, states);
}

int tryExtract(FaucetView const& v) {
    auto& region = *v.region;
    int   facing = facingOf(*v.block);
    auto  behind = behindOf(v.pos, facing);
    if (region.getBlock(behind).isAir()) return 0;
    BlockPos below{v.pos.x, v.pos.y - 1, v.pos.z};

    auto src = findSource(region, behind);
    if (src.kind != SourceKind::None && src.kind != SourceKind::Liquid) {
        if (auto filled = fillBelow(region, below, src.offer)) {
            if (*filled == 0) return 0;
            drain(region, behind, src, *filled);
            return *filled * kCooldownPerBottle;
        }
        // nothing below takes it: like Supplementaries, try the item interaction next
    }
    bool        connected = stateOf(*v.block, kStateConnected) != 0;
    auto const& conf      = cfg();
    if (!connected && (conf.spillItems || conf.fillEntitiesBelow)) {
        auto item = extractItem(region, behind);
        if (!item.isNull()) {
            bool given = conf.fillEntitiesBelow && giveToEntityBelow(region, v.pos, item);
            if (!given) {
                Vec3 at{
                    static_cast<float>(v.pos.x) + 0.5f,
                    static_cast<float>(v.pos.y),
                    static_cast<float>(v.pos.z) + 0.5f
                };
                if (auto level = ll::service::getLevel()) level->getSpawner().spawnItem(region, item, nullptr, at, 0);
                playSound(
                    region,
                    at,
                    "mob.chicken.plop",
                    0.3f,
                    0.5f + (static_cast<float>(rng()() % 100) / 100.f - 0.5f) / 4.f
                );
            }
            return kCooldownPerBottle;
        }
    }
    if (src.kind == SourceKind::Liquid) {
        if (auto filled = fillBelow(region, below, src.offer)) {
            if (*filled == 0) return 0;
            drain(region, behind, src, *filled);
            return *filled * kCooldownPerBottle;
        }
    }
    return 0;
}

void drip(FaucetView const& v) {
    int content = contentOf(*v.block);
    if (content == 0 || stateOf(*v.block, kStateConnected)) return;
    bool open = (stateOf(*v.block, kStateEnabled) != 0) != (stateOf(*v.block, kStatePowered) != 0);
    if (open) return; // the stream is drawn by the model
    if (rng()() % 8 != 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    float dx = (static_cast<float>(rng()() % 100) / 100.f - 0.5f) * 0.125f;
    float dz = (static_cast<float>(rng()() % 100) / 100.f - 0.5f) * 0.125f;
    Vec3  at{
        static_cast<float>(v.pos.x) + 0.5f + dx,
        static_cast<float>(v.pos.y) + 0.25f,
        static_cast<float>(v.pos.z) + 0.5f + dz
    };
    level->spawnParticleEffect(
        content == 2 ? "minecraft:lava_drip_particle" : "minecraft:water_drip_particle",
        at,
        &v.region->getDimension()
    );
}

} // namespace

bool faucetKnown(core::GlobalPos const& pos) { return gFaucets.contains(pos); }

void faucetPlaced(core::GlobalPos const& pos) {
    gFaucets[pos] = {};
    if (auto v = view(pos)) refresh(*v);
}

void faucetRemoved(core::GlobalPos const& pos) { gFaucets.erase(pos); }

void faucetNeighborChanged(core::GlobalPos const& pos) {
    if (auto it = gFaucets.find(pos); it != gFaucets.end()) it->second.dirty = true;
}

void toggleFaucet(core::GlobalPos const& pos) {
    if (!gFaucets.contains(pos)) gFaucets[pos] = {};
    auto v = view(pos);
    if (!v) return;
    bool enabled = stateOf(*v->block, kStateEnabled) != 0;
    playSound(
        *v->region,
        Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f},
        "random.click",
        1.f,
        enabled ? 1.f : 1.2f
    );
    setStates(
        *v->region,
        v->pos,
        {
            {kStateEnabled, enabled ? 0 : 1}
    }
    );
    gFaucets[pos].dirty = true;
    log().info("faucet: toggled at {} ({})", pos.key(), enabled ? "closed" : "opened");
}

int runFaucetOnce(core::GlobalPos const& pos) {
    auto v = view(pos);
    if (!v) return 0;
    return tryExtract(*v);
}

void tickFaucets() {
    if (!cfg().faucetEnabled || gFaucets.empty()) return;
    ++gTick;
    std::vector<core::GlobalPos> gone;
    std::vector<core::GlobalPos> list;
    list.reserve(gFaucets.size());
    for (auto const& [pos, rt] : gFaucets) list.push_back(pos);
    for (auto const& pos : list) {
        auto it = gFaucets.find(pos);
        if (it == gFaucets.end()) continue;
        auto* region = regionOf(pos.dim);
        if (!region || !region->hasChunksAt(game::blockPos(pos), 0, false)) continue;
        auto v = view(pos);
        if (!v) {
            gone.push_back(pos);
            continue;
        }
        auto& rt = it->second;
        if (rt.dirty || gTick % 10 == 0) {
            rt.dirty = false;
            refresh(*v);
            v = view(pos);
            if (!v) continue;
        } else {
            bool p = powered(*v->region, v->pos);
            if (p != (stateOf(*v->block, kStatePowered) != 0)) {
                setStates(
                    *v->region,
                    v->pos,
                    {
                        {kStatePowered, p ? 1 : 0}
                }
                );
                v = view(pos);
                if (!v) continue;
            }
        }
        if (gTick % 10 == 0) drip(*v);
        if (rt.cooldown > 0) {
            --rt.cooldown;
            continue;
        }
        bool open = (stateOf(*v->block, kStateEnabled) != 0) != (stateOf(*v->block, kStatePowered) != 0);
        if (open) rt.cooldown += tryExtract(*v);
    }
    for (auto const& p : gone) gFaucets.erase(p);
}

void loadFaucets() {
    gFaucets.clear();
    auto j = game::db::load(kFaucetsKey);
    if (!j || !j->is_array()) return;
    for (auto const& e : *j) {
        if (!e.is_array() || e.size() < 4) continue;
        core::GlobalPos p{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()};
        FaucetRuntime   rt;
        if (e.size() > 4) rt.cooldown = e[4].get<int>();
        gFaucets[p] = rt;
    }
}

void saveFaucets() {
    if (!game::db::available()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (auto const& [p, rt] : gFaucets) arr.push_back({p.dim, p.x, p.y, p.z, rt.cooldown});
    game::db::save(kFaucetsKey, arr);
}

void clearFaucets() {
    gFaucets.clear();
    gTick = 0;
}

} // namespace cm::feature::jar
