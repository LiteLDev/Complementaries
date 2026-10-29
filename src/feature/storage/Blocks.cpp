// Sack and present blocks: placement, removal (drops with contents), falling sacks, the sack screen,
// the present form, trapped presents, dyeing and the overencumbered penalty.

#include <algorithm>
#include <cctype>

#include "ll/api/form/CustomForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/storage/Features.h"
#include "feature/storage/View.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::storage {

namespace {

/// A one-slot container for running an item's dispenser behaviour (trapped presents).
class DispenseContainer : public Container {
public:
    explicit DispenseContainer(ItemStack item) : Container(SharedTypes::Legacy::ContainerType::Dispenser), mItem(std::move(item)) {}
    ItemStack const& getItem(int) const override { return mItem; }
    void             setItem(int, ItemStack const& item) override { mItem = item; }
    int              getContainerSize() const override { return 1; }
    int              getMaxStackSize() const override { return 64; }
    void             startOpen(Actor&) override {}
    void serverInitItemStackIds(int, int, std::function<void(int, ItemStack const&)>) override {}

    ItemStack mItem;
};

Vec3 center(core::GlobalPos const& p, float dy = 0.5f) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + dy, static_cast<float>(p.z) + 0.5f};
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string blockTitle(Player& player, Kind kind) {
    switch (kind) {
    case Kind::Sack:
        return trFor(player, "block.supplementaries.sack", "Sack");
    case Kind::TrappedPresent:
        return trFor(player, "block.supplementaries.trapped_present", "Trapped Present");
    default:
        return trFor(player, "block.supplementaries.present", "Present");
    }
}

bool presentAllows(ItemStack const& s) { return kindOf(itemId(s)) != Kind::Present && kindOf(itemId(s)) != Kind::TrappedPresent; }

bool sackAllows(ItemStack const& s) {
    auto id = itemId(s);
    return id != kSack && id.find("shulker_box") == std::string::npos && !id.ends_with("bundle");
}

/// The block at `pos` if it is one of ours, else nullptr.
Block const* ourBlock(core::GlobalPos const& pos, BlockSource** regionOut = nullptr) {
    auto* region = regionOf(pos.dim);
    if (!region) return nullptr;
    auto bp = game::blockPos(pos);
    if (!region->hasChunksAt(bp, 0, false)) return nullptr;
    auto const& b = region->getBlock(bp);
    if (kindOf(b) == Kind::None) return nullptr;
    if (regionOut) *regionOut = region;
    return &b;
}

/// Our own block moves/changes (falling sack) must not count as a removal.
bool gMoving = false;

void showPresentForm(Player& player, core::GlobalPos const& pos, Kind kind) {
    auto rec      = blockRecordOrNew(pos);
    auto contents = loadContents(rec.uid, 1);
    auto content  = isEmpty(contents[0]) ? trFor(player, "cm.storage.present.empty", "Empty")
                                         : stackName(contents[0]) + " x" + std::to_string(countOf(contents[0]));
    ll::form::CustomForm form;
    form.setTitle(blockTitle(player, kind));
    form.appendLabel(trFor(player, "cm.storage.present.contents", "Content: {}", content));
    form.appendDropdown(
        "item",
        trFor(player, "cm.storage.present.item", "Item"),
        {trFor(player, "cm.storage.present.keep", "Keep the content"),
         trFor(player, "cm.storage.present.put", "Put the item you hold"),
         trFor(player, "cm.storage.present.take", "Take the item out")}
    );
    if (kind == Kind::Present) {
        form.appendInput(
            "recipient",
            trFor(player, "gui.supplementaries.present.send", "To:"),
            trFor(player, "message.supplementaries.present.public", "Public"),
            rec.recipient
        );
    }
    form.appendInput("description", trFor(player, "gui.supplementaries.present.description", "Description"), "", rec.description);
    form.appendToggle("pack", trFor(player, "gui.supplementaries.present.pack", "Pack"), false);
    std::string uid = rec.uid;
    form.sendTo(player, [pos, uid](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) return;
        auto cur = loadBlockRecord(pos);
        if (!ourBlock(pos) || !cur || cur->uid != uid || cur->packed) return; // changed meanwhile
        auto get = [&](char const* key) -> std::string {
            auto it = result->find(key);
            if (it == result->end()) return {};
            if (auto const* s = std::get_if<std::string>(&it->second)) return *s;
            return {};
        };
        auto num = [&](char const* key) -> uint64 {
            auto it = result->find(key);
            if (it == result->end()) return 0;
            if (auto const* n = std::get_if<uint64>(&it->second)) return *n;
            if (auto const* d = std::get_if<double>(&it->second)) return static_cast<uint64>(*d);
            return 0;
        };
        applyPresentForm(
            p,
            pos,
            static_cast<PresentAction>(std::min<uint64>(num("item"), 2)),
            result->contains("recipient") ? get("recipient") : cur->recipient,
            get("description"),
            num("pack") != 0
        );
    });
}

} // namespace

