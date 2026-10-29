// Quiver and lunch basket (selectable container items) and the lore of every storage item.

#include <algorithm>

#include "ll/api/service/Bedrock.h"

#include "mc/util/Random.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/IFoodItemComponent.h"
#include "mc/world/level/Level.h"

#include "feature/storage/Features.h"
#include "feature/storage/View.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

CM_REGISTER_CONFIG("storage", cm::feature::storage::StorageConfig);

namespace cm::feature::storage {

StorageConfig const& config() { return cm::config::get<StorageConfig>(); }
int                  sackSlots() { return std::clamp(config().sackSlots, 1, 27); }
int                  quiverSlots() { return std::clamp(config().quiverSlots, 1, 9); }
int                  basketSlots() { return std::clamp(config().lunchBasketSlots, 1, 9); }

namespace {

constexpr char const* kProxyKey = "proxy";
constexpr char const* kProxySlot = "pslot";

int slotsOf(std::string_view id) {
    if (id == kQuiver) return quiverSlots();
    if (id == kLunchBasket) return basketSlots();
    if (id == kSack) return sackSlots();
    return 1;
}

std::string proxyOf(ItemStack const& s) { return isEmpty(s) ? std::string() : tagString(s, kProxyKey); }

/// Removes the proxy marker (and an emptied user data) from an arrow stack.
void stripProxy(ItemStack& s) {
    if (!s.mUserData) return;
    if (s.mUserData->contains("cm_storage", Tag::Type::Compound)) {
        auto& t = (*s.mUserData)["cm_storage"].get<CompoundTag>();
        t.erase(kProxyKey);
        t.erase(kProxySlot);
        if (t.empty()) s.mUserData->erase("cm_storage");
    }
    if (s.mUserData->empty()) s.mUserData.reset();
}

/// Merges `s` into `items` (preferred slot first). Returns what did not fit.
ItemStack mergeInto(std::vector<ItemStack>& items, ItemStack s, int preferred) {
    auto tryAt = [&](int i) {
        if (i < 0 || i >= static_cast<int>(items.size()) || isEmpty(s)) return;
        auto& t = items[i];
        if (isEmpty(t)) {
            t = s;
            s = ItemStack();
        } else if (t.isStackable(s)) {
            int room = std::max(0, static_cast<int>(t.getMaxStackSize()) - countOf(t));
            int move = std::min(room, countOf(s));
            if (move > 0) {
                t.add(move);
                s.remove(move);
                if (s.mCount == 0) s = ItemStack();
            }
        }
    };
    tryAt(preferred);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) tryAt(i);
    return s;
}

void showSelection(Player& player, std::string_view id, std::vector<ItemStack> const& items, int sel) {
    bool quiver = id == kQuiver;
    if (sel < 0) {
        actionbar(
            player,
            quiver ? trFor(player, "message.supplementaries.quiver.empty", "The quiver is empty")
                   : trFor(player, "message.supplementaries.lunch_basket.empty", "The basket is empty")
        );
        return;
    }
    auto what = stackName(items[sel]) + " x" + std::to_string(countOf(items[sel]));
    actionbar(
        player,
        quiver ? trFor(player, "message.supplementaries.quiver.selected", "Selected: {}", what)
               : trFor(player, "message.supplementaries.lunch_basket.selected", "Selected: {}", what)
    );
}

} // namespace

// ---- predicates ------------------------------------------------------------------------------------

bool isArrow(ItemStack const& s) { return itemId(s) == "minecraft:arrow"; }

bool isFood(ItemStack const& s) {
    if (isEmpty(s)) return false;
    auto const* item = s.mItem.get();
    return item && (item->isFood() || item->getFood() != nullptr) && itemId(s) != kLunchBasket;
}

bool isRangedWeapon(ItemStack const& s) {
    auto id = itemId(s);
    return id == "minecraft:bow" || id == "minecraft:crossbow";
}

int findItem(Player& player, std::string_view id) {
    int found = -2;
    // the selected hotbar slot first, then the inventory in order, then the off-hand
    if (itemId(player.getSelectedItem()) == id) return player.getSelectedItemSlot();
    forEachSlot(player, [&](int slot, ItemStack const& s) {
        if (itemId(s) != id) return false;
        found = slot;
        return true;
    });
    return found;
}

int selectedSlot(std::vector<ItemStack> const& items, int sel) {
    int n = static_cast<int>(items.size());
    if (n == 0) return -1;
    sel = std::clamp(sel, 0, n - 1);
    for (int k = 0; k < n; ++k) {
        int i = (sel + k) % n;
        if (!isEmpty(items[i])) return i;
    }
    return -1;
}

// ---- lore ------------------------------------------------------------------------------------------

