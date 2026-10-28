#pragma once

// What a jar holds (pure data, no game headers): a fluid, up to 12 cookies, or one mob.
// Saved as JSON in the world DB (placed jars) and in the jar item's user data.

#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "feature/jar/Fluids.h"

namespace cm::feature::jar {

inline constexpr int kMaxCookies = 12;

/// An item stored in a jar: its id / aux plus its full NBT (hex of the binary tag, may be empty).
struct StoredItem {
    std::string id;
    int         aux = 0;
    std::string nbt;

    bool operator==(StoredItem const&) const = default;
};

/// A captured mob.
struct StoredMob {
    std::string type;        ///< entity id ("minecraft:bee")
    std::string name;        ///< display name (custom name or the type's name)
    std::string nbt;         ///< hex of the binary entity tag (empty when it came from a bucket)
    std::string bucket;      ///< fish kept as a bucket ("minecraft:cod_bucket"), shown with water
    int         display = 0; ///< jar_mob display kind (0 = not drawn)
    int         light   = 0; ///< light the mob gives the jar
    bool        baby    = false;

    bool operator==(StoredMob const&) const = default;
};

struct Contents {
    FluidStack               fluid;
    std::vector<StoredItem>  items;
    std::optional<StoredMob> mob;

    [[nodiscard]] bool empty() const { return fluid.empty() && items.empty() && !mob; }
    bool               operator==(Contents const&) const = default;
};

void to_json(nlohmann::json& j, StoredItem const& s);
void from_json(nlohmann::json const& j, StoredItem& s);
void to_json(nlohmann::json& j, StoredMob const& s);
void from_json(nlohmann::json const& j, StoredMob& s);
void to_json(nlohmann::json& j, Contents const& c);
void from_json(nlohmann::json const& j, Contents& c);

/// `content` / `level` block states for a jar with these contents.
struct JarVisual {
    int content = 0;
    int level   = 0;
};
[[nodiscard]] JarVisual visualFor(Contents const& c, int capacity);

/// Hex helpers for binary NBT kept in JSON.
[[nodiscard]] std::string toHex(std::string const& bytes);
[[nodiscard]] std::string fromHex(std::string const& hex);

} // namespace cm::feature::jar
