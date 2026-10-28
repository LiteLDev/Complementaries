// Antique ink: signs get sepia text (the closest Bedrock has to Supplementaries' antique font),
// written books and filled maps get an "antique" flag. Antique signs are remembered in the world
// DB (`cm:tool:antique:<dim>:<x>:<y>:<z>`) so soap knows what to wash off.

#include <format>

#include "mc/deps/core/math/Color.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/SignBlockActor.h"

#include "feature/tool/Tool.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::tool::antique {

namespace {

constexpr std::uint32_t kAntiqueColor = 0xFF4E3220; ///< sepia ink
constexpr std::uint32_t kBlack        = 0xFF000000;

std::string dbKey(int dim, BlockPos const& pos) {
    return std::format("cm:tool:antique:{}:{}:{}:{}", dim, pos.x, pos.y, pos.z);
}

SignBlockActor* signAt(BlockSource& region, BlockPos const& pos) {
    auto* ba = region.getBlockEntity(pos);
    if (!ba) return nullptr;
    auto type = ba->getType();
    if (type != BlockActorType::Sign && type != BlockActorType::HangingSign) return nullptr;
    return static_cast<SignBlockActor*>(ba);
}

std::uint32_t toArgb(mce::Color const& c) {
    auto byte = [](float f) { return static_cast<std::uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
    return (byte(c.a) << 24) | (byte(c.r) << 16) | (byte(c.g) << 8) | byte(c.b);
}

mce::Color fromArgb(std::uint32_t argb) {
    return mce::Color{
        static_cast<float>((argb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(argb & 0xFF) / 255.0f,
        static_cast<float>((argb >> 24) & 0xFF) / 255.0f,
    };
}

std::string antiqueLorePrefix() { return fmtCode("r6") + tr("message.supplementaries.antique", "Antique"); }

} // namespace

bool isSign(Block const& block) { return block.getTypeName().find("sign") != std::string::npos; }

std::optional<std::uint32_t> signColor(BlockSource& region, BlockPos const& pos) {
    auto* sign = signAt(region, pos);
    if (!sign) return std::nullopt;
    auto* front = sign->mTextFront.get();
    if (!front) return std::nullopt;
    return toArgb(*front->mSignTextColor);
}

bool setSignLook(BlockSource& region, BlockPos const& pos, std::uint32_t color, bool glow) {
    auto* sign = signAt(region, pos);
    if (!sign) return false;
    bool changed = false;
    for (auto* text : {sign->mTextFront.get(), sign->mTextBack.get()}) {
        if (!text) continue;
        if (toArgb(*text->mSignTextColor) != color || text->mGlowing != glow) changed = true;
        *text->mSignTextColor = fromArgb(color);
        text->mGlowing        = glow;
    }
    sign->refresh(region);
    return changed;
}

bool isAntiqueSign(int dim, BlockPos const& pos) {
    if (!game::db::load(dbKey(dim, pos))) return false;
    auto* region = regionOf(dim);
    if (!region) return true;
    auto c = signColor(*region, pos);
    if (c && *c == kAntiqueColor) return true;
    game::db::erase(dbKey(dim, pos)); // re-inked (vanilla ink sac) or replaced
    return false;
}

bool applyToSign(BlockSource& region, BlockPos const& pos) {
    if (!signAt(region, pos)) return false;
    int dim = static_cast<int>(region.getDimensionId());
    setSignLook(region, pos, kAntiqueColor, false);
    game::db::save(dbKey(dim, pos), true);
    log().info("antique ink applied to the sign at {} {} {}", pos.x, pos.y, pos.z);
    return true;
}

bool removeFromSign(BlockSource& region, BlockPos const& pos) {
    int dim = static_cast<int>(region.getDimensionId());
    if (!isAntiqueSign(dim, pos)) return false;
    game::db::erase(dbKey(dim, pos));
    setSignLook(region, pos, kBlack, false);
    return true;
}

bool useOnBlock(Player& player, BlockPos const& pos) {
    if (!config().antiqueInk.enabled) return false;
    auto& region = player.getDimensionBlockSource();
    if (!isSign(region.getBlock(pos))) return false;
    if (isAntiqueSign(static_cast<int>(region.getDimensionId()), pos)) return false;
    if (!applyToSign(region, pos)) return false;
    consumeMainhand(player, 1);
    playSound(static_cast<int>(region.getDimensionId()), center(pos), "sign.ink_sac.use", 1.0f, 0.9f);
    return true;
}

bool isAntiqueItem(ItemStackBase const& item) { return intData(item, "cm_antique", 0) != 0; }

void setAntiqueItem(ItemStackBase& item, bool on) {
    if (on) {
        setIntData(item, "cm_antique", 1);
        setLoreLine(item, antiqueLorePrefix(), antiqueLorePrefix());
    } else {
        removeData(item, "cm_antique");
        setLoreLine(item, antiqueLorePrefix(), "");
    }
}

bool useInAir(Player& player) {
    if (!config().antiqueInk.enabled) return false;
    ItemStack other = player.getOffhandSlot();
    if (!isItem(other, "minecraft:written_book") && !isItem(other, "minecraft:filled_map")) return false;
    if (isAntiqueItem(other)) return false;
    setAntiqueItem(other, true);
    setOffhand(player, other);
    consumeMainhand(player, 1);
    playSound(static_cast<int>(player.getDimensionId()), player.getPosition(), "sign.ink_sac.use", 1.0f, 0.9f);
    log().info("antique ink applied to {} by {}", itemId(other), player.getRealName());
    return true;
}

} // namespace cm::feature::tool::antique
