// Jar boat: decorative; generated in shipwreck treasure chests.

#include "ll/api/memory/Hook.h"

#include "mc/util/Random.h"
#include "mc/world/Container.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/storage/loot/LootTable.h"

#include "feature/tableware/Tableware.h"

namespace cm::feature::tableware::jar_boat {

namespace {
LL_TYPE_INSTANCE_HOOK(
    JarBoatLootHook,
    ll::memory::HookPriority::Normal,
    LootTable,
    &LootTable::fill,
    void,
    Container&        container,
    Random&           random,
    LootTableContext& context
) {
    origin(container, random, context);
    if (this->mDir->find("shipwreck_treasure") == std::string::npos) return;
    if (random.nextFloat() >= config().jarBoatShipwreckChance) return;
    container.addItemToFirstEmptySlot(makeItem(kJarBoat));
}
} // namespace

void installHooks() { JarBoatLootHook::hook(); }
void uninstallHooks() { JarBoatLootHook::unhook(); }

} // namespace cm::feature::tableware::jar_boat
