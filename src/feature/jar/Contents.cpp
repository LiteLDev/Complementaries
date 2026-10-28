#include "feature/jar/Contents.h"

#include <algorithm>

namespace cm::feature::jar {

void to_json(nlohmann::json& j, StoredItem const& s) {
    j = nlohmann::json{{"id", s.id}};
    if (s.aux) j["aux"] = s.aux;
    if (!s.nbt.empty()) j["nbt"] = s.nbt;
}

void from_json(nlohmann::json const& j, StoredItem& s) {
    s.id  = j.value("id", std::string{});
    s.aux = j.value("aux", 0);
    s.nbt = j.value("nbt", std::string{});
}

void to_json(nlohmann::json& j, StoredMob const& s) {
    j = nlohmann::json{{"type", s.type}, {"name", s.name}, {"display", s.display}};
    if (!s.nbt.empty()) j["nbt"] = s.nbt;
    if (!s.bucket.empty()) j["bucket"] = s.bucket;
    if (s.light) j["light"] = s.light;
    if (s.baby) j["baby"] = true;
}

void from_json(nlohmann::json const& j, StoredMob& s) {
    s.type    = j.value("type", std::string{});
    s.name    = j.value("name", std::string{});
    s.nbt     = j.value("nbt", std::string{});
    s.bucket  = j.value("bucket", std::string{});
    s.display = j.value("display", 0);
    s.light   = j.value("light", 0);
    s.baby    = j.value("baby", false);
}

void to_json(nlohmann::json& j, Contents const& c) {
    j = nlohmann::json::object();
    if (!c.fluid.empty()) j["fluid"] = c.fluid;
    if (!c.items.empty()) j["items"] = c.items;
    if (c.mob) j["mob"] = *c.mob;
}

void from_json(nlohmann::json const& j, Contents& c) {
    c = {};
    if (!j.is_object()) return;
    if (auto it = j.find("fluid"); it != j.end() && it->is_object()) c.fluid = it->get<FluidStack>();
    if (auto it = j.find("items"); it != j.end() && it->is_array()) {
        for (auto const& e : *it) {
            auto s = e.get<StoredItem>();
            if (!s.id.empty() && c.items.size() < static_cast<size_t>(kMaxCookies)) c.items.push_back(std::move(s));
        }
    }
    if (auto it = j.find("mob"); it != j.end() && it->is_object()) {
        auto m = it->get<StoredMob>();
        if (!m.type.empty()) c.mob = std::move(m);
    }
}

JarVisual visualFor(Contents const& c, int capacity) {
    if (!c.fluid.empty()) return {contentFor(c.fluid), Tank{c.fluid, capacity}.pixelLevel()};
    if (!c.items.empty()) {
        auto const& id      = c.items.front().id;
        int         content = id == "minecraft:cookie" ? kContentCookie
                            : id == "complementaries:candy" ? kContentCandy
                                                            : kContentItem;
        return {content, std::min(static_cast<int>(c.items.size()), kMaxCookies)};
    }
    if (c.mob) {
        if (!c.mob->bucket.empty()) return {1, 12}; // fish swim in water
        if (c.mob->light >= 5) return {kContentLight5, 0};
        if (c.mob->light > 0) return {kContentLight3, 0};
    }
    return {};
}

std::string toHex(std::string const& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string           out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

std::string fromHex(std::string const& hex) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

} // namespace cm::feature::jar
