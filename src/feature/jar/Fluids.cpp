#include "feature/jar/Fluids.h"

#include <algorithm>
#include <cmath>

#include "nlohmann/json.hpp"

namespace cm::feature::jar {

namespace {

constexpr std::string_view kPotion    = "minecraft:potion";
constexpr std::string_view kSplash    = "minecraft:splash_potion";
constexpr std::string_view kLingering = "minecraft:lingering_potion";
constexpr std::string_view kBottleId  = "minecraft:glass_bottle";
constexpr std::string_view kBucketId  = "minecraft:bucket";
constexpr std::string_view kBowlId    = "minecraft:bowl";

std::vector<FluidDef> makeFluids() {
    std::string const bottle(kBottleId), bucket(kBucketId), bowl(kBowlId);
    return {
        {"water", "cm.jar.fluid.water", "Water", 0, 1, FoodKind::None, "",
         {{"minecraft:water_bucket", bucket, kBucket}, {std::string(kPotion), bottle, kBottle}}},
        {"lava", "cm.jar.fluid.lava", "Lava", 15, 2, FoodKind::None, "", {{"minecraft:lava_bucket", bucket, kBucket}}},
        {"honey", "fluid.supplementaries.honey", "Honey", 0, 3, FoodKind::WholeItem, "minecraft:honey_bottle",
         {{"minecraft:honey_bottle", bottle, kBottle}, {"minecraft:honey_block", "", 4}}},
        {"milk", "fluid.supplementaries.milk", "Milk", 0, 4, FoodKind::Milk, "minecraft:milk_bucket",
         {{"minecraft:milk_bucket", bucket, kBucket}}},
        {"mushroom_stew", "cm.jar.fluid.mushroom_stew", "Mushroom Stew", 0, 5, FoodKind::Stew, "minecraft:mushroom_stew",
         {{"minecraft:mushroom_stew", bowl, kBowl}}},
        {"beetroot_soup", "cm.jar.fluid.beetroot_soup", "Beetroot Soup", 0, 6, FoodKind::Stew, "minecraft:beetroot_soup",
         {{"minecraft:beetroot_soup", bowl, kBowl}}},
        {"rabbit_stew", "cm.jar.fluid.rabbit_stew", "Rabbit Stew", 0, 7, FoodKind::Stew, "minecraft:rabbit_stew",
         {{"minecraft:rabbit_stew", bowl, kBowl}}},
        {"suspicious_stew", "cm.jar.fluid.suspicious_stew", "Suspicious Stew", 0, 8, FoodKind::SuspiciousStew,
         "minecraft:suspicious_stew", {{"minecraft:suspicious_stew", bowl, kBowl}}},
        {"experience", "fluid.supplementaries.experience", "Experience", 5, 9, FoodKind::Experience,
         "minecraft:experience_bottle", {{"minecraft:experience_bottle", bottle, kBottle}}},
        {"dragon_breath", "cm.jar.fluid.dragon_breath", "Dragon's Breath", 3, 10, FoodKind::None, "",
         {{"minecraft:dragon_breath", bottle, kBottle}}},
        {"ghast_tear", "cm.jar.fluid.ghast_tear", "Ghast Tear", 0, 11, FoodKind::None, "",
         {{"minecraft:ghast_tear", "", kBottle}}},
        {"magma_cream", "cm.jar.fluid.magma_cream", "Magma Cream", 0, 12, FoodKind::None, "",
         {{"minecraft:magma_cream", "", kBottle}}},
        {"slime", "fluid.supplementaries.slime", "Slime", 0, 13, FoodKind::None, "",
         {{"minecraft:slime_ball", "", kBottle}, {"minecraft:slime", "", 9}}},
        {"powder_snow", "cm.jar.fluid.powder_snow", "Powder Snow", 0, 14, FoodKind::None, "",
         {{"minecraft:powder_snow_bucket", bucket, 3}}},
        {"ominous", "cm.jar.fluid.ominous", "Ominous Bottle", 0, 15, FoodKind::WholeItem, "minecraft:ominous_bottle",
         {{"minecraft:ominous_bottle", bottle, kBottle}}},
        {"lumisene", "cm.jar.fluid.lumisene", "Lumisene", 15, 16, FoodKind::WholeItem,
         "complementaries:lumisene_bottle",
         {{"complementaries:lumisene_bottle", bottle, kBottle}, {"complementaries:lumisene_bucket", bucket, kBucket}}},
        {"potion", "cm.jar.fluid.potion", "Potion", 0, kContentFirstPotion, FoodKind::WholeItem, std::string(kPotion),
         {{std::string(kPotion), bottle, kBottle},
          {std::string(kSplash), bottle, kBottle},
          {std::string(kLingering), bottle, kBottle}}},
    };
}

// Bedrock potion aux value -> colour group (1..20, 0 = no effect) and effect name key.
struct PotionInfo {
    int              group;
    std::string_view key;
};

PotionInfo potionInfo(int aux) {
    switch (aux) {
    case 5:
    case 6:
        return {1, "night_vision"};
    case 7:
    case 8:
        return {2, "invisibility"};
    case 9:
    case 10:
    case 11:
        return {3, "leaping"};
    case 12:
    case 13:
        return {4, "fire_resistance"};
    case 14:
    case 15:
    case 16:
        return {5, "swiftness"};
    case 17:
    case 18:
    case 42:
        return {6, "slowness"};
    case 19:
    case 20:
        return {7, "water_breathing"};
    case 21:
    case 22:
        return {8, "healing"};
    case 23:
    case 24:
        return {9, "harming"};
    case 25:
    case 26:
    case 27:
        return {10, "poison"};
    case 28:
    case 29:
    case 30:
        return {11, "regeneration"};
    case 31:
    case 32:
    case 33:
        return {12, "strength"};
    case 34:
    case 35:
        return {13, "weakness"};
    case 36:
        return {14, "decay"};
    case 37:
    case 38:
    case 39:
        return {15, "turtle_master"};
    case 40:
    case 41:
        return {16, "slow_falling"};
    case 43:
        return {17, "wind_charging"};
    case 44:
        return {18, "weaving"};
    case 45:
        return {19, "oozing"};
    case 46:
        return {20, "infestation"};
    default:
        return {0, ""};
    }
}

bool isPotionItem(std::string_view id) { return id == kPotion || id == kSplash || id == kLingering; }

} // namespace

std::vector<FluidDef> const& fluids() {
    static std::vector<FluidDef> const all = makeFluids();
    return all;
}

FluidDef const* fluidDef(std::string_view id) {
    for (auto const& f : fluids()) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

int potionColorGroup(int potionAux) { return potionInfo(potionAux).group; }

std::string_view potionEffectKey(int potionAux) { return potionInfo(potionAux).key; }

int contentFor(FluidStack const& stack) {
    if (stack.empty()) return kContentEmpty;
    if (stack.fluid == "potion") return kContentFirstPotion + potionColorGroup(stack.data);
    auto const* def = fluidDef(stack.fluid);
    return def ? def->visual : kContentEmpty;
}

std::optional<Drained> fluidFromItem(ItemRef const& item) {
    if (item.id.empty()) return std::nullopt;
    if (isPotionItem(item.id)) {
        // a plain water bottle is water; every other potion keeps its type and bottle kind
        if (item.id == kPotion && item.aux == 0) {
            return Drained{FluidStack{"water", kBottle, 0, ""}, std::string(kBottleId)};
        }
        if (item.aux == 0) return std::nullopt; // splash / lingering water bottles are not water
        return Drained{FluidStack{"potion", kBottle, item.aux, item.id}, std::string(kBottleId)};
    }
    for (auto const& f : fluids()) {
        for (auto const& c : f.containers) {
            if (c.filled != item.id) continue;
            FluidStack s{f.id, c.amount, 0, ""};
            if (f.id == "ominous" || f.id == "suspicious_stew") s.data = item.aux;
            return Drained{std::move(s), c.empty};
        }
    }
    return std::nullopt;
}

std::optional<ItemRef> fillItem(FluidStack& tank, std::string_view emptyId) {
    if (tank.empty()) return std::nullopt;
    auto const* def = fluidDef(tank.fluid);
    if (!def) return std::nullopt;
    for (auto const& c : def->containers) {
        if (c.empty != emptyId) continue;
        if (tank.fluid == "potion" && c.filled != tank.bottle) continue; // keep the bottle kind
        if (tank.count < c.amount) continue;
        ItemRef out{c.filled, 0};
        if (tank.fluid == "potion" || tank.fluid == "ominous" || tank.fluid == "suspicious_stew") out.aux = tank.data;
        tank.count -= c.amount;
        if (tank.count <= 0) tank = {};
        return out;
    }
    return std::nullopt;
}

int Tank::add(FluidStack const& stack, bool simulate) {
    if (stack.empty() || !compatible(stack)) return 0;
    int amount = std::min(space(), stack.count);
    if (amount <= 0 || simulate) return std::max(amount, 0);
    if (empty()) {
        fluid       = stack;
        fluid.count = amount;
    } else {
        fluid.count += amount;
    }
    return amount;
}

FluidStack Tank::remove(int amount, bool simulate) {
    if (empty() || amount <= 0) return {};
    int        n   = std::min(amount, fluid.count);
    FluidStack out = fluid;
    out.count      = n;
    if (!simulate) {
        fluid.count -= n;
        normalize();
    }
    return out;
}

int Tank::comparatorOutput() const {
    if (empty() || capacity <= 0) return 0;
    return static_cast<int>(std::floor(static_cast<float>(fluid.count) / static_cast<float>(capacity) * 14.0f)) + 1;
}

int Tank::pixelLevel() const {
    if (empty() || capacity <= 0) return 0;
    int px = static_cast<int>(std::ceil(12.0 * fluid.count / capacity));
    return std::clamp(px, 1, 12);
}

void to_json(nlohmann::json& j, FluidStack const& s) {
    j = nlohmann::json{{"fluid", s.fluid}, {"count", s.count}};
    if (s.data) j["data"] = s.data;
    if (!s.bottle.empty()) j["bottle"] = s.bottle;
}

void from_json(nlohmann::json const& j, FluidStack& s) {
    s.fluid  = j.value("fluid", std::string{});
    s.count  = j.value("count", 0);
    s.data   = j.value("data", 0);
    s.bottle = j.value("bottle", std::string{});
    if (s.count <= 0) s = {};
}

} // namespace cm::feature::jar
