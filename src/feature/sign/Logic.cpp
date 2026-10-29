#include "feature/sign/Logic.h"

#include <cmath>
#include <variant>

#include "ll/api/form/CustomForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "feature/sign/SignGame.h"
#include "feature/sign/Store.h"
#include "game/Blocks.h"
#include "feature/sign/SignConfig.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::sign::logic {

namespace {
Store& store() { return Store::get(); }

Vec3 centerOf(std::string const& key) {
    auto p = parsePosKey(key);
    if (!p) return Vec3{0, 0, 0};
    return Vec3{p->x + 0.5f, p->y + 0.5f, p->z + 0.5f};
}
int dimOf(std::string const& key) {
    auto p = parsePosKey(key);
    return p ? p->dim : 0;
}

/// The block at `key` is `id` (and loaded).
bool blockIs(std::string const& key, std::string_view id) {
    auto p = parsePosKey(key);
    if (!p) return false;
    auto* region = regionOf(p->dim);
    return region && game::blockIdAt(*region, BlockPos{p->x, p->y, p->z}) == id;
}

/// The arm to edit, or nullptr when missing / waxed.
Arm* editableArm(Post& post, bool upper) {
    if (post.waxed) return nullptr;
    Arm& a = post.arm(upper);
    return a.active ? &a : nullptr;
}
} // namespace

// ---- sign posts ---------------------------------------------------------------------------------
bool addArm(std::string const& key, int wood, bool upper, float yaw, bool left) {
    auto p = parsePosKey(key);
    if (!p) return false;
    auto* region = regionOf(p->dim);
    if (!region || !isPostBlock(game::blockIdAt(*region, BlockPos{p->x, p->y, p->z}))) return false;
    Post post;
    if (auto* existing = store().post(key)) post = *existing;
    if (post.waxed) return false;
    if (post.arm(upper).active) upper = !upper;
    if (post.arm(upper).active) return false;
    Arm& a   = post.arm(upper);
    a        = Arm{};
    a.active = true;
    a.wood   = wood;
    a.yaw    = normalizeYaw(yaw);
    a.left   = left;
    store().putPost(key, std::move(post));
    playSound(p->dim, centerOf(key), "dig.wood");
    log().info("sign: added a {} sign post arm at {}", kWoods[static_cast<std::size_t>(wood)], key);
    return true;
}

std::optional<bool> pickArm(std::string const& key, double relY) {
    auto* post = store().post(key);
    if (!post || post->empty()) return std::nullopt;
    bool upper = relY >= 0.5;
    if (!post->arm(upper).active) upper = !upper;
    return upper;
}

bool setArmText(std::string const& key, bool upper, std::string const& text) {
    auto* stored = store().post(key);
    if (!stored) return false;
    Post post = *stored;
    Arm* a    = editableArm(post, upper);
    if (!a) return false;
    a->text = fitWidth(stripFormatting(text), kSignPostLineWidth);
    store().putPost(key, std::move(post));
    log().info("sign: sign post text at {} set to '{}'", key, store().post(key)->arm(upper).text);
    return true;
}

bool pointArm(std::string const& key, bool upper, double targetX, double targetZ) {
    auto* stored = store().post(key);
    if (!stored) return false;
    Post post = *stored;
    Arm* a    = editableArm(post, upper);
    if (!a) return false;
    auto c = centerOf(key);
    a->yaw = yawTowards(c.x, c.z, targetX, targetZ);
    store().putPost(key, std::move(post));
    log().info("sign: sign post arm at {} points to {} {} (yaw {})", key, targetX, targetZ, store().post(key)->arm(upper).yaw);
    return true;
}

bool flipArm(std::string const& key, bool upper) {
    auto* stored = store().post(key);
    if (!stored) return false;
    Post post = *stored;
    Arm* a    = editableArm(post, upper);
    if (!a) return false;
    a->left = !a->left;
    a->yaw  = normalizeYaw(a->yaw + 180.0f);
    store().putPost(key, std::move(post));
    return true;
}

bool dyeArm(std::string const& key, bool upper, int dye) {
    auto* stored = store().post(key);
    if (!stored) return false;
    Post post = *stored;
    Arm* a    = editableArm(post, upper);
    if (!a || a->text.empty() || a->color == dye) return false;
    a->color = dye;
    store().putPost(key, std::move(post));
    playSound(dimOf(key), centerOf(key), "sign.dye.use");
    return true;
}

bool glowArm(std::string const& key, bool upper, bool glowing) {
    auto* stored = store().post(key);
    if (!stored) return false;
    Post post = *stored;
    Arm* a    = editableArm(post, upper);
    if (!a || a->text.empty() || a->glowing == glowing) return false;
    a->glowing = glowing;
    store().putPost(key, std::move(post));
    playSound(dimOf(key), centerOf(key), glowing ? "sign.glow_ink_sac.use" : "sign.ink_sac.use");
    return true;
}

