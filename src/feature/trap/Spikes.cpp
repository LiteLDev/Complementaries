// Bamboo spikes: entities inside take damage and get the potion of tipped spikes.

#include "feature/trap/Trap.h"

#include <array>
#include <cmath>
#include <limits>
#include <map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectIds.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/alchemy/Potion.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::trap {

/// Set while the spikes hurt an entity: the death message hook (TrapModule.cpp) reads it.
thread_local bool gSpikeHurt = false;

namespace spikes {
namespace {

constexpr float kPotionMultiplier = 0.1f;

std::map<core::GlobalPos, Tip> gTips;
bool                           gLoaded = false;

void ensureLoaded() {
    if (gLoaded || !game::db::available()) return;
    gLoaded = true;
    std::vector<std::string> keys;
    game::db::forEachKey("cm:trap:spike:", [&keys](std::string_view suffix) { keys.emplace_back(suffix); });
    for (auto const& suffix : keys) {
        auto pos = parseKeySuffix(suffix);
        auto j   = game::db::load("cm:trap:spike:" + suffix);
        if (!pos || !j) continue;
        gTips[*pos] = Tip{j->value("potion", -1), j->value("charges", 0), 0};
    }
}

void store(core::GlobalPos const& pos, Tip const& tip) {
    gTips[pos] = tip;
    game::db::save(dataKey("spike", pos), {{"potion", tip.potion}, {"charges", tip.charges}});
}

void forget(core::GlobalPos const& pos) {
    gTips.erase(pos);
    game::db::erase(dataKey("spike", pos));
}

MobEffect const* effectById(uint id) {
    if (id == 0 || id >= static_cast<uint>(MobEffectIds::Count)) return nullptr;
    return MobEffect::mMobEffects()[static_cast<MobEffectIds>(id)].get();
}

Potion const* potionById(int id) {
    if (id < 0 || id >= 64) return nullptr;
    return Potion::mPotionsById()[static_cast<size_t>(id)].get();
}

std::vector<MobEffectInstance> const* effectsOf(int potionId) {
    auto const* p = potionById(potionId);
    return p ? &*p->mEffects : nullptr;
}

bool isHarmful(uint effectId) {
    auto const* e = effectById(effectId);
    return e && e->mIsHarmful;
}

/// The potion has a harmful effect: with `onlyAllowHarmfulEffects` its charges never run out.
bool infinite(int potionId) {
    if (!config().bambooSpikes.onlyAllowHarmfulEffects) return false;
    if (auto const* effects = effectsOf(potionId)) {
        for (auto const& e : *effects) {
            if (isHarmful(e.mId)) return true;
        }
    }
    return false;
}

std::string baseName(int potionId) {
    auto const* p = potionById(potionId);
    if (!p) return "unknown";
    std::string name = *p->mNameId;
    for (std::string_view prefix : {"long_", "strong_"}) {
        if (name.starts_with(prefix)) name = name.substr(prefix.size());
    }
    return name;
}

std::string pretty(std::string id) {
    bool up = true;
    for (auto& c : id) {
        if (c == '_') {
            c  = ' ';
            up = true;
        } else if (up) {
            c  = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            up = false;
        }
    }
    return id;
}

std::string potionName(int potionId) {
    auto base = baseName(potionId);
    return tr("cm.trap.potion." + base, pretty(base));
}

std::string roman(int amplifier) {
    static constexpr std::array<char const*, 5> numerals{"", " II", " III", " IV", " V"};
    if (amplifier >= 0 && amplifier < static_cast<int>(numerals.size())) return numerals[static_cast<size_t>(amplifier)];
    return " " + std::to_string(amplifier + 1);
}

std::vector<std::string> loreFor(Tip const& tip) {
    std::vector<std::string> lore;
    if (auto const* effects = effectsOf(tip.potion)) {
        for (auto const& e : *effects) {
            auto const* effect = effectById(e.mId);
            if (!effect) continue;
            std::string res  = *effect->mResourceName;
            std::string line = (effect->mIsHarmful ? "§c" : "§9") + tr("cm.trap.effect." + res, pretty(res)) + roman(e.mAmplifier);
            if (!effect->isInstantaneous()) {
                int ticks = static_cast<int>(static_cast<float>(e.mDuration->mValue) * kPotionMultiplier);
                int secs  = ticks / 20;
                line += " (" + std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60) + ")";
            }
            lore.push_back(line);
        }
    }
    lore.push_back(
        "§7" + (infinite(tip.potion) ? tr("cm.trap.charges_infinite", "Charges: infinite") : tr("cm.trap.charges", "Charges: {}", tip.charges))
    );
    return lore;
}

int face(Block const& block) { return stateOf(block, "minecraft:block_face", 1); }

/// Nearest player in the dimension (the "fake player" of `playerLoot`).
Player* nearestPlayer(Actor const& actor) {
    Player* best  = nullptr;
    float   bestD = std::numeric_limits<float>::max();
    actor.getLevel().forEachPlayer([&](Player& p) {
        if (p.getDimensionId() != actor.getDimensionId()) return true;
        float d = p.distanceToSqr(actor);
        if (d < bestD) {
            bestD = d;
            best  = &p;
        }
        return true;
    });
    return best;
}

void inside(BlockSource& region, BlockPos const& pos, Block const& block, Actor& actor) {
    bool up       = face(block) == 1;
    bool isPlayer = actor.isPlayer();
    if (!isPlayer) {
        // makeStuckInBlock(0.95, up ? 0.45 : 0.95, 0.95); the fall distance is untouched
        Vec3& delta = *actor.mBuiltInComponents->mStateVectorComponent->mPosDelta;
        delta.x *= 0.95f;
        delta.y *= up ? 0.45f : 0.95f;
        delta.z *= 0.95f;
    }
    if (up && isPlayer && actor.isSneaking()) return;
    float damage = actor.getFeetPos().y > static_cast<float>(pos.y) + 0.0625f + 0.001f ? 3.f : 1.5f;
    if (config().bambooSpikes.playerLoot && !isPlayer) {
        if (auto* p = nearestPlayer(actor)) actor.setLastHurtByPlayer(p);
    }
    gSpikeHurt = true;
    actor.hurtByCause(damage, SharedTypes::Legacy::ActorDamageCause::Contact);
    gSpikeHurt = false;
    if (stateOf(block, kTipState) > 0 && config().bambooSpikes.tippedSpikes && actor.isAlive()) applyTip(region, pos, actor);
}

BlockType const* gSpikesType = nullptr;

} // namespace

