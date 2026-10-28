// Hourglass: holds one dust item that flows from one bulb to the other; comparator output follows the
// flow; sneak + empty hand flips it.

#include <algorithm>
#include <array>
#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/instrument/Instrument.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace cm::feature::instrument::hourglass {

namespace {
struct DustDef {
    std::string_view item;
    int              index;
    int              duration;
    int              light;
};
// index = dust_page * 16 + dust; textures and light levels are defined by tools/textures/instrument.ps1
constexpr std::array kDusts{
    DustDef{"minecraft:sand", 1, 70, 0},
    DustDef{"minecraft:red_sand", 2, 70, 0},
    DustDef{"minecraft:suspicious_sand", 3, 70, 0},
    DustDef{"minecraft:sugar", 4, 40, 0},
    DustDef{"complementaries:sugar_cube", 4, 40, 0},
    DustDef{"complementaries:ash", 5, 95, 0},
    DustDef{"minecraft:gunpowder", 6, 150, 0},
    DustDef{"minecraft:glowstone_dust", 7, 190, 10},
    DustDef{"minecraft:soul_sand", 8, 211, 0},
    DustDef{"minecraft:blaze_powder", 9, 277, 6},
    DustDef{"minecraft:redstone", 10, 400, 4},
    DustDef{"minecraft:slime_ball", 11, 1750, 0},
    DustDef{"minecraft:slime", 11, 1750, 0},
    DustDef{"minecraft:honey_block", 12, 2000, 0},
    DustDef{"minecraft:white_concrete_powder", 16, 105, 0},
    DustDef{"minecraft:orange_concrete_powder", 17, 105, 0},
    DustDef{"minecraft:magenta_concrete_powder", 18, 105, 0},
    DustDef{"minecraft:light_blue_concrete_powder", 19, 105, 0},
    DustDef{"minecraft:yellow_concrete_powder", 20, 105, 0},
    DustDef{"minecraft:lime_concrete_powder", 21, 105, 0},
    DustDef{"minecraft:pink_concrete_powder", 22, 105, 0},
    DustDef{"minecraft:gray_concrete_powder", 23, 105, 0},
    DustDef{"minecraft:light_gray_concrete_powder", 24, 105, 0},
    DustDef{"minecraft:cyan_concrete_powder", 25, 105, 0},
    DustDef{"minecraft:purple_concrete_powder", 26, 105, 0},
    DustDef{"minecraft:blue_concrete_powder", 27, 105, 0},
    DustDef{"minecraft:brown_concrete_powder", 28, 105, 0},
    DustDef{"minecraft:green_concrete_powder", 29, 105, 0},
    DustDef{"minecraft:red_concrete_powder", 30, 105, 0},
    DustDef{"minecraft:black_concrete_powder", 31, 105, 0},
};

bool vertical(std::string_view face) { return face == "up" || face == "down"; }
} // namespace

std::optional<Dust> dustOf(std::string_view itemId) {
    for (auto const& d : kDusts) {
        if (d.item == itemId) return Dust{d.index, d.duration, d.light};
    }
    return std::nullopt;
}

int signalFor(Record const& rec, std::string_view face) {
    if (face == "down") return 1 + static_cast<int>((1.f - rec.progress) * 14.f);
    return 1 + static_cast<int>(rec.progress * 14.f);
}

void tick(Record& rec, BlockSource& region, BlockPos const& pos, Block const& block) {
    auto face = stringState(block, "minecraft:block_face");
    auto dust = rec.item.empty() ? std::nullopt : dustOf(rec.item);
    if (dust && dust->duration > 0) {
        float inc = 1.f / static_cast<float>(dust->duration);
        if (face == "up" && rec.progress < 1.f) {
            rec.progress = std::min(rec.progress + inc, 1.f);
            rec.dirty    = true;
        } else if (face == "down" && rec.progress > 0.f) {
            rec.progress = std::max(rec.progress - inc, 0.f);
            rec.dirty    = true;
        }
    }
    float lower = face == "down" ? 1.f - rec.progress : rec.progress;
    int   level = std::clamp(static_cast<int>(std::lround(lower * 15.f)), 0, 15);
    int   index = dust ? dust->index : 0;
    apply(
        region,
        pos,
        block,
        withStates(block, {{"complementaries:level", level}, {"complementaries:dust", index % 16}, {"complementaries:dust_page", index / 16}})
    );
    int signal = signalFor(rec, face);
    if (signal != rec.signal) {
        rec.signal = signal;
        updateComparators(region, pos);
    }
}

