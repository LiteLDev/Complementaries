// The Flag loom: the Bedrock stand-in for flags in the loom and the `flag_from_banner` recipe.

#include "feature/flag/Flag.h"

#include <memory>

#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::flag {

namespace {

/// The flag in the main hand, if any (copy).
std::optional<ItemStack> heldFlag(Player& player) {
    auto const& item = player.getSelectedItem();
    if (item.isNull() || !isFlag(item.getTypeName())) return std::nullopt;
    return ItemStack(item);
}

/// Writes `item` back to the main hand if the hand still holds the same kind of flag.
bool storeHeld(Player& player, ItemStack const& before, ItemStack const& after) {
    auto const& now = player.getSelectedItem();
    if (now.isNull() || now.getTypeName() != before.getTypeName() || readLayers(now) != readLayers(before))
        return false;
    if (now.mCount > 1) {
        // like the loom: one flag of the stack gets the patterns
        ItemStack rest(now);
        rest.remove(1);
        ItemStack one(after);
        one.set(1);
        player.setSelectedItem(rest);
        if (!player.add(one)) player.drop(one, false);
    } else {
        ItemStack one(after);
        one.set(1);
        player.setSelectedItem(one);
    }
    player.refreshInventory();
    return true;
}

bool hasItem(Player& player, std::string_view id) {
    if (id.empty()) return true;
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& it = inv.getItem(i);
        if (!it.isNull() && it.getTypeName() == id) return true;
    }
    return false;
}

int dyeCount(Player& player, int color) {
    int   n   = 0;
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& it = inv.getItem(i);
        if (!it.isNull() && dyeColor(it.getTypeName()) == color) n += it.mCount;
    }
    return n;
}

bool takeDye(Player& player, int color) {
    if (player.isCreative()) return true;
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& it = inv.getItem(i);
        if (!it.isNull() && dyeColor(it.getTypeName()) == color) {
            inv.removeItem(i, 1);
            return true;
        }
    }
    return false;
}

void openColors(Player& player, std::string pattern);
void openCopy(Player& player);

} // namespace

bool addLayer(ItemStack& item, Layer const& layer, Player const* player) {
    auto layers = readLayers(item);
    if (static_cast<int>(layers.size()) >= kMaxLayers) return false;
    layers.push_back(layer);
    writeLayers(item, layers, player);
    return true;
}

bool copyLayers(ItemStackBase const& from, ItemStack& to, Player const* player) {
    int fromColor = bannerOrFlagColor(from);
    int toColor   = bannerOrFlagColor(to);
    if (fromColor < 0 || fromColor != toColor) return false;
    bool fromBanner = from.getTypeName() == "minecraft:banner";
    bool toBanner   = to.getTypeName() == "minecraft:banner";
    if (fromBanner && toBanner) return false; // banner -> banner is vanilla duplication
    auto layers = readLayers(from);
    if (layers.empty() || static_cast<int>(layers.size()) > kMaxLayers || !readLayers(to).empty()) return false;
    writeLayers(to, layers, player);
    return true;
}

void openLoom(Player& player) {
    auto held = heldFlag(player);
    if (!held) return;
    auto const& cfg    = config::get<FlagConfig>();
    auto        layers = readLayers(*held);

    std::string content;
    for (auto const& l : layers) content += layerName(l.pattern, l.color, &player) + "\n";
    if (content.empty()) content = trFor(player, "cm.flag.loom.empty", "This flag has no patterns yet.");

    auto                 actions = std::make_shared<std::vector<std::function<void(Player&)>>>();
    ll::form::SimpleForm form(trFor(player, "cm.flag.loom.title", "Flag Loom"), content);

    if (layers.empty()) { // like the recipe: only a blank flag takes a banner's patterns
        form.appendButton(trFor(player, "cm.flag.loom.copy", "Copy patterns from a banner"));
        actions->push_back([](Player& p) { openCopy(p); });
    }
    if (!layers.empty()) {
        form.appendButton(trFor(player, "cm.flag.loom.remove", "Remove last pattern"));
        actions->push_back([](Player& p) {
            auto before = heldFlag(p);
            if (!before) return;
            auto after = *before;
            auto l     = readLayers(after);
            if (l.empty()) return;
            l.pop_back();
            writeLayers(after, l, &p);
            storeHeld(p, *before, after);
            log().info("flag: {} removed a flag pattern", p.getRealName());
        });
    }
    for (auto code : kPatternCodes) {
        if (code == "dragon" && !cfg.dragonPattern) continue;
        auto label = trFor(player, "cm.flag.pattern." + std::string(code), patternEnglish(code));
        if (auto need = patternItem(code); !need.empty() && !hasItem(player, need) && !player.isCreative())
            label = "§8" + label;
        form.appendButton(label);
        actions->push_back([code = std::string(code)](Player& p) { openColors(p, code); });
    }
    log().info("flag: opened the flag loom for {}", player.getRealName());
    form.sendTo(player, [actions](Player& p, int selected, ll::form::FormCancelReason) {
        if (selected < 0 || selected >= static_cast<int>(actions->size())) return;
        (*actions)[selected](p);
    });
}