void onWorldReady() {
    gTips.clear();
    gLoaded     = false;
    gSpikesType = nullptr;
    ensureLoaded();
}

void onWorldClosed() {
    gTips.clear();
    gLoaded     = false;
    gSpikesType = nullptr;
}

std::optional<Tip> tipAt(core::GlobalPos const& pos) {
    ensureLoaded();
    auto it = gTips.find(pos);
    if (it == gTips.end()) return std::nullopt;
    return it->second;
}

bool isPotionValid(int potionId) {
    auto const* effects = effectsOf(potionId);
    if (!effects || effects->empty()) return false;
    if (config().bambooSpikes.onlyAllowHarmfulEffects) {
        for (auto const& e : *effects) {
            uint id = e.mId;
            if (!isHarmful(id)) return false;
            // blacklist: oozing, infested
            if (id == static_cast<uint>(MobEffectIds::Oozing) || id == static_cast<uint>(MobEffectIds::Infested)) return false;
        }
    }
    return true;
}

int tipColor(int potionId) {
    static constexpr std::array<std::array<int, 3>, 8> palette{{
        {0x4e, 0x93, 0x31}, // poison
        {0x43, 0x0a, 0x09}, // harming
        {0x5a, 0x6c, 0x81}, // slowness
        {0x48, 0x4d, 0x48}, // weakness
        {0x35, 0x2a, 0x27}, // wither
        {0xcd, 0x5c, 0xab}, // pink
        {0x3f, 0x76, 0xe4}, // blue
        {0x89, 0x32, 0xb8}, // purple
    }};
    auto const* effects = effectsOf(potionId);
    if (!effects || effects->empty()) return 8;
    auto  c     = MobEffectInstance::getAverageColorValue(*effects);
    int   best  = 8;
    float bestD = std::numeric_limits<float>::max();
    for (size_t i = 0; i < palette.size(); ++i) {
        float dr = c.r * 255.f - static_cast<float>(palette[i][0]);
        float dg = c.g * 255.f - static_cast<float>(palette[i][1]);
        float db = c.b * 255.f - static_cast<float>(palette[i][2]);
        float d  = dr * dr + dg * dg + db * db;
        if (d < bestD) {
            bestD = d;
            best  = static_cast<int>(i) + 1;
        }
    }
    return best;
}

