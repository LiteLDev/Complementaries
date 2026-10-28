#include "feature/sign/SignData.h"

#include <cmath>
#include <numbers>

namespace cm::feature::sign {

std::string signPostItemId(int wood) {
    if (wood < 0 || wood >= static_cast<int>(kWoods.size())) wood = 0;
    return "complementaries:sign_post_" + std::string(kWoods[wood]);
}

std::optional<int> woodOfSignPostItem(std::string_view itemId) {
    constexpr std::string_view prefix = "complementaries:sign_post_";
    if (!itemId.starts_with(prefix)) return std::nullopt;
    itemId.remove_prefix(prefix.size());
    for (int i = 0; i < static_cast<int>(kWoods.size()); ++i) {
        if (kWoods[i] == itemId) return i;
    }
    return std::nullopt;
}

int woodOfBlock(std::string_view blockId) {
    if (auto colon = blockId.find(':'); colon != std::string_view::npos) blockId.remove_prefix(colon + 1);
    int best = 0;
    std::size_t bestLen = 0;
    for (int i = 0; i < static_cast<int>(kWoods.size()); ++i) {
        // longest match wins ("dark_oak" over "oak", "pale_oak" over "oak")
        if (blockId.starts_with(kWoods[i]) && kWoods[i].size() > bestLen) {
            best    = i;
            bestLen = kWoods[i].size();
        }
    }
    return best;
}

namespace {
constexpr std::array<std::string_view, 16> kDyeNames = {
    "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
    "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black",
};
// Sign text colours of the dyes (the game's own DyeColor text colours).
constexpr std::array<std::uint32_t, 16> kDyeText = {
    0xFFFFFF, 0xFF681F, 0xFF00FF, 0x9AC0CD, 0xFFFF00, 0xBFFF00, 0xFF69B4, 0x808080,
    0xD3D3D3, 0x00FFFF, 0xA020F0, 0x0000FF, 0x8B4513, 0x00FF00, 0xFF0000, 0x000000,
};
// Name tags have a dark background: black text becomes white there.
constexpr std::array<char, 16> kDyeCodes = {
    'f', '6', 'd', 'b', 'e', 'a', 'd', '8', '7', '3', '5', '9', 'n', '2', 'c', 'f',
};
} // namespace

std::optional<int> dyeOfItem(std::string_view itemId) {
    constexpr std::string_view ns = "minecraft:";
    if (!itemId.starts_with(ns) || !itemId.ends_with("_dye")) return std::nullopt;
    itemId.remove_prefix(ns.size());
    itemId.remove_suffix(4);
    for (int i = 0; i < 16; ++i) {
        if (kDyeNames[i] == itemId) return i;
    }
    return std::nullopt;
}

std::uint32_t dyeTextColor(int dye) { return kDyeText[static_cast<std::size_t>(dye & 15)]; }

std::uint32_t dyeOutlineColor(int dye) {
    // Glowing text: black text gets a light outline, other colours a darker shade of themselves.
    if ((dye & 15) == kDefaultDye) return 0xF0EBCC;
    auto c = dyeTextColor(dye);
    auto r = static_cast<std::uint32_t>(((c >> 16) & 0xFF) * 0.4);
    auto g = static_cast<std::uint32_t>(((c >> 8) & 0xFF) * 0.4);
    auto b = static_cast<std::uint32_t>((c & 0xFF) * 0.4);
    return (r << 16) | (g << 8) | b;
}

char dyeFormatCode(int dye) { return kDyeCodes[static_cast<std::size_t>(dye & 15)]; }

// ---- text ---------------------------------------------------------------------------------------
namespace {
int asciiWidth(char c) {
    switch (c) {
    case '!':
    case '\'':
    case ',':
    case '.':
    case ':':
    case ';':
    case '|':
    case 'i':
        return 2;
    case '`':
    case 'l':
        return 3;
    case ' ':
    case '"':
    case '(':
    case ')':
    case '*':
    case 'I':
    case '[':
    case ']':
    case 't':
    case '{':
    case '}':
        return 4;
    case '<':
    case '>':
    case 'f':
    case 'k':
        return 5;
    case '@':
    case '~':
        return 7;
    default:
        return 6;
    }
}

/// Decodes one UTF-8 code point starting at `i` (advances `i`).
std::uint32_t nextCodePoint(std::string_view s, std::size_t& i) {
    auto c = static_cast<unsigned char>(s[i]);
    int  extra = 0;
    std::uint32_t cp = c;
    if (c >= 0xF0) {
        extra = 3;
        cp    = c & 0x07;
    } else if (c >= 0xE0) {
        extra = 2;
        cp    = c & 0x0F;
    } else if (c >= 0xC0) {
        extra = 1;
        cp    = c & 0x1F;
    }
    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    return cp;
}

int codePointWidth(std::uint32_t cp) {
    if (cp < 0x80) return asciiWidth(static_cast<char>(cp));
    if (cp >= 0x2E80) return 9; // CJK and other wide glyphs of the unicode font
    return 6;
}
} // namespace

int textWidth(std::string_view text) {
    int         w = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        auto cp = nextCodePoint(text, i);
        if (cp == 0xA7 && i < text.size()) { // § + code: no width
            nextCodePoint(text, i);
            continue;
        }
        w += codePointWidth(cp);
    }
    return w;
}

