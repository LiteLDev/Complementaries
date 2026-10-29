// "Edit Blackboard": the edit session of a player and its server form (works on every client).

#include "feature/blackboard/Editor.h"

#include <deque>
#include <map>
#include <memory>
#include <tuple>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/ui/form/CustomForm.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/blackboard/Blackboard.h"
#include "game/Blocks.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::blackboard {

namespace {

constexpr std::size_t kHistory = 20; // Java: CircularList(20)

struct Session {
    std::int64_t                  playerKey = 0;
    std::string                   uuid;
    int                           dim = 0;
    Canvas                        canvas;
    std::vector<Pixels>           original; ///< per canvas cell
    std::vector<Pixels>           work;
    std::uint8_t                  color = 1;
    std::deque<std::vector<std::tuple<int, int, std::uint8_t>>> history;
    ll::ui::ObservableString      preview{std::string()};
    ll::ui::ObservableNumber      column{0.0, {.clientWritable = true}};
    ll::ui::ObservableNumber      row{0.0, {.clientWritable = true}};
    ll::ui::ObservableNumber      colorIndex{1.0, {.clientWritable = true}};
    std::unique_ptr<ll::ui::CustomForm> form;
    bool                          closed = false;

    [[nodiscard]] int width() const { return canvas.cols * kSize; }
    [[nodiscard]] int height() const { return canvas.rows * kSize; }
    [[nodiscard]] std::size_t cellOf(int u, int v) const {
        return static_cast<std::size_t>((u / kSize) * canvas.rows + v / kSize);
    }
    [[nodiscard]] bool hasBoard(int u, int v) const { return canvas.cells[cellOf(u, v)].has_value(); }
    [[nodiscard]] std::uint8_t pixel(int u, int v) const {
        return work[cellOf(u, v)][static_cast<std::size_t>((v % kSize) * kSize + u % kSize)];
    }
    void setPixel(int u, int v, std::uint8_t c, std::vector<std::tuple<int, int, std::uint8_t>>& step) {
        if (!hasBoard(u, v)) return;
        auto& slot = work[cellOf(u, v)][static_cast<std::size_t>((v % kSize) * kSize + u % kSize)];
        if (slot == c) return;
        step.emplace_back(u, v, slot);
        slot = c;
    }
    void commit(std::vector<std::tuple<int, int, std::uint8_t>> step) {
        if (step.empty()) return;
        history.push_back(std::move(step));
        while (history.size() > kHistory) history.pop_front();
    }
    [[nodiscard]] std::pair<int, int> cursor() const {
        int u = std::clamp(static_cast<int>(column.getData()), 0, width() - 1);
        int v = std::clamp(static_cast<int>(row.getData()), 0, height() - 1);
        return {u, v};
    }
};

std::map<std::int64_t, std::shared_ptr<Session>>& sessions() {
    static std::map<std::int64_t, std::shared_ptr<Session>> s;
    return s;
}

/// The board under the cursor as 16 lines of coloured block characters (a 48 column canvas is
/// wider than the form), with the cursor in red and the board's place in the canvas on top.
std::string renderPreview(Session const& s) {
    auto [cu, cv] = s.cursor();
    int         u0 = cu / kSize * kSize, v0 = cv / kSize * kSize;
    std::string out;
    if (s.canvas.cols > 1 || s.canvas.rows > 1) {
        out += std::format("§7{} / {}  ·  {} / {}\n", cu / kSize + 1, s.canvas.cols, cv / kSize + 1, s.canvas.rows);
    }
    for (int v = v0; v < v0 + kSize; ++v) {
        int last = -1;
        for (int u = u0; u < u0 + kSize; ++u) {
            int         code  = s.hasBoard(u, v) ? s.pixel(u, v) : 100;
            char const* glyph = code == 100 ? "\u2591" : "\u2588";
            if (u == cu && v == cv) {
                code  = 101;
                glyph = "\u2593";
            }
            if (code != last) {
                out += code == 100 ? "§8" : code == 101 ? "§c" : std::string(formatCodeOf(static_cast<std::uint8_t>(code)));
                last = code;
            }
            out += glyph;
        }
        out += '\n';
    }
    return out;
}
void refresh(Session& s) { s.preview.setData(renderPreview(s)); }

void applySession(Session& s) {
    auto* region = regionOf(s.dim);
    if (!region) return;
    std::optional<BlockPos> soundAt;
    bool                    colored = config().colored_blackboard;
    for (std::size_t i = 0; i < s.canvas.cells.size(); ++i) {
        auto const& cell = s.canvas.cells[i];
        if (!cell || s.work[i] == s.original[i]) continue;
        auto pos = game::blockPos(*cell);
        if (!region->hasChunksAt(pos, 0, false) || !isBlackboard(region->getBlock(pos))) continue;
        Board board = boardAt(*cell);
        if (board.waxed) continue;
        // only the pixels this session changed: edits made meanwhile (dye, soap, ...) survive
        Pixels px = board.px;
        bool   ok = true;
        for (std::size_t k = 0; k < px.size(); ++k) {
            if (s.work[i][k] == s.original[i][k]) continue;
            if (!colored && s.work[i][k] > 1) ok = false; // Java refuses coloured pixels when colours are off
            px[k] = s.work[i][k];
        }
        if (!ok || board.px == px) continue;
        board.px = px;
        setBoard(*region, pos, board);
        soundAt = pos;
    }
    if (soundAt) {
        playSound(*region, *soundAt, "complementaries.block.blackboard.draw_complete");
        log().info("blackboard: edit applied at {}", game::globalPos(s.dim, *soundAt).key());
    }
}
void endSession(std::int64_t key, bool apply) {
    auto& all = sessions();
    auto  it  = all.find(key);
    if (it == all.end()) return;
    auto s = it->second;
    all.erase(it);
    if (s->closed) return;
    s->closed = true;
    if (apply) applySession(*s);
}

void showForm(Player& player, std::shared_ptr<Session> const& s) {
    std::weak_ptr<Session> weak = s;
    auto act                    = [weak](auto fn) {
        return [weak, fn]() {
            auto s = weak.lock();
            if (!s || s->closed) return;
            fn(*s);
            refresh(*s);
        };
    };
    s->form = std::make_unique<ll::ui::CustomForm>(player, trFor(player, "gui.supplementaries.blackboard.edit", "Edit Blackboard"));
    auto& f = *s->form;
    f.label(s->preview);
    f.slider(trFor(player, "cm.blackboard.column", "Column"), s->column, 0.0, static_cast<double>(s->width() - 1), {.step = 1.0});
    f.slider(trFor(player, "cm.blackboard.row", "Row"), s->row, 0.0, static_cast<double>(s->height() - 1), {.step = 1.0});
    if (config().colored_blackboard) {
        std::vector<ll::ui::DropdownItemData> items;
        for (int c = 0; c < 16; ++c) {
            items.push_back({trFor(player, "cm.blackboard.color." + std::to_string(c), "Colour " + std::to_string(c)), static_cast<double>(c), std::nullopt});
        }
        f.dropdown(trFor(player, "cm.blackboard.color", "Colour"), s->colorIndex, std::move(items));
    }
    f.button(trFor(player, "cm.blackboard.draw", "Draw"), act([](Session& s) {
        auto [u, v] = s.cursor();
        auto color  = config().colored_blackboard ? static_cast<std::uint8_t>(static_cast<int>(s.colorIndex.getData()) & 15)
                                                  : std::uint8_t{1};
        std::vector<std::tuple<int, int, std::uint8_t>> step;
        // Java click: the selected colour, or blank when the pixel already has it
        s.setPixel(u, v, s.pixel(u, v) == color ? std::uint8_t{0} : color, step);
        s.commit(std::move(step));
    }));
    f.button(trFor(player, "cm.blackboard.erase", "Erase"), act([](Session& s) {
        auto [u, v] = s.cursor();
        std::vector<std::tuple<int, int, std::uint8_t>> step;
        s.setPixel(u, v, 0, step);
        s.commit(std::move(step));
    }));
    f.button(trFor(player, "gui.supplementaries.blackboard.clear", "Clear"), act([](Session& s) {
        std::vector<std::tuple<int, int, std::uint8_t>> step;
        for (int u = 0; u < s.width(); ++u)
            for (int v = 0; v < s.height(); ++v) s.setPixel(u, v, 0, step);
        s.commit(std::move(step));
    }));
    f.button(trFor(player, "gui.supplementaries.blackboard.undo", "Undo"), act([](Session& s) {
        if (s.history.empty()) return;
        auto step = std::move(s.history.back());
        s.history.pop_back();
        for (auto it = step.rbegin(); it != step.rend(); ++it) {
            auto [u, v, c] = *it;
            s.work[s.cellOf(u, v)][static_cast<std::size_t>((v % kSize) * kSize + u % kSize)] = c;
        }
    }));
    f.button(trFor(player, "cm.blackboard.done", "Done"), [weak]() {
        auto s = weak.lock();
        if (s && s->form) (void)s->form->close();
    });
    // the preview follows the cursor sliders and the colour
    s->column.subscribe([weak](double const&) {
        if (auto s = weak.lock(); s && !s->closed) refresh(*s);
    });
    s->row.subscribe([weak](double const&) {
        if (auto s = weak.lock(); s && !s->closed) refresh(*s);
    });
    auto key = s->playerKey;
    auto res = f.show([key, weak](ll::ui::CustomForm::Result) {
        auto s = weak.lock();
        if (!s) return;
        auto it = sessions().find(key);
        if (it != sessions().end() && it->second == s) endSession(key, true);
    });
    if (!res) {
        log().warn("blackboard: could not show the edit form: {}", res.error().message());
        endSession(key, false);
    }
}

} // namespace

