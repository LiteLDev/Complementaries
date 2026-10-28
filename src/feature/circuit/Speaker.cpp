// Speaker Block: per-block message / mode / volume, the speaker screen (a custom form) and the
// broadcast on a rising redstone edge.

#include <chrono>
#include <map>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/circuit/Circuit.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::circuit {

namespace {

std::string dbKey(core::GlobalPos const& pos) { return "cm:circuit:speaker:" + pos.key(); }

std::map<core::GlobalPos, SpeakerData>& cache() {
    static std::map<core::GlobalPos, SpeakerData> m;
    return m;
}

/// Custom names of speaker items being placed, per player (PlayerPlacing -> PlayerPlaced).
std::unordered_map<std::int64_t, std::string>& pendingNames() {
    static std::unordered_map<std::int64_t, std::string> m;
    return m;
}

/// Last screen opening per player (the client may send several use packets for one click).
std::unordered_map<std::int64_t, std::chrono::steady_clock::time_point>& lastOpen() {
    static std::unordered_map<std::int64_t, std::chrono::steady_clock::time_point> m;
    return m;
}

ll::event::ListenerPtr gInteract;
ll::event::ListenerPtr gPlacing;
ll::event::ListenerPtr gPlaced;

std::string modeKey(int mode) {
    switch (static_cast<SpeakerMode>(mode)) {
    case SpeakerMode::Status:
        return "gui.supplementaries.speaker_block.status_message";
    case SpeakerMode::Title:
        return "gui.supplementaries.speaker_block.title_message";
    case SpeakerMode::Narrator:
        return "gui.supplementaries.speaker_block.narrator_message";
    default:
        return "gui.supplementaries.speaker_block.chat_message";
    }
}
std::string_view modeFallback(int mode) {
    switch (static_cast<SpeakerMode>(mode)) {
    case SpeakerMode::Status:
        return "Status message";
    case SpeakerMode::Title:
        return "Title";
    case SpeakerMode::Narrator:
        return "Narrator";
    default:
        return "Chat";
    }
}

void sendTitle(Player& player, SetTitlePacketPayload::TitleType type, std::string const& text) {
    SetTitlePacket pkt(type, text, std::nullopt);
    pkt.sendTo(player);
}

} // namespace

SpeakerData speakerData(core::GlobalPos const& pos) {
    auto& c = cache();
    if (auto it = c.find(pos); it != c.end()) return it->second;
    SpeakerData d;
    d.volume = config::get<CircuitConfig>().speakerDefaultVolume;
    if (auto j = game::db::load(dbKey(pos))) {
        d.message = j->value("message", "");
        d.mode    = std::clamp(j->value("mode", 0), 0, kSpeakerModeCount - 1);
        d.volume  = j->value("volume", d.volume);
        d.name    = j->value("name", "");
    }
    c[pos] = d;
    return d;
}

void setSpeakerData(core::GlobalPos const& pos, SpeakerData const& data) {
    cache()[pos] = data;
    game::db::save(
        dbKey(pos),
        {{"message", data.message}, {"mode", data.mode}, {"volume", data.volume}, {"name", data.name}}
    );
}

void eraseSpeakerData(core::GlobalPos const& pos) {
    cache().erase(pos);
    game::db::erase(dbKey(pos));
}

void clearSpeakerCache() {
    cache().clear();
    pendingNames().clear();
    lastOpen().clear();
}

int speak(BlockSource& region, BlockPos const& pos) {
    auto gp   = game::globalPos(region, pos);
    auto data = speakerData(gp);
    if (data.message.empty()) return 0;
    auto const& cfg    = config::get<CircuitConfig>();
    int         volume = clampVolume(data.volume, cfg.speakerRange);
    auto        mode   = static_cast<SpeakerMode>(data.mode);
    if (mode == SpeakerMode::Narrator && !cfg.speakerNarrator) mode = SpeakerMode::Chat;
    Vec3  center{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    int   dimId = static_cast<int>(region.getDimensionId());
    auto& level = region.getLevel();
    int   heard = 0;
    level.forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dimId) return true;
        if (p.getPosition().distanceTo(center) > static_cast<float>(volume)) return true;
        std::string name = data.name.empty() ? trFor(p, "block.supplementaries.speaker_block", "Speaker Block") : data.name;
        switch (mode) {
        case SpeakerMode::Status:
            sendTitle(p, SetTitlePacketPayload::TitleType::Actionbar, data.message);
            break;
        case SpeakerMode::Title:
            sendTitle(p, SetTitlePacketPayload::TitleType::Title, data.message);
            break;
        case SpeakerMode::Narrator: // read aloud by the client's chat text-to-speech
        case SpeakerMode::Chat:
            p.sendMessage(trFor(p, "message.supplementaries.speaker", "{0}: {1}", "§o" + name + "§r", data.message));
            break;
        }
        ++heard;
        return true;
    });
    level.spawnParticleEffect("minecraft:note_particle", Vec3{pos.x + 0.5f, pos.y + 1.2f, pos.z + 0.5f}, &region.getDimension());
    log().info("speaker at {} spoke to {} player(s)", gp.key(), heard);
    return heard;
}