bool movingBlocks() { return gMoving; }

void removeQuietly(core::GlobalPos const& pos) {
    if (auto rec = loadBlockRecord(pos)) {
        view::close(rec->uid);
        eraseContents(rec->uid);
        eraseBlockRecord(pos);
    }
    BlockSource* region = nullptr;
    if (!ourBlock(pos, &region)) return;
    gMoving = true;
    game::clearBlock(*region, game::blockPos(pos));
    gMoving = false;
}

// ---- items <-> blocks ------------------------------------------------------------------------------

ItemStack blockItem(std::string const& id, BlockRecord const* rec) {
    auto item = makeStack(id);
    if (!rec) return item;
    bool empty = contentsEmpty(loadContents(rec->uid, kindOf(id) == Kind::Sack ? sackSlots() : 1));
    if (empty && !rec->packed && rec->recipient.empty() && rec->description.empty()) {
        eraseContents(rec->uid);
        return item;
    }
    setTagString(item, "uid", rec->uid);
    if (kindOf(id) != Kind::Sack) {
        setTagInt(item, "packed", rec->packed ? 1 : 0);
        if (!rec->recipient.empty()) setTagString(item, "recipient", rec->recipient);
        if (!rec->sender.empty()) setTagString(item, "sender", rec->sender);
        if (!rec->description.empty()) setTagString(item, "description", rec->description);
    }
    updateLore(item);
    return item;
}

void onBlockPlaced(core::GlobalPos const& pos, ItemStack const& item) {
    BlockRecord rec;
    rec.uid         = itemUid(item);
    rec.packed      = tagInt(item, "packed", 0) != 0;
    rec.recipient   = tagString(item, "recipient");
    rec.sender      = tagString(item, "sender");
    rec.description = tagString(item, "description");
    if (rec.uid.empty()) {
        eraseBlockRecord(pos); // a stale record of an earlier block here
    } else {
        saveBlockRecord(pos, rec);
    }
    BlockSource* region = nullptr;
    if (auto const* b = ourBlock(pos, &region)) {
        if (kindOf(*b) != Kind::Sack && boolState(*b, kStatePacked) != rec.packed) {
            gMoving = true;
            setBoolState(*region, game::blockPos(pos), kStatePacked, rec.packed);
            gMoving = false;
        }
    }
}

void onBlockGone(core::GlobalPos const& pos, std::string const& oldId, std::optional<BlockRecord> const& rec, bool creativeBreak) {
    if (rec) view::close(rec->uid);
    ItemStack drop = blockItem(oldId, rec ? &*rec : nullptr);
    bool      bare = itemUid(drop).empty();
    if (creativeBreak && bare) return; // creative: only filled blocks drop (shulker-box rule)
    if (auto* region = regionOf(pos.dim)) spawnItem(*region, drop, center(pos));
    log().info("storage: {} at {} dropped{}", oldId, pos.key(), bare ? "" : " with contents");
}

void onBlockLanded(core::GlobalPos const& pos, std::optional<BlockRecord> const& rec) {
    if (rec) saveBlockRecord(pos, *rec);
    else eraseBlockRecord(pos);
    auto const* b = ourBlock(pos);
    if (!b) return;
    log().info("storage: {} pushed to {}", b->getTypeName(), pos.key());
    if (kindOf(*b) == Kind::Sack) checkFall(pos);
}

