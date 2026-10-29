// U1 jar: dispensers facing a jar fill it from (or empty it into) the containers they hold, put
// cookies and fish buckets in, like the Supplementaries dispenser behaviours.

#include <random>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/Container.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/DispenserBlock.h"

#include "feature/jar/Jar.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::feature::jar {

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

/// Puts `result` back: in the emptied slot, else merged / in a free slot, else dropped in front.
void giveBack(BlockSource& region, ::Container& cont, int slot, ItemStack const& result, Vec3 const& front) {
    if (result.isNull()) return;
    if (cont.getItem(slot).isNull()) {
        cont.setItem(slot, result);
        return;
    }
    for (int i = 0; i < cont.getContainerSize(); ++i) {
        auto const& s = cont.getItem(i);
        if (s.isNull()) {
            cont.setItem(i, result);
            return;
        }
        if (s.getTypeName() == result.getTypeName() && s.getAuxValue() == result.getAuxValue() && !s.mUserData
            && !result.mUserData && s.mCount + result.mCount <= s.getMaxStackSize()) {
            ItemStack merged = s;
            merged.add(result.mCount);
            cont.setItem(i, merged);
            return;
        }
    }
    if (auto level = ll::service::getLevel()) level->getSpawner().spawnItem(region, result, nullptr, front, 0);
}

void useOne(::Container& cont, int slot) {
    ItemStack s = cont.getItem(slot);
    s.remove(1);
    cont.setItem(slot, s.mCount > 0 ? s : ItemStack::EMPTY_ITEM());
}

/// One dispense into the jar at `jarPos`. Returns true when the jar used the item.
bool dispenseIntoJar(
    BlockSource&    region,
    BlockPos const& dispenser,
    BlockPos const& jarBp,
    ::Container&    cont,
    int             slot
) {
    auto const& conf = cfg();
    auto        pos  = game::globalPos(region, jarBp);
    auto        c    = jarAt(pos);
    ItemStack   item = cont.getItem(slot);
    Vec3        front{
        static_cast<float>(jarBp.x) + 0.5f,
        static_cast<float>(jarBp.y) + 0.5f,
        static_cast<float>(jarBp.z) + 0.5f
    };
    (void)dispenser;

    if (conf.jarLiquids && c.items.empty() && !c.mob) {
        if (item.getAuxValue() == 0) {
            FluidStack tank = c.fluid;
            if (auto filled = fillItem(tank, item.getTypeName())) {
                c.fluid = tank;
                setJar(pos, c);
                useOne(cont, slot);
                giveBack(region, cont, slot, makeItem(filled->id, 1, filled->aux), front);
                playSound(region, front, "bottle.fill");
                return true;
            }
        }
        if (auto d = fluidFromItem(refOf(item))) {
            Tank t{c.fluid, capacity()};
            if (t.add(d->fluid, true) == d->fluid.count) {
                t.add(d->fluid, false);
                c.fluid = t.fluid;
                setJar(pos, c);
                useOne(cont, slot);
                if (!d->emptyId.empty()) giveBack(region, cont, slot, makeItem(d->emptyId), front);
                playSound(region, front, "bottle.empty");
                return true;
            }
        }
    }
    if (conf.jarCookies && c.fluid.empty() && !c.mob && c.items.size() < static_cast<size_t>(kMaxCookies)) {
        for (auto const& id : conf.cookieItems) {
            if (id != item.getTypeName()) continue;
            ItemStack one = item;
            one.set(1);
            c.items.push_back(storeItem(one));
            setJar(pos, c);
            useOne(cont, slot);
            playSound(region, front, "random.pop");
            return true;
        }
    }
    if (c.items.empty() && c.fluid.empty() && !c.mob) {
        if (auto m = mobFromBucket(item)) {
            c.mob = *m;
            setJar(pos, c);
            useOne(cont, slot);
            giveBack(region, cont, slot, makeItem("minecraft:bucket"), front);
            playSound(region, front, "bucket.empty_fish");
            return true;
        }
    }
    return false;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    JarDispenseHook,
    ll::memory::HookPriority::Normal,
    DispenserBlock,
    &DispenserBlock::$dispenseFrom,
    void,
    ::BlockSource&    region,
    ::BlockPos const& pos
) {
    auto const& conf = cfg();
    if (conf.enabled && conf.dispensers && !region.getLevel().isClientSide()) {
        auto const& self = region.getBlock(pos);
        if (self.getTypeName() == "minecraft:dispenser") {
            static constexpr int d[6][3] = {
                {0,  -1, 0 },
                {0,  1,  0 },
                {0,  0,  -1},
                {0,  0,  1 },
                {-1, 0,  0 },
                {1,  0,  0 }
            };
            int      face = vanillaInt(self, "facing_direction", 0);
            BlockPos target{pos.x + d[face % 6][0], pos.y + d[face % 6][1], pos.z + d[face % 6][2]};
            if (isBlock(region.getBlock(target), kJarId)) {
                if (auto cont = region.tryGetContainer(pos)) {
                    std::vector<int> slots;
                    for (int i = 0; i < cont->getContainerSize(); ++i) {
                        if (!cont->getItem(i).isNull()) slots.push_back(i);
                    }
                    if (!slots.empty()) {
                        int slot = slots[rng()() % slots.size()];
                        if (dispenseIntoJar(region, pos, target, *cont, slot)) return;
                    }
                }
            }
        }
    }
    origin(region, pos);
}

class JarDispenserModule {
public:
    void install() { JarDispenseHook::hook(); }
    void uninstall() { JarDispenseHook::unhook(); }
};

} // namespace cm::feature::jar

CM_REGISTER_MODULE(cm::feature::jar::JarDispenserModule);