void openSpeakerScreen(Player& player, core::GlobalPos const& pos) {
    auto const& cfg   = config::get<CircuitConfig>();
    auto        data  = speakerData(pos);
    int         range = std::max(cfg.speakerRange, 1);
    std::vector<std::string> modes;
    for (int i = 0; i < kSpeakerModeCount; ++i) {
        if (i == static_cast<int>(SpeakerMode::Narrator) && !cfg.speakerNarrator) break;
        modes.push_back(trFor(player, modeKey(i), modeFallback(i)));
    }
    int mode = std::min(data.mode, static_cast<int>(modes.size()) - 1);
    ll::form::CustomForm form(trFor(player, "gui.supplementaries.speaker_block.edit", "Speaker Block"));
    form.appendInput(
        "message",
        trFor(player, "gui.supplementaries.speaker_block.message", "Message"),
        "",
        data.message
    );
    form.appendDropdown("mode", trFor(player, "gui.supplementaries.speaker_block.mode", "Mode"), modes, static_cast<size_t>(mode));
    form.appendSlider(
        "volume",
        trFor(player, "gui.supplementaries.speaker_block.volume", "Volume"),
        1,
        range,
        1,
        clampVolume(data.volume, range)
    );
    form.sendTo(player, [pos](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) return;
        auto& region = p.getDimensionBlockSource();
        auto  bp     = game::blockPos(pos);
        if (static_cast<int>(region.getDimensionId()) != pos.dim || !region.hasChunksAt(bp, 0, false)) return;
        if (kindOf(region.getBlock(bp)) != Kind::Speaker) return;
        auto const& cfg  = config::get<CircuitConfig>();
        auto        data = speakerData(pos);
        if (auto it = result->find("message"); it != result->end()) {
            if (auto const* s = std::get_if<std::string>(&it->second)) {
                data.message = s->substr(0, static_cast<size_t>(std::max(cfg.speakerMaxLength, 0)));
            }
        }
        if (auto it = result->find("mode"); it != result->end()) {
            if (auto const* v = std::get_if<uint64>(&it->second)) data.mode = std::clamp(static_cast<int>(*v), 0, kSpeakerModeCount - 1);
            else if (auto const* d = std::get_if<double>(&it->second)) data.mode = std::clamp(static_cast<int>(*d), 0, kSpeakerModeCount - 1);
        }
        if (auto it = result->find("volume"); it != result->end()) {
            if (auto const* d = std::get_if<double>(&it->second)) data.volume = clampVolume(static_cast<int>(*d + 0.5), cfg.speakerRange);
            else if (auto const* v = std::get_if<uint64>(&it->second)) data.volume = clampVolume(static_cast<int>(*v), cfg.speakerRange);
        }
        setSpeakerData(pos, data);
        log().info("speaker at {} set by {}: mode={} volume={}", pos.key(), p.getRealName(), data.mode, data.volume);
    });
    log().info("speaker screen opened at {} for {}", pos.key(), player.getRealName());
}

void installSpeaker() {
    auto& bus = ll::event::EventBus::getInstance();
    gInteract = bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        auto  block  = ev.block();
        if (!block || kindOf(*block) != Kind::Speaker) return;
        if (player.isSneaking() && !ev.item().isNull()) return; // sneak + item: place / use the item
        ev.cancel();
        auto id  = player.getOrCreateUniqueID().rawID;
        auto now = std::chrono::steady_clock::now();
        if (auto it = lastOpen().find(id); it != lastOpen().end() && now - it->second < std::chrono::milliseconds(400)) {
            return;
        }
        lastOpen()[id] = now;
        openSpeakerScreen(player, game::globalPos(player.getDimensionBlockSource(), ev.blockPos()));
    });
    gPlacing = bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>([](ll::event::PlayerPlacingBlockEvent& ev) {
        auto&       player = ev.self();
        auto const& item   = player.getSelectedItem();
        if (item.isNull() || item.getTypeName() != kSpeaker) return;
        auto name = item.getCustomName();
        if (name.empty()) pendingNames().erase(player.getOrCreateUniqueID().rawID);
        else pendingNames()[player.getOrCreateUniqueID().rawID] = std::move(name);
    });
    gPlaced = bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
        auto& player = ev.self();
        auto  it     = pendingNames().find(player.getOrCreateUniqueID().rawID);
        if (it == pendingNames().end()) return;
        std::string name = std::move(it->second);
        pendingNames().erase(it);
        if (kindOf(ev.placedBlock()) != Kind::Speaker) return;
        auto gp   = game::globalPos(player.getDimensionBlockSource(), ev.pos());
        auto data = speakerData(gp);
        data.name = name;
        setSpeakerData(gp, data);
    });
}

void uninstallSpeaker() {
    auto& bus = ll::event::EventBus::getInstance();
    bus.removeListener(gInteract);
    bus.removeListener(gPlacing);
    bus.removeListener(gPlaced);
}

} // namespace cm::feature::circuit
