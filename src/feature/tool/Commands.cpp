// `/supplementaries tool enchant <stasis|loyalty|clear> [level]` (operator): Stasis and Loyalty on
// the held slingshot / bubble blower (Bedrock has no Stasis and won't put Loyalty on them).

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"

#include "feature/tool/Tool.h"
#include "mod/Commands.h"
#include "mod/I18n.h"

namespace cm::feature::tool {

// command parameter types need external linkage
struct ToolEnchantParams {
    std::string kind;
    int         level = 1;
};

namespace {

void applyLoyalty(ItemStack& item, int level) {
    EnchantmentInstance inst{};
    inst.mEnchantType = Enchant::Type::Loyalty;
    inst.mLevel       = level;
    EnchantUtils::applyEnchant(item, inst, true);
}

/// Removes Loyalty, keeping the other enchantments.
void removeLoyalty(ItemStack& item) {
    if (!item.isEnchanted()) return;
    auto all = item.constructItemEnchantsFromUserData().getAllEnchants();
    item.removeEnchants();
    for (auto const& e : all) {
        if (e.mEnchantType == Enchant::Type::Loyalty) continue;
        EnchantUtils::applyEnchant(item, e, true);
    }
}

} // namespace

} // namespace cm::feature::tool

CM_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace cm::feature::tool;
    cmd.overload<ToolEnchantParams>().text("tool").text("enchant").required("kind").optional("level").execute(
        [](CommandOrigin const& origin, CommandOutput& output, ToolEnchantParams const& params) {
            if (!cm::commands::isAdmin(origin)) return output.error("You need operator permission.");
            auto* player = cm::commands::playerOf(origin);
            if (!player) return output.error("Only players can use this command.");
            ItemStack item = player->getSelectedItem();
            if (!isItem(item, ids::Slingshot) && !isItem(item, ids::BubbleBlower)) {
                return output.error(
                    cm::trFor(*player, "cm.tool.enchant.no_item", "Hold a slingshot or a bubble blower")
                );
            }
            int level = std::clamp(params.level, 1, 5);
            if (params.kind == "stasis") {
                setStasisLevel(item, level);
            } else if (params.kind == "loyalty") {
                removeLoyalty(item);
                applyLoyalty(item, std::min(level, 3));
            } else if (params.kind == "clear") {
                setStasisLevel(item, 0);
                removeLoyalty(item);
                setMainhand(*player, item);
                return output.success(cm::trFor(*player, "cm.tool.enchant.cleared", "Removed Loyalty and Stasis"));
            } else {
                return output.error("stasis | loyalty | clear");
            }
            setMainhand(*player, item);
            output.success(cm::trFor(*player, "cm.tool.enchant.done", "Applied {} {}", params.kind, level));
        }
    );
});