bool tryAddPotion(BlockSource& region, BlockPos const& pos, int potionId, int charges) {
    if (!config().bambooSpikes.tippedSpikes) return false;
    auto const& block = region.getBlock(pos);
    if (!isBlock(block, kSpikesId) || stateOf(block, kTipState) != 0) return false;
    if (!isPotionValid(potionId) || charges <= 0) return false;
    auto gp = game::globalPos(region, pos);
    store(gp, Tip{potionId, std::min(charges, kMaxCharges), 0});
    setState(region, pos, kTipState, tipColor(potionId));
    playSound(region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, "fall.honey_block", 0.5f, 1.5f);
    log().info("trap: spikes at {} tipped with potion {}", gp.key(), potionId);
    return true;
}

void applyTip(BlockSource& region, BlockPos const& pos, Actor& actor) {
    ensureLoaded();
    auto gp = game::globalPos(region, pos);
    auto it = gTips.find(gp);
    if (it == gTips.end()) return;
    Tip& tip = it->second;
    if (tip.charges <= 0) return;
    uint64_t now = currentTick();
    if (tip.last != 0 && now - tip.last < 20) return;
    auto const* effects = effectsOf(tip.potion);
    if (!effects) return;
    bool used = false;
    for (auto const& e : *effects) {
        uint id = e.mId;
        if (!actor.canBeAffected(id)) continue;
        if (actor.getEffect(id)) continue;
        auto const* effect = effectById(id);
        if (!effect) continue;
        if (effect->isInstantaneous()) {
            effect->applyInstantaneousEffect(nullptr, nullptr, &actor, e.mAmplifier, 0.5f);
        } else {
            EffectDuration duration{};
            duration.mValue = static_cast<int>(static_cast<float>(e.mDuration->mValue) * kPotionMultiplier);
            MobEffectInstance instance(id, duration);
            instance.mAmplifier = e.mAmplifier;
            actor.addEffect(instance);
        }
        used = true;
    }
    if (!used) return;
    particle(region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, "minecraft:mobspell_emitter");
    if (infinite(tip.potion)) return; // Java: harmful effects never use up a charge
    tip.last = now;
    tip.charges -= 1;
    if (tip.charges <= 0) {
        forget(gp);
        setState(region, pos, kTipState, 0);
        log().info("trap: spikes at {} ran out of charges", gp.key());
    } else {
        store(gp, tip);
    }
}

ItemStack tippedItem(int potionId, int charges) {
    ItemStack item = makeItem(kTippedId);
    auto&     tag  = userDataMut(item);
    tag["CmPotion"]  = potionId;
    tag["CmCharges"] = std::clamp(charges, 0, kMaxCharges);
    item.setCustomName(Bedrock::Safety::RedactableString(
        "§r" + tr("item.supplementaries.bamboo_spikes_tipped.effect", "Bamboo Spikes of {}", potionName(potionId))
    ));
    item.setCustomLore(loreFor(Tip{potionId, std::clamp(charges, 0, kMaxCharges), 0}));
    return item;
}

