#pragma once

// Soft fluids (pure logic, no game headers): virtual fluids measured in bottles that jars and
// faucets store and move, and the item containers that carry them.
//
// Amounts: bottle 1, bowl 2, bucket 4, honey block 4, powder snow bucket 3, slime block 9.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json_fwd.hpp"

namespace cm::feature::jar {

inline constexpr int kBottle = 1;
inline constexpr int kBowl   = 2;
inline constexpr int kBucket = 4;

/// A Bedrock item by id and aux value (potions keep their type in the aux value).
struct ItemRef {
    std::string id;
    int         aux = 0;

    bool operator==(ItemRef const&) const = default;
};

/// An amount of one soft fluid. Two stacks mix only when `sameKind` (same fluid and same data).
struct FluidStack {
    std::string fluid;  ///< fluid id ("water", "potion", ...); empty = no fluid
    int         count = 0;
    int         data  = 0;  ///< potion type, ominous level, suspicious stew kind (item aux)
    std::string bottle;     ///< potions: the bottle item ("minecraft:splash_potion", ...)

    [[nodiscard]] bool empty() const { return fluid.empty() || count <= 0; }
    [[nodiscard]] bool sameKind(FluidStack const& o) const {
        return fluid == o.fluid && data == o.data && bottle == o.bottle;
    }
    bool operator==(FluidStack const&) const = default;
};

/// How a fluid is eaten / drunk (Supplementaries "food provider").
enum class FoodKind {
    None,
    WholeItem,   ///< acts like using the item itself (honey bottle, potion, ominous bottle, lumisene)
    Stew,        ///< bowl food: nutrition and saturation of the item / 2
    SuspiciousStew,
    Milk,        ///< removes one effect
    Experience,  ///< gives the experience of one bottle
};

struct Container {
    std::string filled;     ///< filled item id
    std::string empty;      ///< empty container id ("" = nothing, i.e. the empty hand)
    int         amount = 1; ///< bottles
};

struct FluidDef {
    std::string            id;
    std::string            nameKey;      ///< translation key of the fluid name
    std::string            nameFallback; ///< English name
    int                    light  = 0;   ///< luminosity 0..15
    int                    visual = 0;   ///< jar / faucet `content` state (see contentFor)
    FoodKind               food   = FoodKind::None;
    std::string            foodItem;     ///< item whose effect a sip has
    std::vector<Container> containers;
};

/// All built-in fluids (vanilla + lumisene). `lumisene` is only usable when its items exist.
[[nodiscard]] std::vector<FluidDef> const& fluids();
[[nodiscard]] FluidDef const*              fluidDef(std::string_view id);

// ---- content state (jar / faucet visuals) ----------------------------------------------------
inline constexpr int kContentEmpty     = 0;
inline constexpr int kContentFirstPotion = 17;
inline constexpr int kContentCookie    = 48;
inline constexpr int kContentCandy     = 49;
inline constexpr int kContentItem      = 50;
inline constexpr int kContentLight5    = 62;
inline constexpr int kContentLight3    = 63;

/// The `content` state value showing `stack` (0 when empty).
[[nodiscard]] int contentFor(FluidStack const& stack);
/// Potion colour group (0 = no effect) of a Bedrock potion aux value.
[[nodiscard]] int potionColorGroup(int potionAux);
/// The English effect name of a potion aux value ("" for water / mundane / awkward / thick).
[[nodiscard]] std::string_view potionEffectKey(int potionAux);

// ---- items <-> fluids ------------------------------------------------------------------------

struct Drained {
    FluidStack fluid;     ///< what the filled item holds
    std::string emptyId;  ///< what is left in the hand ("" = nothing)
};

/// The fluid inside a filled item (water bucket, potion, stew, ...), if any.
[[nodiscard]] std::optional<Drained> fluidFromItem(ItemRef const& item);

/// Fills `emptyId` ("" = empty hand) from `tank`: returns the filled item and removes its amount
/// from `tank`, or nullopt when the tank's fluid has no such container or not enough fluid.
[[nodiscard]] std::optional<ItemRef> fillItem(FluidStack& tank, std::string_view emptyId);

/// A tank: one fluid kind up to `capacity` bottles.
struct Tank {
    FluidStack fluid;
    int        capacity = 12;

    [[nodiscard]] bool empty() const { return fluid.empty(); }
    [[nodiscard]] int  space() const { return capacity > fluid.count ? capacity - fluid.count : 0; }
    [[nodiscard]] bool compatible(FluidStack const& s) const { return empty() || fluid.sameKind(s); }
    /// Adds up to `stack.count`; returns the amount added (all or part of it).
    int add(FluidStack const& stack, bool simulate);
    /// Removes up to `amount`; returns the removed stack.
    FluidStack remove(int amount, bool simulate);
    /// floor(count / capacity * 14) + 1, 0 when empty (Supplementaries comparator output).
    [[nodiscard]] int comparatorOutput() const;
    /// Visual fill level in pixels (0..12) for a 12 px high jar.
    [[nodiscard]] int pixelLevel() const;
    void              normalize() {
        if (fluid.count <= 0) fluid = {};
    }
};

// ---- JSON ------------------------------------------------------------------------------------
void       to_json(nlohmann::json& j, FluidStack const& s);
void       from_json(nlohmann::json const& j, FluidStack& s);

} // namespace cm::feature::jar
