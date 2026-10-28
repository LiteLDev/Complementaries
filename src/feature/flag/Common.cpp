#include "feature/flag/Flag.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockSupportType.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"

namespace cm::feature::flag {

namespace {
constexpr std::string_view kNs = "complementaries:";

struct PatternInfo {
    std::string_view code;
    std::string_view english;
    std::string_view item;
};
constexpr std::array<PatternInfo, 43> kPatternInfo{{
    {"bs", "Base", ""},
    {"ts", "Chief", ""},
    {"ls", "Pale Dexter", ""},
    {"rs", "Pale Sinister", ""},
    {"cs", "Pale", ""},
    {"ms", "Fess", ""},
    {"drs", "Bend", ""},
    {"dls", "Bend Sinister", ""},
    {"ss", "Paly", ""},
    {"cr", "Saltire", ""},
    {"sc", "Cross", ""},
    {"bt", "Chevron", ""},
    {"tt", "Inverted Chevron", ""},
    {"bts", "Base Indented", ""},
    {"tts", "Chief Indented", ""},
    {"ld", "Per Bend Sinister", ""},
    {"rd", "Per Bend Sinister Inverted", ""},
    {"lud", "Per Bend Inverted", ""},
    {"rud", "Per Bend", ""},
    {"mc", "Roundel", ""},
    {"mr", "Lozenge", ""},
    {"vh", "Per Pale", ""},
    {"hh", "Per Fess", ""},
    {"vhr", "Per Pale Inverted", ""},
    {"hhb", "Per Fess Inverted", ""},
    {"bo", "Bordure", ""},
    {"cbo", "Bordure Indented", "minecraft:bordure_indented_banner_pattern"},
    {"gra", "Gradient", ""},
    {"gru", "Base Gradient", ""},
    {"bri", "Field Masoned", "minecraft:field_masoned_banner_pattern"},
    {"glb", "Globe", "minecraft:globe_banner_pattern"},
    {"cre", "Creeper Charge", "minecraft:creeper_banner_pattern"},
    {"sku", "Skull Charge", "minecraft:skull_banner_pattern"},
    {"flo", "Flower Charge", "minecraft:flower_banner_pattern"},
    {"moj", "Thing", "minecraft:mojang_banner_pattern"},
    {"pig", "Snout", "minecraft:piglin_banner_pattern"},
    {"bl", "Base Dexter Canton", ""},
    {"br", "Base Sinister Canton", ""},
    {"tl", "Chief Dexter Canton", ""},
    {"tr", "Chief Sinister Canton", ""},
    {"flw", "Flow", "minecraft:flow_banner_pattern"},
    {"gus", "Guster", "minecraft:guster_banner_pattern"},
    {"dragon", "Dragon Charge", "complementaries:dragon_banner_pattern"},
}};

constexpr std::array<std::string_view, kColors> kColorEnglish{
    "White", "Orange", "Magenta", "Light Blue", "Yellow", "Lime", "Pink", "Gray",
    "Light Gray", "Cyan", "Purple", "Blue", "Brown", "Green", "Red", "Black",
};

std::string translate(Player const* player, std::string_view key, std::string_view fallback) {
    return player ? trFor(*player, key, fallback) : tr(key, fallback);
}

/// Color suffix of `<prefix><color>` ids ("complementaries:flag_" + "red").
int suffixColor(std::string_view id, std::string_view prefix) {
    if (!id.starts_with(prefix)) return -1;
    return colorIndex(id.substr(prefix.size()));
}
} // namespace

// ---- colours / patterns ---------------------------------------------------------------------------

int colorIndex(std::string_view name) {
    for (int i = 0; i < kColors; ++i)
        if (kColorNames[i] == name) return i;
    return -1;
}

std::string colorName(int javaColor, Player const* player) {
    if (javaColor < 0 || javaColor >= kColors) return {};
    return translate(player, "cm.flag.color." + std::string(kColorNames[javaColor]), kColorEnglish[javaColor]);
}

int patternIndex(std::string_view code) {
    for (size_t i = 0; i < kPatternCodes.size(); ++i)
        if (kPatternCodes[i] == code) return static_cast<int>(i);
    return -1;
}

std::string_view patternEnglish(std::string_view code) {
    for (auto const& p : kPatternInfo)
        if (p.code == code) return p.english;
    return code;
}

std::string_view patternItem(std::string_view code) {
    for (auto const& p : kPatternInfo)
        if (p.code == code) return p.item;
    return {};
}

std::string layerName(std::string_view code, int javaColor, Player const* player) {
    auto pattern = translate(player, "cm.flag.pattern." + std::string(code), patternEnglish(code));
    auto color   = colorName(javaColor, player);
    auto fmt     = translate(player, "cm.flag.layer", "{0} {1}");
    return core::formatMessageArgs(fmt, color, pattern);
}

// ---- item layers ------------------------------------------------------------------------------------

Layers readLayers(ItemStackBase const& item) {
    Layers out;
    auto const& data = item.mUserData;
    if (!data || !data->contains("Patterns", Tag::List)) return out;
    for (auto const& e : (*data)["Patterns"].get<ListTag>()) {
        if (!e.hold(Tag::Compound)) continue;
        auto const& c = e.get<CompoundTag>();
        if (!c.contains("Pattern", Tag::String)) continue;
        Layer l;
        l.pattern = static_cast<std::string const&>(c["Pattern"].get<StringTag>());
        l.color   = c.contains("Color", Tag::Int) ? fromItemColor(c["Color"].get<IntTag>().data) : 0;
        l.color   = std::clamp(l.color, 0, kColors - 1);
        out.push_back(std::move(l));
    }
    return out;
}

void writeLayers(ItemStack& item, Layers const& layers, Player const* player) {
    auto tag = item.mUserData ? item.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->erase("Patterns");
    if (!layers.empty()) {
        std::vector<CompoundTagVariant> list;
        for (auto const& l : layers) {
            CompoundTag c;
            c["Pattern"] = l.pattern;
            c["Color"]   = toItemColor(l.color);
            list.emplace_back(std::move(c));
        }
        (*tag)["Patterns"] = ListTag(std::move(list));
    }
    if (tag->contains("display", Tag::Compound)) {
        auto& display = (*tag)["display"].get<CompoundTag>();
        display.erase("Lore");
        if (display.empty()) tag->erase("display");
    }
    if (tag->empty()) item.setUserData(nullptr);
    else item.setUserData(std::move(tag));
    if (!layers.empty()) {
        std::vector<std::string> lore;
        for (auto const& l : layers) lore.push_back("§r§7" + layerName(l.pattern, l.color, player));
        item.setCustomLore(lore);
    }
}

nlohmann::json layersToJson(Layers const& layers) {
    auto j = nlohmann::json::array();
    for (auto const& l : layers) j.push_back({{"pattern", l.pattern}, {"color", l.color}});
    return j;
}

Layers layersFromJson(nlohmann::json const& j) {
    Layers out;
    if (!j.is_array()) return out;
    for (auto const& e : j) {
        if (!e.is_object()) continue;
        out.push_back({e.value("pattern", std::string{}), std::clamp(e.value("color", 0), 0, kColors - 1)});
    }
    return out;
}

int layerProperty(Layer const& layer) {
    int idx = patternIndex(layer.pattern);
    if (idx < 0) return 0;
    return (idx + 1) * 16 + std::clamp(layer.color, 0, kColors - 1);
}

// ---- items / ids --------------------------------------------------------------------------------------

ItemStack makeItem(std::string_view id, int count, int aux) {
    ItemStack s;
    s.reinit(id, count, aux);
    return s;
}

int  flagColor(std::string_view id) { return suffixColor(id, "complementaries:flag_"); }
bool isFlag(std::string_view id) { return flagColor(id) >= 0; }
bool isAwning(std::string_view id) {
    return id == "complementaries:awning" || suffixColor(id, "complementaries:awning_") >= 0;
}
bool isBuntingCeiling(std::string_view id) { return suffixColor(id, "complementaries:bunting_") >= 0; }
bool isBuntingWall(std::string_view id) { return suffixColor(id, "complementaries:bunting_wall_") >= 0; }
bool isBuntingItem(std::string_view id) { return isBuntingCeiling(id); }
int  buntingColor(std::string_view id) {
    int c = suffixColor(id, "complementaries:bunting_wall_");
    return c >= 0 ? c : suffixColor(id, "complementaries:bunting_");
}
bool isStick(std::string_view id) { return id == "complementaries:stick"; }

int dyeColor(std::string_view itemId) {
    if (!itemId.starts_with("minecraft:")) return -1;
    auto name = itemId.substr(10);
    if (name == "ink_sac") return 15;
    if (name == "bone_meal") return 0;
    if (name == "lapis_lazuli") return 11;
    if (name == "cocoa_beans") return 12;
    if (name.ends_with("_dye")) return colorIndex(name.substr(0, name.size() - 4));
    return -1;
}

int bannerOrFlagColor(ItemStackBase const& item) {
    if (item.isNull()) return -1;
    auto id = item.getTypeName();
    if (id == "minecraft:banner") return fromItemColor(std::clamp<int>(item.getAuxValue(), 0, 15));
    return flagColor(id);
}

// ---- directions ----------------------------------------------------------------------------------------

BlockPos offset(BlockPos const& pos, int dir, int n) {
    switch (dir) {
    case Down:
        return {pos.x, pos.y - n, pos.z};
    case Up:
        return {pos.x, pos.y + n, pos.z};
    case North:
        return {pos.x, pos.y, pos.z - n};
    case South:
        return {pos.x, pos.y, pos.z + n};
    case West:
        return {pos.x - n, pos.y, pos.z};
    default:
        return {pos.x + n, pos.y, pos.z};
    }
}

int  oppositeDir(int dir) { return dir ^ 1; }
bool horizontal(int dir) { return dir >= North; }

int dirFrom2d(int facing2d) {
    switch (facing2d & 3) {
    case 0:
        return South;
    case 1:
        return West;
    case 2:
        return North;
    default:
        return East;
    }
}

int dir2d(int dir) {
    switch (dir) {
    case South:
        return 0;
    case West:
        return 1;
    case North:
        return 2;
    case East:
        return 3;
    default:
        return -1;
    }
}

// Java: N -> E -> S -> W
int clockwise(int dir) {
    switch (dir) {
    case North:
        return East;
    case East:
        return South;
    case South:
        return West;
    case West:
        return North;
    default:
        return dir;
    }
}
int counterClockwise(int dir) { return oppositeDir(clockwise(dir)); }

std::array<int, 6> nearestLookingDirections(Actor const& actor) {
    auto const& rot   = actor.getRotation();
    double      pitch = rot.x * std::numbers::pi / 180.0;
    double      yaw   = rot.y * std::numbers::pi / 180.0;
    double      vx    = -std::sin(yaw) * std::cos(pitch);
    double      vy    = -std::sin(pitch);
    double      vz    = std::cos(yaw) * std::cos(pitch);
    std::array<std::pair<double, int>, 6> d{{{-vy, Down}, {vy, Up}, {-vz, North}, {vz, South}, {-vx, West}, {vx, East}}};
    std::stable_sort(d.begin(), d.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    std::array<int, 6> out{};
    for (int i = 0; i < 6; ++i) out[i] = d[i].second;
    return out;
}

// ---- flag storage ----------------------------------------------------------------------------------------

std::string flagKey(core::GlobalPos const& pos) {
    return "cm:flag:flag:" + std::to_string(pos.dim) + ":" + std::to_string(pos.x) + ":" + std::to_string(pos.y) + ":"
         + std::to_string(pos.z);
}

std::optional<FlagData> loadFlag(core::GlobalPos const& pos) {
    auto j = game::db::load(flagKey(pos));
    if (!j || !j->is_object()) return std::nullopt;
    FlagData d;
    d.color  = std::clamp(j->value("color", 0), 0, kColors - 1);
    d.facing = j->value("facing", 2) & 3;
    d.layers = layersFromJson(j->value("layers", nlohmann::json::array()));
    d.name   = j->value("name", std::string{});
    return d;
}

void saveFlag(core::GlobalPos const& pos, FlagData const& data) {
    game::db::save(
        flagKey(pos),
        {{"color", data.color}, {"facing", data.facing}, {"layers", layersToJson(data.layers)}, {"name", data.name}}
    );
}

void eraseFlag(core::GlobalPos const& pos) { game::db::erase(flagKey(pos)); }

// ---- world helpers ------------------------------------------------------------------------------------------

bool solid(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) return false;
    auto const& b = region.getBlock(pos);
    return !b.isAir() && b._isSolid();
}

bool sturdyFace(BlockSource& region, BlockPos const& pos, int face) {
    if (!region.hasChunksAt(pos, 0, false)) return false;
    auto const& b = region.getBlock(pos);
    return !b.isAir() && b.canProvideSupport(static_cast<uchar>(face), BlockSupportType::Center);
}

bool place(BlockSource& region, BlockPos const& pos, std::string_view id, std::vector<std::pair<std::string_view, int>> const& states) {
    auto const* block = game::blockWith(id, states);
    return block && game::setBlock(region, pos, *block);
}

void breakWithDrops(BlockSource& region, BlockPos const& pos) {
    auto level = ll::service::getLevel();
    if (!level) return;
    BlockChangeContext ctx{};
    level->destroyBlock(region, pos, true, ctx);
}

} // namespace cm::feature::flag