std::optional<std::string> editorOf(core::GlobalPos const& pos) {
    for (auto const& [key, s] : sessions()) {
        for (auto const& cell : s->canvas.cells) {
            if (cell && *cell == pos) return s->uuid;
        }
    }
    return std::nullopt;
}

bool openEditor(Player& player, BlockPos const& pos) {
    auto key = player.getOrCreateUniqueID().rawID;
    if (sessions().contains(key)) return true; // already open (Bedrock repeats the use while held)
    auto& region = player.getDimensionBlockSource();
    auto  canvas = connectedBoards(region, pos, config().connected_gui);
    auto  uuid   = player.getUuid().asString();
    for (auto const& cell : canvas.cells) {
        if (!cell) continue;
        if (auto who = editorOf(*cell); who && *who != uuid) {
            player.sendMessage(trFor(player, "cm.blackboard.busy", "Someone else is editing this blackboard"));
            return false;
        }
    }
    auto s       = std::make_shared<Session>();
    s->playerKey = key;
    s->uuid      = uuid;
    s->dim       = static_cast<int>(region.getDimensionId());
    s->canvas    = std::move(canvas);
    for (auto const& cell : s->canvas.cells) {
        s->original.push_back(cell ? boardAt(*cell).px : Pixels{});
    }
    s->work = s->original;
    // start the cursor on the clicked board
    for (int c = 0; c < s->canvas.cols; ++c) {
        for (int r = 0; r < s->canvas.rows; ++r) {
            auto const& cell = s->canvas.at(c, r);
            if (cell && cell->x == pos.x && cell->y == pos.y && cell->z == pos.z) {
                s->column.setData(c * kSize);
                s->row.setData(r * kSize);
            }
        }
    }
    refresh(*s);
    sessions()[key] = s;
    log().info("blackboard: editor opened at {} ({}x{})", game::globalPos(region, pos).key(), s->canvas.cols, s->canvas.rows);
    try {
        showForm(player, s);
    } catch (std::exception const& e) {
        log().warn("blackboard: the edit form failed: {}", e.what());
        endSession(key, false);
        return false;
    }
    return true;
}

