// Dyeing blocks in a cauldron of dyed water (docs/spec/tweak.md §3.2).

#include <array>
#include <climits>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/CauldronBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/tweak/Tweak.h"
#include "feature/tweak/TweakConfig.h"
#include "feature/tweak/Util.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::tweak {

namespace {
struct Dye {
    char const* name;
    int         rgb;
};
// Bedrock order (= bed aux values), with the dye colours the game mixes in cauldrons.
constexpr std::array<Dye, 16> kDyes = {{
    {"white", 0xF9FFFE},
    {"orange", 0xF9801D},
    {"magenta", 0xC74EBD},
    {"light_blue", 0x3AB3DA},
    {"yellow", 0xFED83D},
    {"lime", 0x80C71F},
    {"pink", 0xF38BAA},
    {"gray", 0x474F52},
    {"light_gray", 0x9D9D97},
    {"cyan", 0x169C9C},
    {"purple", 0x8932B8},
    {"blue", 0x3C44AA},
    {"brown", 0x835432},
    {"green", 0x5E7C16},
    {"red", 0xB02E26},
    {"black", 0x1D1D21},
}};

constexpr char const* kFamilies[] = {
    "_stained_glass_pane",
    "_stained_glass",
    "_concrete_powder",
    "_concrete",
    "_terracotta",
    "_carpet",
    "_candle",
    "_wool",
};

/// The colour prefix of a "minecraft:<color>_<family>" id (-1 when none).
int colorOf(std::string_view name) {
    int best = -1;
    std::size_t bestLen = 0;
    for (int i = 0; i < 16; ++i) {
        std::string_view n = kDyes[i].name;
        if (name.starts_with(n) && name.size() > n.size() && name[n.size()] == '_' && n.size() > bestLen) {
            best    = i;
            bestLen = n.size();
        }
    }
    return best;
}

constexpr int kFullBottle = 2; // one glass bottle = 2 of Bedrock's 6 fill levels
} // namespace

int nearestDye(int argb) {
    int r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    int best = 0, bestDist = INT_MAX;
    for (int i = 0; i < 16; ++i) {
        int dr = r - ((kDyes[i].rgb >> 16) & 0xFF), dg = g - ((kDyes[i].rgb >> 8) & 0xFF), db = b - (kDyes[i].rgb & 0xFF);
        int d = dr * dr + dg * dg + db * db;
        if (d < bestDist) {
            bestDist = d;
            best     = i;
        }
    }
    return best;
}

std::string recolor(std::string const& itemId, int dye, int& aux) {
    if (dye < 0 || dye > 15) return {};
    std::string const prefix = "minecraft:";
    if (!itemId.starts_with(prefix)) return {};
    std::string_view name = std::string_view(itemId).substr(prefix.size());
    if (name == "bed") {
        aux = dye;
        return itemId;
    }
    // undyed bases
    if (name == "glass") return prefix + kDyes[dye].name + "_stained_glass";
    if (name == "glass_pane") return prefix + kDyes[dye].name + "_stained_glass_pane";
    if (name == "hardened_clay") return prefix + kDyes[dye].name + "_terracotta";
    if (name == "candle") return prefix + kDyes[dye].name + "_candle";
    int color = colorOf(name);
    if (color < 0) return {};
    std::string_view rest = name.substr(std::string_view(kDyes[color].name).size());
    for (auto const* family : kFamilies) {
        if (rest == family) return prefix + kDyes[dye].name + std::string(family);
    }
    return {};
}

bool cauldronUseOn(UseOn const& use) {
    if (!config().cauldronDyeing || blockName(use.block) != "minecraft:cauldron" || use.itemId.empty()) return false;
    int aux = use.item.getAuxValue();
    if (recolor(use.itemId, 0, aux).empty()) return false;
    auto& region = use.player.getDimensionBlockSource();
    auto* actor  = region.getBlockEntity(use.pos);
    if (!actor) return false;
    auto* cauldron = static_cast<CauldronBlockActor*>(actor);
    if (!cauldron->mHasCustomColor || cauldron->mPotionId != -1) return false;
    if (game::intState(use.block, "fill_level").value_or(0) <= 0 || game::intState(use.block, "cauldron_liquid").value_or(1) != 0) {
        return false;
    }
    auto playerId = use.player.getOrCreateUniqueID();
    int  dim      = static_cast<int>(region.getDimensionId());
    auto pos      = use.pos;
    auto item     = use.itemId;
    defer([=] {
        auto* player = findPlayer(playerId);
        auto* r      = regionOf(dim);
        if (!player || !r) return;
        ItemStack held = player->getSelectedItem();
        if (itemId(held) != item) return;
        Block const& b = r->getBlock(pos);
        auto*        a = r->getBlockEntity(pos);
        if (blockName(b) != "minecraft:cauldron" || !a) return;
        auto* c     = static_cast<CauldronBlockActor*>(a);
        int   level = game::intState(b, "fill_level").value_or(0);
        if (!c->mHasCustomColor || level <= 0) return;
        int  dye    = nearestDye(c->mCustomColor);
        int  newAux = held.getAuxValue();
        auto newId  = recolor(item, dye, newAux);
        if (newId.empty()) return;
        if (newId != item || newAux != held.getAuxValue()) {
            setSelected(*player, makeItem(newId, held.mCount, newAux));
        }
        int next = std::max(0, level - kFullBottle);
        if (auto const* nb = withState(b, "fill_level", next)) game::setBlock(*r, pos, *nb);
        if (next == 0) c->mHasCustomColor = false;
        playSound(*r, Sound::Splash, Vec3{pos.x + 0.5f, pos.y + 0.8f, pos.z + 0.5f});
        log().info("tweak: {} dyed {} {} in a cauldron", player->getRealName(), static_cast<int>(held.mCount), newId);
    });
    return true;
}

} // namespace cm::feature::tweak
