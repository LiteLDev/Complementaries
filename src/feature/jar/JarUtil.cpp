// U1 jar: block, item, inventory and sound helpers.

#include <algorithm>
#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/jar/Jar.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/I18n.h"

namespace cm::feature::jar {

namespace {
constexpr std::string_view kUserDataKey = "cm_jar";
} // namespace

JarConfig const& cfg() { return config::get<JarConfig>(); }

int capacity() { return std::clamp(cfg().capacity, 0, 1024); }

// ---- blocks ------------------------------------------------------------------------------------

bool isBlock(Block const& block, std::string_view id) { return block.getTypeName() == id; }

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

int stateOf(Block const& block, std::string_view state, int fallback) {
    return game::intState(block, state).value_or(fallback);
}

namespace {
CompoundTag const* statesOf(Block const& block) {
    auto const& id = *block.mSerializationId;
    if (!id.contains("states", Tag::Type::Compound)) return nullptr;
    return &id["states"].get<CompoundTag>();
}
} // namespace

int vanillaInt(Block const& block, std::string_view name, int fallback) {
    auto const* s = statesOf(block);
    if (!s || !s->contains(name)) return fallback;
    auto const& v = (*s)[name];
    if (!v.is_number_integer()) return fallback;
    return static_cast<int>(static_cast<int64>(v));
}

std::string vanillaString(Block const& block, std::string_view name) {
    auto const* s = statesOf(block);
    if (!s || !s->contains(name, Tag::Type::String)) return {};
    return std::string((*s)[name].get<StringTag>());
}

int contentOf(Block const& block) { return stateOf(block, kStateKind) * 16 + stateOf(block, kStateFluid); }

bool setStates(BlockSource& region, BlockPos const& pos, std::vector<std::pair<std::string_view, int>> const& states) {
    if (!region.hasChunksAt(pos, 0, false)) return false;
    Block const* block   = &region.getBlock(pos);
    Block const* changed = block;
    for (auto const& [name, value] : states) {
        auto const* s = changed->getBlockType().getBlockState(HashedString(std::string(name)));
        if (!s) continue;
        if (changed->getState<int>(*s) == value) continue;
        if (auto b = changed->setState(*s, value)) changed = b.as_ptr();
    }
    if (changed == block) return false;
    return game::setBlock(region, pos, *changed);
}

// ---- items ---------------------------------------------------------------------------------------

ItemStack makeItem(std::string_view id, int count, int aux) {
    ItemStack s;
    s.reinit(id, count, aux);
    return s;
}

ItemRef refOf(ItemStack const& stack) {
    if (stack.isNull()) return {};
    return {stack.getTypeName(), stack.getAuxValue()};
}

bool isItem(ItemStack const& stack, std::string_view id) { return !stack.isNull() && stack.getTypeName() == id; }

std::optional<Contents> itemContents(ItemStack const& stack) {
    if (stack.isNull() || !stack.mUserData) return std::nullopt;
    auto const& tag = *stack.mUserData;
    if (!tag.contains(kUserDataKey, Tag::Type::String)) return std::nullopt;
    auto j = nlohmann::json::parse(std::string(tag[kUserDataKey].get<StringTag>()), nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    auto c = j.get<Contents>();
    if (c.empty()) return std::nullopt;
    return c;
}

std::vector<std::string> tooltipFor(Contents const& c, Player const* viewer) {
    auto t = [&](std::string_view key, std::string_view fallback, auto const&... args) {
        return viewer ? trFor(*viewer, key, fallback, args...) : tr(key, fallback, args...);
    };
    std::vector<std::string> lines;
    if (!c.fluid.empty()) {
        auto const* def  = fluidDef(c.fluid.fluid);
        std::string name = def ? t(def->nameKey, def->nameFallback) : c.fluid.fluid;
        if (c.fluid.fluid == "potion") {
            if (auto key = potionEffectKey(c.fluid.data); !key.empty()) {
                name += " (" + t("cm.jar.effect." + std::string(key), std::string(key)) + ")";
            }
        }
        lines.push_back(t("message.supplementaries.fluid_tooltip", "{}: {} mBtl", name, c.fluid.count));
    }
    if (!c.items.empty()) {
        auto item = restoreItem(c.items.front());
        lines.push_back(t("cm.jar.cookies", "{} x{}", item.getName(), c.items.size()));
    }
    if (c.mob) lines.push_back(t("cm.jar.mob", "{}", c.mob->name));
    if (lines.empty()) {
        lines.push_back(t("message.supplementaries.jar", "Can store bowls, bottles, buckets, cookies and small mobs"));
    }    for (auto& l : lines) l = "§r§7" + l;
    return lines;
}

void setItemContents(ItemStack& stack, Contents const& c, Player const* viewer) {
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    if (c.empty()) {
        tag->erase(kUserDataKey);
    } else {
        tag->putString(std::string(kUserDataKey), nlohmann::json(c).dump());
    }
    if (tag->empty()) {
        stack.setUserData(nullptr);
    } else {
        stack.setUserData(std::move(tag));
    }
    if (c.empty()) {
        stack.clearCustomLore();
    } else {
        stack.setCustomLore(tooltipFor(c, viewer));
    }
}

StoredItem storeItem(ItemStack const& stack) {
    StoredItem s{stack.getTypeName(), stack.getAuxValue(), {}};
    if (stack.mUserData && !stack.mUserData->empty()) s.nbt = toHex(stack.mUserData->toBinaryNbt());
    return s;
}

ItemStack restoreItem(StoredItem const& item) {
    auto stack = makeItem(item.id, 1, item.aux);
    if (!item.nbt.empty() && !stack.isNull()) {
        if (auto tag = CompoundTag::fromBinaryNbt(fromHex(item.nbt))) {
            stack.setUserData(std::make_unique<CompoundTag>(std::move(*tag)));
        }
    }
    return stack;
}

void giveOrDrop(Player& player, ItemStack item) {
    if (item.isNull()) return;
    if (!player.add(item)) player.drop(item, false);
}

void swapHeld(Player& player, ItemStack const& result) {
    ItemStack held = player.getSelectedItem();
    if (player.isCreative()) {
        if (!result.isNull()) {
            auto& inv   = player.getInventory();
            bool  found = false;
            for (int i = 0; i < inv.getContainerSize() && !found; ++i) {
                auto const& s = inv.getItem(i);
                found         = !s.isNull() && s.getTypeName() == result.getTypeName() && s.getAuxValue() == result.getAuxValue();
            }
            if (!found) {
                ItemStack copy = result;
                giveOrDrop(player, copy);
            }
        }
    } else if (held.isNull() || held.mCount <= 1) {
        player.setSelectedItem(result.isNull() ? ItemStack::EMPTY_ITEM() : result);
    } else {
        held.remove(1);
        player.setSelectedItem(held);
        giveOrDrop(player, result);
    }
    player.refreshInventory();
}

void playSound(BlockSource& region, Vec3 const& pos, std::string_view name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& p           = static_cast<PlaySoundPacket&>(*packet);
    p.mName           = std::string(name);
    p.mPos            = BlockPos{
        static_cast<int>(std::floor(pos.x * 8.f)),
        static_cast<int>(std::floor(pos.y * 8.f)),
        static_cast<int>(std::floor(pos.z * 8.f)),
    };
    p.mVolume = volume;
    p.mPitch  = pitch;
    p.sendTo(BlockPos{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))},
             region.getDimensionId());
}

} // namespace cm::feature::jar