std::optional<Tip> itemTip(ItemStack const& item) {
    if (itemId(item) != kTippedId) return std::nullopt;
    if (auto const* tag = userData(item); tag && tag->contains("CmPotion")) {
        Tip tip;
        tip.potion  = (*tag)["CmPotion"];
        tip.charges = tag->contains("CmCharges") ? static_cast<int>((*tag)["CmCharges"]) : kMaxCharges;
        return tip;
    }
    if (int aux = item.getAuxValue(); aux > 0) return Tip{aux, kMaxCharges, 0}; // crafted (BP recipe)
    return std::nullopt;
}

void onRemoved(BlockSource& region, BlockPos const& pos, bool drop) {
    auto gp  = game::globalPos(region, pos);
    auto tip = tipAt(gp);
    if (tip) forget(gp);
    // the tipped loot table drops nothing: drop the tipped item, or plain spikes without data
    if (drop && tip && tip->charges > 0) {
        spawnItem(region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, tippedItem(tip->potion, tip->charges));
    } else if (drop) {
        spawnItem(region, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, makeItem(kSpikesId));
    }
}

void onPlaced(BlockSource& region, BlockPos const& pos, Tip const& tip) {
    auto const& block = region.getBlock(pos);
    if (!isBlock(block, kSpikesId)) return;
    if (!config().bambooSpikes.tippedSpikes || tip.charges <= 0 || !isPotionValid(tip.potion)) return;
    auto gp = game::globalPos(region, pos);
    store(gp, Tip{tip.potion, std::min(tip.charges, kMaxCharges), 0});
    setState(region, pos, kTipState, tipColor(tip.potion));
}

void useLingeringPotion(Player& player, BlockPos const& pos) {
    auto&     region = player.getDimensionBlockSource();
    ItemStack held   = player.getSelectedItem();
    if (itemId(held) != "minecraft:lingering_potion") return;
    if (!tryAddPotion(region, pos, held.getAuxValue())) return;
    if (!isCreative(player)) swapHeld(player, makeItem("minecraft:glass_bottle"));
}

void tick(Level& level) {
    if (!config().bambooSpikes.enabled) return;
    if (!gSpikesType) {
        gSpikesType = BlockType::tryGetFromRegistry(HashedString(std::string(kSpikesId))).as_ptr();
        if (!gSpikesType) return;
    }
    for (Actor* actor : level.getRuntimeActorList()) {
        if (!actor || !actor->isAlive() || !actor->hasCategory(ActorCategory::Mob)) continue;
        if (actor->isPlayer()) {
            auto const& p = static_cast<Player const&>(*actor);
            if (isCreative(p) || p.isSpectator()) continue;
        }
        if (cage::isDisplay(*actor)) continue;
        auto&       region = actor->getDimensionBlockSource();
        AABB const& box    = actor->getAABB();
        int         x0 = static_cast<int>(std::floor(box.min.x + 1e-4f)), x1 = static_cast<int>(std::floor(box.max.x - 1e-4f));
        int         y0 = static_cast<int>(std::floor(box.min.y + 1e-4f)), y1 = static_cast<int>(std::floor(box.max.y - 1e-4f));
        int         z0 = static_cast<int>(std::floor(box.min.z + 1e-4f)), z1 = static_cast<int>(std::floor(box.max.z - 1e-4f));
        if (x1 - x0 > 4 || y1 - y0 > 4 || z1 - z0 > 4) continue; // huge boxes: not worth it
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    BlockPos    pos{x, y, z};
                    auto const& block = region.getBlock(pos);
                    if (&block.getBlockType() != gSpikesType) continue;
                    inside(region, pos, block, *actor);
                    if (!actor->isAlive()) goto next;
                }
            }
        }
    next:;
    }
}

} // namespace spikes
} // namespace cm::feature::trap