void insert(Record& rec, BlockSource& region, BlockPos const& pos, std::string const& item, int aux) {
    rec.item     = item;
    rec.aux      = aux;
    rec.progress = stringState(region.getBlock(pos), "minecraft:block_face") == "down" ? 1.f : 0.f;
    rec.dirty    = true;
    playSound(region, pos, "dig.sand", 1.f, 1.f);
    log().info("instrument: hourglass at {} filled with {}", rec.pos.key(), item);
}

void flip(Record& rec, BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    auto        face  = stringState(block, "minecraft:block_face");
    if (!vertical(face)) return;
    if (apply(region, pos, block, withStringState(block, "minecraft:block_face", face == "up" ? "down" : "up"))) {
        playSound(region, pos, "complementaries.block.rotate", 1.f, 1.f);
        log().info("instrument: hourglass at {} flipped", rec.pos.key());
    }
}

bool use(Record& rec, Player& player, BlockSource&, BlockPos const&, Block const& block) {
    auto const& item      = player.getSelectedItem();
    bool        handEmpty = item.isNull();
    auto        gpos      = rec.pos;
    std::int64_t key      = player.getOrCreateUniqueID().rawID;
    auto lookup = [key]() -> Player* {
        auto level = ll::service::getLevel();
        if (!level) return nullptr;
        ActorUniqueID id;
        id.rawID = key;
        return level->getPlayer(id);
    };
    if (handEmpty && player.isSneaking()) {
        if (!vertical(stringState(block, "minecraft:block_face"))) return false;
        nextTick([gpos] {
            auto* r = find(gpos);
            auto* region = regionOf(gpos.dim);
            if (r && region) flip(*r, *region, game::blockPos(gpos));
        });
        return true;
    }
    if (handEmpty) {
        if (rec.item.empty()) return false;
        nextTick([gpos, lookup] {
            auto* r = find(gpos);
            auto* p = lookup();
            auto* region = regionOf(gpos.dim);
            if (!r || !p || !region || r->item.empty()) return;
            ItemStack stack;
            stack.reinit(r->item, 1, r->aux);
            if (p->getSelectedItem().isNull()) {
                p->setSelectedItem(stack);
                p->sendInventory(false);
            } else {
                dropItem(*region, game::blockPos(gpos), r->item, 1, r->aux);
            }
            log().info("instrument: hourglass at {} emptied ({})", gpos.key(), r->item);
            r->item.clear();
            r->aux   = 0;
            r->dirty = true;
            playSound(*region, game::blockPos(gpos), "random.pop", 0.2f, 1.f);
        });
        return true;
    }
    if (player.isSneaking() || !rec.item.empty()) return false;
    auto id = item.getTypeName();
    if (!dustOf(id)) return false;
    int aux = item.getAuxValue();
    nextTick([gpos, lookup, id, aux] {
        auto* r = find(gpos);
        auto* p = lookup();
        auto* region = regionOf(gpos.dim);
        if (!r || !p || !region || !r->item.empty()) return;
        auto held = p->getSelectedItem();
        if (held.isNull() || held.getTypeName() != id) return;
        if (!p->isCreative()) {
            held.remove(1);
            if (held.mCount == 0) held = ItemStack();
            p->setSelectedItem(held);
            p->sendInventory(false);
        }
        insert(*r, *region, game::blockPos(gpos), id, aux);
    });
    return true;
}

} // namespace cm::feature::instrument::hourglass