core::GlobalPos checkFall(core::GlobalPos const& pos) {
    BlockSource* region = nullptr;
    auto const*  b      = ourBlock(pos, &region);
    if (!b || kindOf(*b) != Kind::Sack) return pos;
    BlockPos bp = game::blockPos(pos);
    BlockPos to = bp;
    while (to.y - 1 >= region->getMinHeight() && game::replaceable(*region, BlockPos{to.x, to.y - 1, to.z})) --to.y;
    if (to.y == bp.y) return pos;
    auto rec = loadBlockRecord(pos);
    if (rec) view::close(rec->uid);
    auto const* sack = game::blockWith(kSack, {{kStateOpen, 0}});
    if (!sack) return pos;
    gMoving = true;
    game::setBlock(*region, to, *sack);
    game::clearBlock(*region, bp);
    gMoving = false;
    core::GlobalPos np{pos.dim, to.x, to.y, to.z};
    if (rec) {
        eraseBlockRecord(pos);
        saveBlockRecord(np, *rec);
    }
    log().info("storage: sack fell from {} to {}", pos.key(), np.key());
    // a sack stacked on this one falls after it
    checkFall(core::GlobalPos{pos.dim, pos.x, pos.y + 1, pos.z});
    return np;
}

// ---- sack ------------------------------------------------------------------------------------------

bool openSack(Player& player, core::GlobalPos const& pos) {
    BlockSource* region = nullptr;
    if (!ourBlock(pos, &region)) return false;
    auto            rec = blockRecordOrNew(pos);
    view::Options   o;
    o.uid     = rec.uid;
    o.size    = sackSlots();
    o.title   = blockTitle(player, Kind::Sack);
    o.allowed = sackAllows;
    o.block   = pos;
    o.onClosed = [pos, uid = rec.uid] {
        BlockSource* r = nullptr;
        if (auto const* b = ourBlock(pos, &r); b && kindOf(*b) == Kind::Sack) {
            setBoolState(*r, game::blockPos(pos), kStateOpen, false);
            playSound(*r, center(pos), "armor.equip_leather");
            int stacks = 0;
            for (auto const& s : loadContents(uid, sackSlots())) stacks += isEmpty(s) ? 0 : 1;
            log().info("storage: the sack at {} was closed ({} stacks inside)", pos.key(), stacks);
        }
    };
    if (!view::open(player, std::move(o))) return false;
    setBoolState(*region, game::blockPos(pos), kStateOpen, true);
    playSound(*region, center(pos), "armor.equip_leather");
    log().info("storage: {} opened the sack at {}", player.getRealName(), pos.key());
    return true;
}

int sackCount(Player& player) {
    int n = 0;
    forEachSlot(player, [&n](int, ItemStack const& s) {
        if (itemId(s) == kSack) n += countOf(s);
        return false;
    });
    return n;
}

void overencumberedTick(Player& player) {
    auto const& c = config();
    if (!c.sackPenalty || c.sackIncrement <= 0) return;
    if (player.isCreative() || player.isSpectator()) return;
    int n   = sackCount(player);
    int inc = std::clamp(c.sackIncrement, 1, 50);
    if (n <= inc) return;
    int               amp = (n - 1) / inc - 1;
    MobEffectInstance eff(2u, EffectDuration{200}); // slowness
    eff.mAmplifier      = std::max(amp, 0);
    eff.mEffectVisible  = false;
    eff.mAmbient        = false;
    player.addEffect(eff);
}

// ---- presents --------------------------------------------------------------------------------------

bool canOpenPresent(Player& player, BlockRecord const& rec) {
    if (!rec.packed || rec.recipient.empty() || player.isCreative()) return true;
    return lower(rec.recipient) == lower(player.getRealName());
}

void applyPresentForm(Player& player, core::GlobalPos const& pos, PresentAction action, std::string const& recipient,
                      std::string const& description, bool pack) {
    BlockSource* region = nullptr;
    auto const*  b      = ourBlock(pos, &region);
    if (!b) return;
    Kind kind = kindOf(*b);
    if (kind == Kind::Sack) return;
    auto rec      = blockRecordOrNew(pos);
    auto contents = loadContents(rec.uid, 1);
    if (action == PresentAction::Put) {
        ItemStack held = player.getSelectedItem();
        if (!isEmpty(held) && presentAllows(held)) {
            if (!isEmpty(contents[0])) giveOrDrop(player, contents[0]);
            contents[0] = held;
            player.setSelectedItem(ItemStack());
            player.sendInventory(false);
        }
    } else if (action == PresentAction::Take && !isEmpty(contents[0])) {
        giveOrDrop(player, contents[0]);
        contents[0] = ItemStack();
    }
    saveContents(rec.uid, contents);
    rec.recipient   = kind == Kind::Present ? recipient : std::string();
    rec.description = description;
    if (pack) {
        rec.packed = true;
        rec.sender = player.getRealName();
        setBoolState(*region, game::blockPos(pos), kStatePacked, true);
        playSound(*region, center(pos), "item.book.page_turn");
        log().info("storage: {} packed the present at {}", player.getRealName(), pos.key());
    }
    saveBlockRecord(pos, rec);
}