std::string stripFormatting(std::string_view text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t start = i;
        auto        cp    = nextCodePoint(text, i);
        if (cp == 0xA7) {
            if (i < text.size()) nextCodePoint(text, i);
            continue;
        }
        if (cp < 0x20 || cp == 0x7F) continue;
        out.append(text.substr(start, i - start));
    }
    return out;
}

std::string fitWidth(std::string_view text, int maxWidth) {
    int         w = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t start = i;
        auto        cp    = nextCodePoint(text, i);
        w += codePointWidth(cp);
        if (w > maxWidth) return std::string(text.substr(0, start));
    }
    return std::string(text);
}

std::vector<std::string> wrapText(std::string_view text, int maxWidth) {
    std::vector<std::string> lines;
    std::size_t              pos = 0;
    while (pos <= text.size()) {
        auto nl   = text.find('\n', pos);
        auto para = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        std::string line;
        std::size_t i = 0;
        while (i < para.size()) {
            // next word (with its leading spaces)
            std::size_t j = i;
            while (j < para.size() && para[j] == ' ') ++j;
            while (j < para.size() && para[j] != ' ') ++j;
            std::string word(para.substr(i, j - i));
            if (textWidth(line + word) <= maxWidth) {
                line += word;
            } else {
                if (!line.empty()) lines.push_back(line);
                // drop leading spaces of a word that starts a line
                auto first = word.find_first_not_of(' ');
                word       = first == std::string::npos ? std::string{} : word.substr(first);
                while (textWidth(word) > maxWidth) { // a word longer than a line
                    auto part = fitWidth(word, maxWidth);
                    if (part.empty()) break;
                    lines.push_back(part);
                    word = word.substr(part.size());
                }
                line = word;
            }
            i = j;
        }
        lines.push_back(line);
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

// ---- sign post data -----------------------------------------------------------------------------
namespace {
nlohmann::json armJson(Arm const& a) {
    return {
        {"active", a.active},
        {"wood", std::string(kWoods[static_cast<std::size_t>(a.wood)])},
        {"yaw", a.yaw},
        {"left", a.left},
        {"text", a.text},
        {"color", a.color},
        {"glowing", a.glowing},
    };
}
Arm armFromJson(nlohmann::json const& j) {
    Arm a;
    if (!j.is_object()) return a;
    a.active   = j.value("active", false);
    auto wood  = j.value("wood", std::string("oak"));
    a.wood     = 0;
    for (int i = 0; i < static_cast<int>(kWoods.size()); ++i) {
        if (kWoods[i] == wood) a.wood = i;
    }
    a.yaw     = normalizeYaw(j.value("yaw", 0.0f));
    a.left    = j.value("left", false);
    a.text    = j.value("text", std::string{});
    a.color   = j.value("color", kDefaultDye) & 15;
    a.glowing = j.value("glowing", false);
    return a;
}
} // namespace

nlohmann::json toJson(Post const& post) {
    return {{"up", armJson(post.up)}, {"down", armJson(post.down)}, {"waxed", post.waxed}, {"entity", post.entity}};
}

Post postFromJson(nlohmann::json const& j) {
    Post p;
    if (!j.is_object()) return p;
    if (j.contains("up")) p.up = armFromJson(j["up"]);
    if (j.contains("down")) p.down = armFromJson(j["down"]);
    p.waxed  = j.value("waxed", false);
    p.entity = j.value("entity", std::int64_t{0});
    return p;
}

float normalizeYaw(float yaw) {
    float y = std::fmod(yaw, 360.0f);
    if (y < 0) y += 360.0f;
    if (y >= 360.0f) y = 0.0f;
    return y;
}

float yawTowards(double fromX, double fromZ, double toX, double toZ) {
    double dx = toX - fromX;
    double dz = toZ - fromZ;
    // Minecraft yaw: direction (-sin(yaw), cos(yaw)), so yaw = atan2(-dx, dz)
    double deg = std::atan2(-dx, dz) * 180.0 / std::numbers::pi;
    return normalizeYaw(static_cast<float>(deg));
}

int packArm(Arm const& a) {
    if (!a.active) return 0;
    int yaw = static_cast<int>(std::lround(normalizeYaw(a.yaw))) % 360;
    return 1 | (a.left ? 2 : 0) | ((a.wood & 15) << 2) | ((yaw & 511) << 6) | ((a.color & 15) << 15)
         | (a.glowing ? (1 << 19) : 0);
}

Arm unpackArm(int p) {
    Arm a;
    a.active  = (p & 1) != 0;
    a.left    = (p & 2) != 0;
    a.wood    = (p >> 2) & 15;
    if (a.wood >= static_cast<int>(kWoods.size())) a.wood = 0;
    a.yaw     = static_cast<float>((p >> 6) & 511);
    a.color   = (p >> 15) & 15;
    a.glowing = ((p >> 19) & 1) != 0;
    return a;
}

int packDisplay(DisplayInfo const& d) {
    return (static_cast<int>(d.kind) & 3) | ((d.facing & 3) << 2) | ((d.color & 15) << 4) | (d.glowing ? (1 << 8) : 0);
}

DisplayInfo unpackDisplay(int p) {
    DisplayInfo d;
    d.kind    = static_cast<DisplayKind>(p & 3);
    d.facing  = (p >> 2) & 3;
    d.color   = (p >> 4) & 15;
    d.glowing = ((p >> 8) & 1) != 0;
    return d;
}

std::string colouredLines(std::vector<std::string> const& lines, int dye) {
    std::string out;
    std::string code = "\xC2\xA7";
    code += dyeFormatCode(dye);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        out += code + lines[i];
    }
    return out;
}

std::string postNameTag(Post const& post) {
    std::string out;
    bool        any = false;
    for (Arm const* a : {&post.up, &post.down}) {
        if (!a->active) continue;
        if (any) out += '\n';
        any = true;
        if (!a->text.empty()) {
            out += "\xC2\xA7";
            out += dyeFormatCode(a->color);
            out += a->text;
        }
    }
    // An all-empty tag hides the name tag completely.
    if (out.find_first_not_of('\n') == std::string::npos) return {};
    return out;
}

std::string posKey(int dim, int x, int y, int z) {
    return std::to_string(dim) + ":" + std::to_string(x) + ":" + std::to_string(y) + ":" + std::to_string(z);
}

std::optional<KeyPos> parsePosKey(std::string_view key) {
    KeyPos p{};
    int*   out[4] = {&p.dim, &p.x, &p.y, &p.z};
    for (int i = 0; i < 4; ++i) {
        auto colon = key.find(':');
        auto part  = key.substr(0, colon);
        if (part.empty()) return std::nullopt;
        try {
            *out[i] = std::stoi(std::string(part));
        } catch (...) {
            return std::nullopt;
        }
        if (colon == std::string_view::npos) {
            if (i != 3) return std::nullopt;
            break;
        }
        key.remove_prefix(colon + 1);
        if (i == 3) return std::nullopt;
    }
    return p;
}

} // namespace cm::feature::sign