void updateLore(ItemStack& stack, Player* owner) {
    auto id  = itemId(stack);
    auto uid = itemUid(stack);
    std::vector<std::string> lore;
    Kind                     kind = kindOf(id);
    if ((kind == Kind::Present || kind == Kind::TrappedPresent) && tagInt(stack, "packed", 0) != 0) {
        auto recipient = tagString(stack, "recipient");
        auto sender    = tagString(stack, "sender");
        auto desc      = tagString(stack, "description");
        if (kind == Kind::Present) {
            lore.push_back(
                "§r§7"
                + (recipient.empty() ? tr("message.supplementaries.present.public", "Public")
                                     : tr("message.supplementaries.present.to", "To: {}", recipient))
            );
        }
        if (!sender.empty()) lore.push_back("§r§7" + tr("message.supplementaries.present.from", "From {}", sender));
        if (!desc.empty()) lore.push_back("§r§o§7" + desc);
    } else if (!uid.empty()) {
        auto items = loadContents(uid, slotsOf(id));
        int  sel   = -1;
        if (id == kQuiver || id == kLunchBasket) {
            int want = tagInt(stack, "sel", 0);
            // arrows lent to the off-hand still belong to the quiver
            if (owner && id == kQuiver) {
                auto const& off = owner->getOffhandSlot();
                if (proxyOf(off) == uid) {
                    int ps = std::clamp(tagInt(off, kProxySlot, 0), 0, static_cast<int>(items.size()) - 1);
                    if (isEmpty(items[ps])) items[ps] = off;
                    want = ps;
                }
            }
            sel = selectedSlot(items, want);
        }
        lore = contentLore(items, sel);
    }
    if (lore.empty()) {
        if (!stack.getCustomLore().empty()) stack.clearCustomLore();
    } else {
        stack.setCustomLore(lore);
    }
}

void refreshItemLore(Player& player, std::string const& uid) {
    if (uid.empty()) return;
    bool changed = false;
    forEachSlot(player, [&](int slot, ItemStack const& s) {
        if (itemUid(s) != uid || !proxyOf(s).empty()) return false;
        ItemStack copy = s;
        updateLore(copy, &player);
        setSlotItem(player, slot, copy);
        changed = true;
        return false;
    });
    if (changed) player.sendInventory(false);
}

// ---- selection and screens -------------------------------------------------------------------------

void cycleSelection(Player& player, int slot) {
    ItemStack item = slotItem(player, slot);
    auto      id   = itemId(item);
    if (id != kQuiver && id != kLunchBasket) return;
    if (id == kQuiver) {
        returnProxies(player);
        item = slotItem(player, slot);
    }
    auto uid   = ensureUid(item);
    auto items = loadContents(uid, slotsOf(id));
    int  cur   = selectedSlot(items, tagInt(item, "sel", 0));
    int  next  = cur < 0 ? -1 : selectedSlot(items, cur + 1);
    if (next >= 0) setTagInt(item, "sel", next);
    updateLore(item, &player);
    setSlotItem(player, slot, item);
    player.sendInventory(false);
    showSelection(player, id, items, next);
    log().info("storage: {} selected slot {} of the {}", player.getRealName(), next, id);
}

bool openItemScreen(Player& player, int slot) {
    ItemStack item = slotItem(player, slot);
    auto      id   = itemId(item);
    if (id != kQuiver && id != kLunchBasket) return false;
    if (id == kQuiver) {
        returnProxies(player);
        item = slotItem(player, slot);
    }
    bool hadUid = !itemUid(item).empty();
    auto uid    = ensureUid(item);
    if (!hadUid) {
        setSlotItem(player, slot, item);
        player.sendInventory(false);
    }
    view::Options o;
    o.uid      = uid;
    o.size     = slotsOf(id);
    o.title    = id == kQuiver ? trFor(player, "item.supplementaries.quiver", "Quiver")
                               : trFor(player, "item.supplementaries.lunch_basket", "Lunch Basket");
    o.allowed  = id == kQuiver ? std::function<bool(ItemStack const&)>(isArrow) : std::function<bool(ItemStack const&)>(isFood);
    long long key = playerKey(player);
    o.onChange = [key, uid] {
        if (auto* p = playerById(key)) refreshItemLore(*p, uid);
    };
    if (!view::open(player, std::move(o))) return false;
    log().info("storage: {} opened the {} screen", player.getRealName(), id);
    return true;
}

// ---- quiver ----------------------------------------------------------------------------------------

void returnProxies(Player& player) {
    bool changed = false;
    forEachSlot(player, [&](int slot, ItemStack const& s) {
        auto quid = proxyOf(s);
        if (quid.empty()) return false;
        ItemStack arrows = s;
        int       pslot  = tagInt(arrows, kProxySlot, 0);
        stripProxy(arrows);
        // the quiver it came from, if the player still has it
        bool found = false;
        forEachSlot(player, [&](int, ItemStack const& q) {
            if (itemId(q) == kQuiver && itemUid(q) == quid) found = true;
            return found;
        });
        if (found) {
            auto items = loadContents(quid, quiverSlots());
            arrows     = mergeInto(items, arrows, pslot);
            saveContents(quid, items);
        }
        setSlotItem(player, slot, arrows);
        changed = true;
        if (found) refreshItemLore(player, quid);
        return false;
    });
    if (changed) player.sendInventory(false);
}