void triggerTrappedPresent(core::GlobalPos const& pos) {
    BlockSource* region = nullptr;
    auto const*  b      = ourBlock(pos, &region);
    if (!b || kindOf(*b) != Kind::TrappedPresent) return;
    auto rec      = blockRecordOrNew(pos);
    auto contents = loadContents(rec.uid, 1);
    rec.packed    = false;
    saveBlockRecord(pos, rec);
    setBoolState(*region, game::blockPos(pos), kStatePacked, false);
    playSound(*region, center(pos), "random.click");
    if (!isEmpty(contents[0])) {
        Vec3              out = center(pos, 1.2f);
        DispenseContainer c(contents[0]);
        auto const*       item = contents[0].mItem.get();
        // one item is activated like a dispenser would; whatever is left pops out: the present ends up empty
        if (item) item->dispense(*region, c, 0, out, 1 /* up */);
        spawnItem(*region, c.mItem, out);
        eraseContents(rec.uid);
    }
    log().info("storage: trapped present at {} triggered", pos.key());
}

bool dyePresent(Player& player, core::GlobalPos const& pos, std::string const& color) {
    BlockSource* region = nullptr;
    auto const*  b      = ourBlock(pos, &region);
    if (!b) return false;
    auto kind = kindOf(*b);
    if (kind == Kind::Sack) return false;
    auto newId = presentId(kind == Kind::TrappedPresent, color);
    if (newId == b->getTypeName()) return false;
    auto const* nb = game::blockWith(newId, {{kStatePacked, boolState(*b, kStatePacked) ? 1 : 0}});
    if (!nb) return false;
    gMoving = true;
    game::setBlock(*region, game::blockPos(pos), *nb);
    gMoving = false;
    if (ItemStack held = player.getSelectedItem(); !player.isCreative() && itemId(held).ends_with("_dye")) {
        held.remove(1);
        player.setSelectedItem(held.mCount > 0 ? held : ItemStack());
        player.sendInventory(false);
    }
    playSound(*region, center(pos), "dye.use");
    return true;
}

void interactBlock(Player& player, core::GlobalPos const& pos) {
    auto const* b = ourBlock(pos);
    if (!b) return;
    Kind kind = kindOf(*b);
    if (kind == Kind::Sack) {
        openSack(player, pos);
        return;
    }
    auto held = itemId(player.getSelectedItem());
    if (config().presentsAllowDyeing && held.starts_with("minecraft:") && held.ends_with("_dye")) {
        auto color = held.substr(10, held.size() - 10 - 4);
        for (auto c : kColors) {
            if (c == color) {
                dyePresent(player, pos, color);
                return;
            }
        }
    }
    bool hadRecord = loadBlockRecord(pos).has_value();
    auto rec       = blockRecordOrNew(pos);
    if (!hadRecord && boolState(*b, kStatePacked)) {
        // a packed present placed without its item (command, structure): a packed public present
        rec.packed = true;
        saveBlockRecord(pos, rec);
    }
    if (!rec.packed) {
        showPresentForm(player, pos, kind);
        return;
    }
    if (kind == Kind::TrappedPresent) {
        triggerTrappedPresent(pos);
        return;
    }
    if (!canOpenPresent(player, rec)) {
        actionbar(player, trFor(player, "message.supplementaries.present.info", "This present is for {}", rec.recipient));
        return;
    }
    BlockSource* region = nullptr;
    ourBlock(pos, &region);
    rec.packed = false;
    saveBlockRecord(pos, rec);
    setBoolState(*region, game::blockPos(pos), kStatePacked, false);
    playSound(*region, center(pos), "item.book.page_turn");
    if (!rec.sender.empty()) actionbar(player, trFor(player, "message.supplementaries.present.from", "From {}", rec.sender));
    log().info("storage: {} opened the present at {}", player.getRealName(), pos.key());
    showPresentForm(player, pos, kind);
}

} // namespace cm::feature::storage