bool waxPost(std::string const& key, bool waxed) {
    auto* stored = store().post(key);
    if (!stored || stored->waxed == waxed) return false;
    Post post  = *stored;
    post.waxed = waxed;
    store().putPost(key, std::move(post));
    playSound(dimOf(key), centerOf(key), waxed ? "copper.wax.on" : "copper.wax.off");
    return true;
}

// ---- notice boards ------------------------------------------------------------------------------
void refreshBoardBlock(std::string const& key) {
    auto p = parsePosKey(key);
    if (!p) return;
    auto* region = regionOf(p->dim);
    if (!region) return;
    BlockPos pos{p->x, p->y, p->z};
    if (game::blockIdAt(*region, pos) != kNoticeBoard) return;
    auto const* board   = store().board(key);
    bool        hasItem = board && !board->item.empty();
    bool        powered = board && board->powered;
    auto const* block   = game::blockWith(
        kNoticeBoard,
        {{kFacingState, blockFacing(*region, pos)}, {kHasBookState, hasItem ? 1 : 0}, {kPoweredState, powered ? 1 : 0}}
    );
    if (block && block != &region->getBlock(pos)) game::setBlock(*region, pos, *block);
}

bool boardInsert(std::string const& key, ItemStack const& item) {
    if (item.isNull() || !blockIs(key, kNoticeBoard)) return false;
    Board board;
    if (auto* b = store().board(key)) board = *b;
    if (!board.item.empty()) return false;
    ItemStack one = item;
    one.set(1);
    board.item = itemToSnbt(one);
    board.page = 0;
    store().putBoard(key, std::move(board));
    refreshBoardBlock(key);
    playSound(dimOf(key), centerOf(key), "block.itemframe.add_item");
    log().info("sign: notice board {} holds {}", key, itemId(item));
    return true;
}

ItemStack boardTake(std::string const& key) {
    auto* b = store().board(key);
    if (!b || b->item.empty()) return {};
    Board board = *b;
    auto  item  = itemFromSnbt(board.item);
    board.item.clear();
    board.page = 0;
    store().putBoard(key, std::move(board));
    refreshBoardBlock(key);
    playSound(dimOf(key), centerOf(key), "block.itemframe.remove_item");
    log().info("sign: notice board {} emptied", key);
    return item;
}

bool boardNextPage(std::string const& key) {
    auto* b = store().board(key);
    if (!b) return false;
    int pages = 0;
    (void)store().boardLines(*b, &pages);
    if (pages < 2) return false;
    Board board = *b;
    board.page  = (board.page + 1) % pages;
    store().putBoard(key, std::move(board));
    playSound(dimOf(key), centerOf(key), "item.book.page_turn");
    log().info("sign: notice board {} turned to page {}", key, store().board(key)->page + 1);
    return true;
}

bool boardDye(std::string const& key, int dye) {
    auto* b = store().board(key);
    if (!b || b->item.empty() || b->color == dye) return false;
    Board board = *b;
    board.color = dye;
    store().putBoard(key, std::move(board));
    playSound(dimOf(key), centerOf(key), "sign.dye.use");
    return true;
}

bool boardGlow(std::string const& key, bool glowing) {
    auto* b = store().board(key);
    if (!b || b->item.empty() || b->glowing == glowing) return false;
    Board board   = *b;
    board.glowing = glowing;
    store().putBoard(key, std::move(board));
    playSound(dimOf(key), centerOf(key), glowing ? "sign.glow_ink_sac.use" : "sign.ink_sac.use");
    return true;
}

int boardSignal(std::string const& key) {
    auto* b = store().board(key);
    if (!b || b->item.empty()) return 0;
    int pages = 0;
    (void)store().boardLines(*b, &pages);
    if (pages > 0) return std::min(15, b->page % pages + 1);
    return 15;
}

// ---- doormats -----------------------------------------------------------------------------------
bool setMatLines(std::string const& key, std::array<std::string, kDoormatLines> const& lines) {
    if (!blockIs(key, kDoormat)) return false;
    Mat mat;
    if (auto* m = store().mat(key)) mat = *m;
    if (mat.waxed) return false;
    for (std::size_t i = 0; i < lines.size(); ++i) mat.lines[i] = fitWidth(stripFormatting(lines[i]), kDoormatLineWidth);
    store().putMat(key, std::move(mat));
    log().info("sign: doormat text at {} set to '{}'", key, store().mat(key)->lines[0]);
    return true;
}

bool matDye(std::string const& key, int dye) {
    auto* m = store().mat(key);
    if (!m || m->waxed || m->color == dye) return false;
    Mat mat   = *m;
    mat.color = dye;
    store().putMat(key, std::move(mat));
    playSound(dimOf(key), centerOf(key), "sign.dye.use");
    return true;
}