void quiverTick(Player& player, bool fullScan) {
    ItemStack const& main    = player.getSelectedItem();
    bool             wantBow = isRangedWeapon(main);
    if (!wantBow && !fullScan && proxyOf(player.getOffhandSlot()).empty()) return; // nothing lent, no bow
    int              qslot   = wantBow ? findItem(player, kQuiver) : -2;
    std::string      quid    = qslot >= -1 ? itemUid(slotItem(player, qslot)) : std::string();
    int              qsel    = qslot >= -1 ? tagInt(slotItem(player, qslot), "sel", 0) : 0;

    // stray or outdated lent stacks go back
    bool stray = false;
    forEachSlot(player, [&](int slot, ItemStack const& s) {
        auto p = proxyOf(s);
        if (p.empty()) return false;
        bool active = slot == -1 && wantBow && !quid.empty() && p == quid && tagInt(s, kProxySlot, -1) == qsel;
        if (!active) stray = true;
        return stray;
    });
    if (stray) {
        // keep the active one in the off-hand: return everything, it is lent again below
        returnProxies(player);
    }
    if (!wantBow || quid.empty() || qslot == -1) return;
    if (!isEmpty(player.getOffhandSlot())) return;
    auto items = loadContents(quid, quiverSlots());
    int  sel   = selectedSlot(items, qsel);
    if (sel < 0) return;
    ItemStack lent = items[sel];
    items[sel]     = ItemStack();
    setTagString(lent, kProxyKey, quid);
    setTagInt(lent, kProxySlot, sel);
    saveContents(quid, items);
    if (sel != qsel) {
        ItemStack q = slotItem(player, qslot);
        setTagInt(q, "sel", sel);
        setSlotItem(player, qslot, q);
    }
    player.setOffhandSlot(lent);
    refreshItemLore(player, quid);
    player.sendInventory(false);
}

ItemStack skeletonQuiver() {
    auto  level = ll::service::getLevel();
    auto  q     = makeStack(kQuiver);
    auto  uid   = ensureUid(q);
    std::vector<ItemStack> items(static_cast<size_t>(quiverSlots()));
    static constexpr int   kTipped[] = {15, 18, 22, 24, 26, 35}; // swiftness, slowness, healing, harming, poison, weakness
    int                    plain     = level ? 4 + level->getRandom().nextInt(9) : 8;
    items[0]                         = makeStack("minecraft:arrow", plain);
    if (items.size() > 1 && level) {
        int aux  = kTipped[level->getRandom().nextInt(static_cast<int>(std::size(kTipped)))];
        items[1] = makeStack("minecraft:arrow", 1 + level->getRandom().nextInt(4), aux);
    }
    saveContents(uid, items);
    updateLore(q);
    return q;
}

// ---- lunch basket ----------------------------------------------------------------------------------

bool basketCanEat(Player& player, int slot) {
    ItemStack const& item  = slotItem(player, slot);
    auto             items = loadContents(itemUid(item), basketSlots());
    int              sel   = selectedSlot(items, tagInt(item, "sel", 0));
    if (sel < 0) return false;
    auto const* food   = items[sel].mItem.get();
    bool        always = food && food->getFood() && food->getFood()->canAlwaysEat();
    return player.isHungry() || always || player.isCreative();
}

bool basketEat(Player& player, ItemStack& basket) {
    auto level = ll::service::getLevel();
    auto uid   = itemUid(basket);
    if (!level || uid.empty()) return false;
    auto items = loadContents(uid, basketSlots());
    int  sel   = selectedSlot(items, tagInt(basket, "sel", 0));
    if (sel < 0) return false;
    ItemStack one = items[sel];
    one.set(1);
    auto const* food   = one.mItem.get();
    auto        foodId = itemId(one);
    if (!food) return false;
    food->useTimeDepleted(one, level.as_ptr(), &player);
    items[sel].remove(1);
    if (items[sel].mCount == 0) items[sel] = ItemStack();
    saveContents(uid, items);
    // a container remainder (bowl, bottle) goes to the inventory
    if (!isEmpty(one) && itemId(one) != foodId) giveOrDrop(player, one);
    int next = selectedSlot(items, sel);
    if (next >= 0) setTagInt(basket, "sel", next);
    updateLore(basket, &player);
    log().info("storage: {} ate from the lunch basket", player.getRealName());
    return true;
}

} // namespace cm::feature::storage