namespace {

void openColors(Player& player, std::string pattern) {
    auto held = heldFlag(player);
    if (!held) return;
    if (static_cast<int>(readLayers(*held).size()) >= kMaxLayers) {
        player.sendMessage(trFor(player, "cm.flag.loom.full", "This flag already has 6 patterns"));
        return;
    }
    if (auto need = patternItem(pattern); !need.empty() && !hasItem(player, need) && !player.isCreative()) {
        auto name = trFor(player, "cm.flag.pattern." + pattern, patternEnglish(pattern));
        player.sendMessage(trFor(player, "cm.flag.loom.need_pattern", "You need the {} banner pattern", name));
        return;
    }
    auto                 colors = std::make_shared<std::vector<int>>();
    ll::form::SimpleForm form(
        trFor(player, "cm.flag.loom.title", "Flag Loom"),
        trFor(
            player,
            "cm.flag.loom.pick_color",
            "Pick a dye colour for {}",
            trFor(player, "cm.flag.pattern." + pattern, patternEnglish(pattern))
        )
    );
    for (int c = 0; c < kColors; ++c) {
        int n = dyeCount(player, c);
        if (n <= 0 && !player.isCreative()) continue;
        form.appendButton(trFor(player, "cm.flag.loom.color", "{} ({} dye)", colorName(c, &player), n));
        colors->push_back(c);
    }
    if (colors->empty()) {
        player.sendMessage(trFor(player, "cm.flag.loom.no_dye", "You need dye to add a pattern"));
        return;
    }
    form.sendTo(player, [colors, pattern, before = *held](Player& p, int selected, ll::form::FormCancelReason) {
        if (selected < 0 || selected >= static_cast<int>(colors->size())) return;
        int  color = (*colors)[selected];
        auto after = before;
        if (!addLayer(after, {pattern, color}, &p)) return;
        if (dyeCount(p, color) <= 0 && !p.isCreative()) {
            p.sendMessage(trFor(p, "cm.flag.loom.need_dye", "You need {} dye", colorName(color, &p)));
            return;
        }
        if (!storeHeld(p, before, after)) return;
        takeDye(p, color);
        p.refreshInventory();
        log().info("flag: {} added pattern {} {} to a flag", p.getRealName(), pattern, kColorNames[color]);
    });
}

void openCopy(Player& player) {
    auto held = heldFlag(player);
    if (!held) return;
    int                  color = bannerOrFlagColor(*held);
    auto                 slots = std::make_shared<std::vector<int>>();
    auto&                inv   = player.getInventory();
    ll::form::SimpleForm form(
        trFor(player, "cm.flag.loom.title", "Flag Loom"),
        trFor(player, "cm.flag.loom.copy_hint", "Pick a banner or flag of the same colour. It is kept.")
    );
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (i == player.getSelectedItemSlot()) continue;
        auto const& it = inv.getItem(i);
        if (it.isNull() || bannerOrFlagColor(it) != color) continue;
        auto layers = readLayers(it);
        if (layers.empty()) continue;
        std::string label = it.getCustomName().empty() ? it.getName() : it.getCustomName();
        for (auto const& l : layers) label += "\n§7" + layerName(l.pattern, l.color, &player);
        form.appendButton(label);
        slots->push_back(i);
        if (slots->size() >= 20) break;
    }
    if (slots->empty()) {
        player.sendMessage(
            trFor(player, "cm.flag.loom.no_banner", "No banner with the same colour and patterns found")
        );
        return;
    }
    form.sendTo(player, [slots, before = *held](Player& p, int selected, ll::form::FormCancelReason) {
        if (selected < 0 || selected >= static_cast<int>(slots->size())) return;
        auto const& src   = p.getInventory().getItem((*slots)[selected]);
        auto        after = before;
        if (!copyLayers(src, after, &p)) {
            p.sendMessage(trFor(p, "cm.flag.loom.no_banner", "No banner with the same colour and patterns found"));
            return;
        }
        if (storeHeld(p, before, after)) log().info("flag: {} copied banner patterns onto a flag", p.getRealName());
    });
}

} // namespace

} // namespace cm::feature::flag
