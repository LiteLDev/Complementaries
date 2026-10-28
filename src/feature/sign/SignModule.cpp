// U4 `sign`: sign posts, notice boards and doormats (server side). See docs/spec/sign.md.

#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/sign/Logic.h"
#include "feature/sign/SignConfig.h"
#include "feature/sign/SignGame.h"
#include "feature/sign/SignModule.h"
#include "feature/sign/Store.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

CM_REGISTER_CONFIG("sign", cm::feature::sign::SignConfig);

namespace cm::feature::sign {

namespace {
std::vector<std::function<void()>>& deferred() {
    static std::vector<std::function<void()>> v;
    return v;
}

SignConfig const& cfg() { return config::get<SignConfig>(); }

FacingID frontFace(int facing) {
    switch (facing & 3) {
    case 0:
        return FacingID::South;
    case 1:
        return FacingID::West;
    case 2:
        return FacingID::North;
    default:
        return FacingID::East;
    }
}

bool isAxe(std::string_view id) { return id.starts_with("minecraft:") && id.ends_with("_axe"); }

Player* playerById(std::int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

/// What a click did: nothing (vanilla handles it) or handled by us (the event is cancelled).
struct Click {
    std::int64_t player;
    int          dim;
    BlockPos     pos;
    FacingID     face;
    double       relX, relY, relZ;
    std::string  item;
    bool         sneaking;
    float        yaw;
};

/// Runs `fn(player)` next tick (world edits during a cancelled interaction are rolled back), only
/// if the player still holds the item they clicked with.
void defer(Click const& c, std::function<void(Player&)> fn) {
    deferred().push_back([c, fn = std::move(fn)] {
        auto* player = playerById(c.player);
        if (!player || itemId(player->getSelectedItem()) != c.item) return;
        fn(*player);
    });
}

void failSound(Click const& c) {
    deferred().push_back([c] {
        playSound(c.dim, Vec3{c.pos.x + 0.5f, c.pos.y + 0.5f, c.pos.z + 0.5f}, "sign.waxed_interact_fail");
    });
}

bool isInk(std::string_view id) { return id == "minecraft:glow_ink_sac" || id == "minecraft:ink_sac"; }

// ---- sign posts ---------------------------------------------------------------------------------
bool postClick(Click const& c) {
    auto key  = posKey(c.dim, c.pos.x, c.pos.y, c.pos.z);
    auto wood = woodOfSignPostItem(c.item);
    auto& store = Store::get();
    auto* post  = store.post(key);
    if (wood) {
        if (!cfg().signPostEnabled) return false;
        if (post && (post->waxed || (post->up.active && post->down.active))) return false;
        defer(c, [c, key, w = *wood](Player& player) {
            // the arrow points to the side of the post the player clicked
            float  rad  = c.yaw * 3.14159265f / 180.0f;
            double rx   = -std::cos(rad), rz = -std::sin(rad); // the player's right
            bool   left = (c.relX - 0.5) * rx + (c.relZ - 0.5) * rz < 0;
            float  yaw  = std::round((c.yaw + (left ? -90.0f : 90.0f)) / 22.5f) * 22.5f;
            if (!logic::addArm(key, w, c.relY >= 0.5, yaw, left)) return;
            consumeSelected(player);
            logic::openPostEditor(player, key);
        });
        return true;
    }
    if (!post || post->empty()) return false;
    auto upper = logic::pickArm(key, c.relY);
    if (!upper) return false;
    bool up = *upper;
    if (post->waxed) {
        if (isAxe(c.item)) {
            defer(c, [key](Player& p) {
                if (logic::waxPost(key, false)) damageSelected(p);
            });
            return true;
        }
        if (c.item.empty() || c.item == "minecraft:honeycomb" || dyeOfItem(c.item) || c.item.ends_with("compass") || isInk(c.item)) {
            failSound(c);
            return true;
        }
        return false;
    }
    if (c.item == "minecraft:compass" || c.item == "minecraft:lodestone_compass" || c.item == "minecraft:recovery_compass") {
        defer(c, [c, key, up](Player& p) {
            if (auto target = compassTarget(p, c.dim)) logic::pointArm(key, up, target->first, target->second);
        });
        return true;
    }
    if (auto dye = dyeOfItem(c.item)) {
        defer(c, [key, up, d = *dye](Player& p) {
            if (logic::dyeArm(key, up, d)) consumeSelected(p);
        });
        return true;
    }
    if (isInk(c.item)) {
        bool glow = c.item == "minecraft:glow_ink_sac";
        defer(c, [key, up, glow](Player& p) {
            if (logic::glowArm(key, up, glow)) consumeSelected(p);
        });
        return true;
    }
    if (c.item == "minecraft:honeycomb") {
        defer(c, [key](Player& p) {
            if (logic::waxPost(key, true)) consumeSelected(p);
        });
        return true;
    }
    if (c.item.empty()) {
        if (c.sneaking) defer(c, [key, up](Player&) { logic::flipArm(key, up); });
        else defer(c, [key](Player& p) { logic::openPostEditor(p, key); });
        return true;
    }
    return false;
}

// ---- notice boards ------------------------------------------------------------------------------
bool boardClick(Click const& c, int facing) {
    if (!cfg().noticeBoardEnabled) return false;
    if (c.face != frontFace(facing)) return false;
    auto  key   = posKey(c.dim, c.pos.x, c.pos.y, c.pos.z);
    auto* board = Store::get().board(key);
    bool  full  = board && !board->item.empty();
    if (!c.item.empty()) {
        if (full) {
            if (auto dye = dyeOfItem(c.item)) {
                defer(c, [key, d = *dye](Player& p) {
                    if (logic::boardDye(key, d)) consumeSelected(p);
                });
                return true;
            }
            if (isInk(c.item)) {
                bool glow = c.item == "minecraft:glow_ink_sac";
                defer(c, [key, glow](Player& p) {
                    if (logic::boardGlow(key, glow)) consumeSelected(p);
                });
                return true;
            }
            return false;
        }
        if (!cfg().noticeBoardAcceptsAnyItem && !isBoardItem(c.item)) return false;
        defer(c, [key](Player& p) {
            if (logic::boardInsert(key, p.getSelectedItem())) consumeSelected(p);
        });
        return true;
    }
    if (!full) return false;
    defer(c, [c, key](Player& player) {
        if (!c.sneaking && logic::boardNextPage(key)) return;
        auto item = logic::boardTake(key);
        if (item.isNull()) return;
        if (player.getSelectedItem().isNull()) {
            player.setSelectedItem(item);
            player.refreshInventory();
        } else if (!player.addAndRefresh(item)) {
            if (auto* region = regionOf(c.dim)) dropItem(*region, player.getPosition(), item);
        }
    });
    return true;
}

// ---- doormats -----------------------------------------------------------------------------------
bool matClick(Click const& c) {
    if (!cfg().doormatEnabled) return false;
    auto key = posKey(c.dim, c.pos.x, c.pos.y, c.pos.z);
    auto* mat = Store::get().mat(key);
    if (mat && mat->waxed) {
        if (isAxe(c.item)) {
            defer(c, [key](Player& p) {
                if (logic::matWax(key, false)) damageSelected(p);
            });
            return true;
        }
        if (c.item.empty()) {
            failSound(c);
            return true;
        }
        return false;
    }
    if (auto dye = dyeOfItem(c.item)) {
        defer(c, [key, d = *dye](Player& p) {
            if (logic::matDye(key, d)) consumeSelected(p);
        });
        return true;
    }
    if (isInk(c.item)) {
        bool glow = c.item == "minecraft:glow_ink_sac";
        defer(c, [key, glow](Player& p) {
            if (logic::matGlow(key, glow)) consumeSelected(p);
        });
        return true;
    }
    if (c.item == "minecraft:honeycomb") {
        defer(c, [key](Player& p) {
            if (logic::matWax(key, true)) consumeSelected(p);
        });
        return true;
    }
    if (c.item.empty()) {
        defer(c, [key](Player& p) { logic::openMatEditor(p, key); });
        return true;
    }
    return false;
}
} // namespace

std::optional<std::pair<double, double>> compassTarget(Player& player, int dim) {
    auto id = itemId(player.getSelectedItem());
    if (id == "minecraft:lodestone_compass") {
        auto t = lodestoneOf(player.getSelectedItem(), dim);
        if (t) return std::pair{t->first + 0.5, t->second + 0.5};
        return std::nullopt;
    }
    if (id == "minecraft:recovery_compass") {
        auto deathDim = player.getLastDeathDimension();
        auto deathPos = player.getLastDeathPos();
        if (!deathPos || !deathDim || static_cast<int>(*deathDim) != dim) return std::nullopt;
        return std::pair{deathPos->x + 0.5, deathPos->z + 0.5};
    }
    if (dim != 0) return std::nullopt; // the needle spins outside the overworld
    auto spawn = player.getLevel().getSharedSpawnPos();
    return std::pair{spawn.x + 0.5, spawn.z + 0.5};
}

// ---- module -------------------------------------------------------------------------------------
SignModule& SignModule::get() {
    static SignModule instance;
    return instance;
}

void SignModule::install() {
    auto& bus = ll::event::EventBus::getInstance();
    game::db::onWorldReady([] { Store::get().load(); });
    game::db::onWorldClosed([] {
        Store::get().clear();
        deferred().clear();
    });
    if (game::db::available()) Store::get().load();

    mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([this](ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide()) return;
        // the client repeats the interaction while the button is held: one action per click
        auto  now  = mTick;
        auto& last = mLastClick[player.getOrCreateUniqueID().rawID];
        auto& region = player.getDimensionBlockSource();
        auto  pos    = ev.blockPos();
        if (now - last.tick < 4 && last.pos == pos) {
            auto id = game::blockIdAt(region, pos);
            if (id == kNoticeBoard || id == kDoormat || Store::get().post(posKey(player.getDimensionId(), pos.x, pos.y, pos.z)))
                ev.cancel();
            return;
        }
        Click c;
        c.player   = player.getOrCreateUniqueID().rawID;
        c.dim      = static_cast<int>(player.getDimensionId());
        c.pos      = pos;
        c.face     = ev.face();
        auto click = ev.clickPos();
        // the click position is absolute on some paths and block-relative on others
        bool absolute = std::abs(click.x - pos.x) <= 1.01 && std::abs(click.y - pos.y) <= 1.01 && std::abs(click.z - pos.z) <= 1.01
                     && (std::abs(pos.x) > 1 || std::abs(pos.y) > 1 || std::abs(pos.z) > 1);
        c.relX     = absolute ? click.x - pos.x : click.x;
        c.relY     = absolute ? click.y - pos.y : click.y;
        c.relZ     = absolute ? click.z - pos.z : click.z;
        c.item     = itemId(ev.item());
        c.sneaking = player.isSneaking();
        c.yaw      = player.getRotation().y;
        auto id    = game::blockIdAt(region, pos);
        bool handled = false;
        if (id == kNoticeBoard) handled = boardClick(c, blockFacing(region, pos));
        else if (id == kDoormat) handled = matClick(c);
        else if (isPostBlock(id)) handled = postClick(c);
        if (handled) {
            last = {now, pos};
            ev.cancel();
        }
    }));

    mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide()) return;
        auto id = game::blockId(ev.placedBlock());
        if (id != kNoticeBoard && id != kDoormat) return;
        int  facing = game::opposite(game::playerFacing(player));
        auto pos    = ev.pos();
        int  dim    = static_cast<int>(player.getDimensionId());
        deferred().push_back([dim, pos, facing, id] {
            auto* region = regionOf(dim);
            if (!region || game::blockIdAt(*region, pos) != id) return;
            auto const* block = game::blockWith(id, {{kFacingState, facing}});
            if (block) game::setBlock(*region, pos, *block);
        });
    }));

    mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide() || !player.isCreative()) return;
        auto pos = ev.pos();
        Store::get().suppressDrops(posKey(player.getDimensionId(), pos.x, pos.y, pos.z));
    }));

    mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
        auto& store = Store::get();
        if (store.postCount() == 0 && store.boardCount() == 0 && !store.hasMats()) return;
        auto& region = ev.blockSource();
        auto  pos    = ev.pos();
        auto  key    = posKey(static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z);
        if (!store.post(key) && !store.board(key) && !store.mat(key)) return;
        int  dim     = static_cast<int>(region.getDimensionId());
        auto newId   = game::blockId(ev.newBlock());
        deferred().push_back([dim, pos, newId] { Store::get().onBlockChanged(dim, pos, newId); });
    }));

    mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent&) {
        ++mTick;
        auto tasks = std::move(deferred());
        deferred().clear();
        for (auto& t : tasks) {
            try {
                t();
            } catch (std::exception const& e) {
                log().error("sign: task failed: {}", e.what());
            }
        }
        if (mTick % 2 == 0) Store::get().tickBoards();
        if (mTick % 20 == 0) Store::get().reconcile(mTick % 200 == 0);
        if (mTick % 100 == 50) tickWaySigns();
    }));
}

void SignModule::uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : mListeners) bus.removeListener(l);
    mListeners.clear();
    deferred().clear();
    mLastClick.clear();
}

} // namespace cm::feature::sign

CM_REGISTER_MODULE(cm::feature::sign::SignModule);