std::string submitPixels(Player* player, bool admin, core::GlobalPos const& pos, Pixels const& px) {
    auto* region = regionOf(pos.dim);
    if (!region) return "no world";
    auto bp = game::blockPos(pos);
    if (!region->hasChunksAt(bp, 0, false) || !isBlackboard(region->getBlock(bp))) return "not a blackboard";
    std::shared_ptr<Session> session;
    if (player) {
        if (auto it = sessions().find(player->getOrCreateUniqueID().rawID); it != sessions().end()) session = it->second;
    }
    std::size_t cellIndex = 0;
    bool        inSession = false;
    if (session) {
        for (std::size_t i = 0; i < session->canvas.cells.size(); ++i) {
            if (session->canvas.cells[i] && *session->canvas.cells[i] == pos) {
                inSession = true;
                cellIndex = i;
            }
        }
    }
    if (!inSession && !admin) return "this blackboard is not being edited by you";
    Board board = boardAt(pos);
    if (board.waxed && !admin) return "the blackboard is waxed";
    if (!config().colored_blackboard && !admin) {
        if (!std::all_of(px.begin(), px.end(), [](std::uint8_t v) { return v <= 1; })) return "coloured pixels are disabled";
    }
    if (inSession) {
        // the form must not apply its (unchanged) copy over the submitted one
        session->original[cellIndex] = px;
        session->work[cellIndex]     = px;
        refresh(*session);
    }
    if (board.px == px) return {};
    board.px = px;
    setBoard(*region, bp, board);
    playSound(*region, bp, "complementaries.block.blackboard.draw_complete");
    log().info("blackboard: pixels set at {}", pos.key());
    return {};
}

void closeEditorOf(Player& player) { endSession(player.getOrCreateUniqueID().rawID, false); }

void closeAllEditors() {
    auto all = std::move(sessions());
    sessions().clear();
    for (auto& [key, s] : all) {
        s->closed = true;
        if (s->form && ll::service::getLevel()) (void)s->form->close();
    }
}

// Command parameter types need external linkage.
struct BlackboardSetParams {
    int         x = 0;
    int         y = 0;
    int         z = 0;
    std::string pixels;
};

} // namespace cm::feature::blackboard

CM_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using cm::feature::blackboard::BlackboardSetParams;
    cmd.overload<BlackboardSetParams>()
        .text("blackboard")
        .text("set")
        .required("x")
        .required("y")
        .required("z")
        .required("pixels")
        .execute([](CommandOrigin const& origin, CommandOutput& output, BlackboardSetParams const& p) {
            auto px = cm::feature::blackboard::parseHex(p.pixels);
            if (!px) return output.error("pixels: 256 hex digits expected");
            auto* player = cm::commands::playerOf(origin);
            int   dim    = player ? static_cast<int>(player->getDimensionId()) : 0;
            if (!player) {
                if (auto* e = origin.getEntity()) dim = static_cast<int>(e->getDimensionId());
            }
            auto err = cm::feature::blackboard::submitPixels(
                player,
                cm::commands::isAdmin(origin),
                cm::core::GlobalPos{dim, p.x, p.y, p.z},
                *px
            );
            if (!err.empty()) return output.error(err);
            // players: the client screen submits silently; console / command blocks get a line
            output.success(player ? "" : "Blackboard updated");
        });
});