bool matGlow(std::string const& key, bool glowing) {
    auto* m = store().mat(key);
    if (!m || m->waxed || m->glowing == glowing) return false;
    Mat mat     = *m;
    mat.glowing = glowing;
    store().putMat(key, std::move(mat));
    playSound(dimOf(key), centerOf(key), glowing ? "sign.glow_ink_sac.use" : "sign.ink_sac.use");
    return true;
}

bool matWax(std::string const& key, bool waxed) {
    if (!blockIs(key, kDoormat)) return false;
    Mat mat;
    if (auto* m = store().mat(key)) mat = *m;
    if (mat.waxed == waxed) return false;
    mat.waxed = waxed;
    store().putMat(key, std::move(mat));
    playSound(dimOf(key), centerOf(key), waxed ? "copper.wax.on" : "copper.wax.off");
    return true;
}

// ---- screens ------------------------------------------------------------------------------------
namespace {
std::string inputText(ll::form::CustomFormResult const& result, std::string const& name, std::string const& fallback) {
    if (!result) return fallback;
    auto it = result->find(name);
    if (it == result->end()) return fallback;
    if (auto const* s = std::get_if<std::string>(&it->second)) return *s;
    return fallback;
}
} // namespace

void openPostEditor(Player& player, std::string const& key) {
    auto* post = store().post(key);
    if (!post || post->empty()) return;
    if (post->waxed) {
        player.sendMessage(trFor(player, "cm.sign.waxed", "This sign is waxed"));
        return;
    }
    ll::form::CustomForm form(trFor(player, "cm.sign.edit", "Edit Sign Message"));
    if (post->up.active) form.appendInput("up", trFor(player, "cm.sign.edit.up", "Upper sign"), "", post->up.text);
    if (post->down.active) form.appendInput("down", trFor(player, "cm.sign.edit.down", "Lower sign"), "", post->down.text);
    log().info("sign: opened the sign post editor for {} at {}", player.getRealName(), key);
    form.sendTo(player, [key](Player&, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) return;
        auto* current = Store::get().post(key);
        if (!current) return;
        if (current->up.active) setArmText(key, true, inputText(result, "up", current->up.text));
        current = Store::get().post(key);
        if (current && current->down.active) setArmText(key, false, inputText(result, "down", current->down.text));
    });
}

void openMatEditor(Player& player, std::string const& key) {
    Mat mat;
    if (auto* m = store().mat(key)) mat = *m;
    if (mat.waxed) {
        player.sendMessage(trFor(player, "cm.sign.waxed", "This sign is waxed"));
        return;
    }
    ll::form::CustomForm form(trFor(player, "cm.sign.doormat.edit", "Edit Doormat Message"));
    for (int i = 0; i < kDoormatLines; ++i) {
        form.appendInput("line" + std::to_string(i), trFor(player, "cm.sign.line", "Line {}", i + 1), "", mat.lines[static_cast<std::size_t>(i)]);
    }
    log().info("sign: opened the doormat editor for {} at {}", player.getRealName(), key);
    form.sendTo(player, [key](Player&, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) return;
        Mat current;
        if (auto* m = Store::get().mat(key)) current = *m;
        std::array<std::string, kDoormatLines> lines;
        for (int i = 0; i < kDoormatLines; ++i) {
            lines[static_cast<std::size_t>(i)] =
                inputText(result, "line" + std::to_string(i), current.lines[static_cast<std::size_t>(i)]);
        }
        setMatLines(key, lines);
    });
}

// ---- way signs ----------------------------------------------------------------------------------
std::string waySignText(WayTarget const& target, int fromX, int fromZ) {
    if (!config::get<SignConfig>().waySignDistanceText) return fitWidth(target.name, kSignPostLineWidth);
    double dx   = target.x - fromX;
    double dz   = target.z - fromZ;
    int    dist = static_cast<int>(std::lround(std::sqrt(dx * dx + dz * dz)));
    return fitWidth(tr("cm.sign.way_sign.distance", "{} {}m", target.name, dist), kSignPostLineWidth);
}

bool buildWaySign(int dim, int x, int y, int z, WayTarget const& first, std::optional<WayTarget> const& second) {
    auto* region = regionOf(dim);
    if (!region) return false;
    BlockPos pos{x, y, z};
    if (!loadedAt(*region, pos)) return false;
    auto const* fence = game::blockWith("minecraft:spruce_fence");
    if (!fence || !game::setBlock(*region, pos, *fence)) return false;
    auto key = posKey(dim, x, y, z);
    Post post;
    post.up        = Arm{true, 1, yawTowards(x + 0.5, z + 0.5, first.x + 0.5, first.z + 0.5), false, waySignText(first, x, z)};
    if (second) {
        post.down = Arm{true, 0, yawTowards(x + 0.5, z + 0.5, second->x + 0.5, second->z + 0.5), false, waySignText(*second, x, z)};
    }
    store().putPost(key, std::move(post));
    log().info("sign: way sign built at {} pointing to {} ({}, {})", key, first.name, first.x, first.z);
    return true;
}

} // namespace cm::feature::sign::logic
