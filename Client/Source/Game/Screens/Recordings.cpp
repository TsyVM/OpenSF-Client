// Your own matches' recordings, on the client's own page for them (PageReplay): every match you
// played that the server still keeps (Server/Staff.cpp replay_list: nobody is handed a match they
// were not in, nor one still being played), and the copies kept on this PC. Play takes the copy on
// this PC, or downloads the server's, keeps it, and plays it on the Replay screen (Screens/Replay.cpp);
// Delete deletes this PC's copy; a memo of your own goes with each. The room's Record plate keeps
// every match you finish without being asked.
//
// This PC's copies sit beside settings.cfg in replays/, one `<started>_<match>.replay` each (the
// server's own file, as sent), and the memos in replays/memos.cfg.
#include "Game/Screens/Screens.hpp"

#include "Game/Replay.hpp"
#include "Game/Settings.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"
#include "Game/World/GameWorld.hpp"

#include "Engine/Core/Config.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <ctime>
#include <map>
#include <optional>

namespace lsf {

namespace fs = std::filesystem;

namespace {

using ui::Align;
using proto::ReplayEntry;
using proto::ReplayResult;

constexpr VanU32 kInk = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kSoft = VAN_COL32(200, 200, 194, 255);
constexpr VanU32 kDim = VAN_COL32(140, 140, 132, 255);
constexpr VanU32 kWon = VAN_COL32(110, 214, 90, 255);
constexpr VanU32 kLost = VAN_COL32(236, 104, 84, 255);
constexpr VanU32 kGold = VAN_COL32(240, 206, 96, 255);

fs::path folder() { return Settings::file().parent_path() / "replays"; }

std::string date_text(u64 t) {
    if (!t) return "-";
    const std::time_t tt = std::time_t(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return eng::str::format("%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

std::string length_text(u32 s) { return eng::str::format("%um %02us", s / 60, s % 60); }

// A recording as the page lists it: the server's word on it, this PC's copy, or both.
struct Entry {
    u32 match = 0;
    u64 started = 0;
    std::string server, map;
    u8 mode = 0;
    u32 seconds = 0;
    u32 players = 0;
    u32 bytes = 0;
    u8 team = u8(Team::None);
    u8 result = u8(ReplayResult::Unknown);
    bool on_server = false;
    fs::path local;
    std::string key() const { return server + "#" + std::to_string(match); }
};

// This PC's copies, read once and again whenever one is kept or deleted.
struct Local {
    bool read = false;
    std::vector<Entry> entries;
    std::map<std::string, std::string> memos;
};
Local g_local;

void read_local() {
    g_local = Local{};
    g_local.read = true;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(folder(), ec)) {
        if (f.path().extension() != replay::kExtension) continue;
        replay::Header h;
        proto::MatchLoad load;
        if (!replay::read_header(f.path(), h) || !proto::decode(h.load, load)) continue;
        Entry e;
        e.match = h.match, e.started = h.started, e.server = h.server, e.map = load.settings.map, e.mode = u8(load.settings.mode);
        e.players = u32(load.players.size());
        e.bytes = u32(std::min<uintmax_t>(fs::file_size(f.path(), ec), 0xFFFFFFFFu));
        e.local = f.path();
        g_local.entries.push_back(std::move(e));
    }
    if (const auto text = eng::fs::read_text_file(folder() / "memos.cfg")) {
        eng::ConfigFile cfg;
        if (eng::ConfigFile::parse(*text, cfg))
            for (const eng::ConfigSection* s : cfg.all("memo")) g_local.memos[std::string(s->get("match"))] = s->get_string("text");
    }
}

void write_memos() {
    eng::ConfigFile cfg;
    for (const auto& [key, text] : g_local.memos) {
        if (text.empty()) continue;
        eng::ConfigSection s;
        s.name = "memo";
        s.set("match", key);
        s.set("text", text);
        cfg.sections.push_back(std::move(s));
    }
    std::error_code ec;
    fs::create_directories(folder(), ec);
    (void)eng::fs::write_text_file(folder() / "memos.cfg", "# Soldier Front Legacy: your notes on your recordings.\n" + cfg.serialize());
}

// What is waiting for a download to finish: kept on this PC, and played if the page asked.
struct Keep {
    u32 match = 0;
    bool watch = false;
};
std::optional<Keep> g_keep;
u32 g_over_seen = 0;
double g_over_at = 0;

// The server's list and this PC's copies, one row a match, newest first.
std::vector<Entry> merged(const Session& s) {
    if (!g_local.read) read_local();
    std::vector<Entry> out;
    std::map<std::string, size_t> at;
    if (s.replays)
        for (const ReplayEntry& r : s.replays->replays) {
            Entry e;
            e.match = r.match, e.started = r.started, e.server = s.server_name, e.map = r.map, e.mode = r.mode, e.seconds = r.seconds, e.players = r.players;
            e.bytes = r.bytes, e.team = r.team, e.result = r.result, e.on_server = true;
            at[e.key()] = out.size();
            out.push_back(std::move(e));
        }
    for (const Entry& l : g_local.entries) {
        if (auto it = at.find(l.key()); it != at.end()) {
            out[it->second].local = l.local;
            continue;
        }
        out.push_back(l);
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.started > b.started; });
    return out;
}

const char* result_text(const Entry& e) {
    if (!e.on_server) return "Saved";
    switch (ReplayResult(e.result)) {
        case ReplayResult::Won: return "Won";
        case ReplayResult::Lost: return "Lost";
        case ReplayResult::Draw: return "Draw";
        default: return "-";
    }
}

VanU32 result_ink(const Entry& e) {
    if (!e.on_server) return kDim;
    return e.result == u8(ReplayResult::Won) ? kWon : e.result == u8(ReplayResult::Lost) ? kLost : kSoft;
}

bool keep_file(const Session::ReplayDownload& d) {
    replay::Header h;
    std::vector<replay::Frame> frames;
    bool complete = false;
    if (!replay::read(d.bytes, h, frames, complete)) return false;
    std::error_code ec;
    fs::create_directories(folder(), ec);
    const fs::path file = folder() / (std::to_string(h.started) + "_" + std::to_string(h.match) + replay::kExtension);
    if (!eng::fs::write_file(file, d.bytes.data(), d.bytes.size())) return false;
    LOG_INFO("Recordings: match %u kept as %s (%zu bytes)", h.match, eng::str::narrow(file.filename().wstring()).c_str(), d.bytes.size());
    g_local.read = false;
    return true;
}

// In a match (or on its way into one): watching would take its world's place.
bool in_a_match(App& app) {
    return app.session().match_load || app.screen() == Screen::Loading || app.screen() == Screen::Match || app.screen() == Screen::Result;
}

void play_file(App& app, const fs::path& file) {
    if (auto bytes = eng::fs::read_file(file)) {
        if (in_a_match(app)) {
            ui::toast(ui::Toast::Warning, "Leave the match first.");
            return;
        }
        app.watch_replay(std::move(*bytes));
    } else {
        ui::toast(ui::Toast::Bad, "That recording could not be read from this PC.");
        g_local.read = false;
    }
}

}  // namespace

bool play_recording(App& app, u32 match) {
    Session& s = app.session();
    for (const Entry& e : merged(s)) {
        if (e.match != match) continue;
        if (!e.local.empty()) play_file(app, e.local);
        else if (e.on_server && !g_keep) {
            g_keep = Keep{e.match, true};
            s.get_replay(e.match);
        } else {
            return false;
        }
        return true;
    }
    return false;
}

int recordings_listed(App& app, int* kept_here) {
    const std::vector<Entry> list = merged(app.session());
    if (kept_here) {
        *kept_here = 0;
        for (const Entry& e : list) *kept_here += !e.local.empty();
    }
    return int(list.size());
}

// Every frame (App::handle_session): a download this page asked for, kept when it is in; and the
// room's Record plate, which keeps each match finished while it is lit.
void recordings_tick(App& app) {
    Session& s = app.session();
    ScreenState& st = app.state();
    if (st.record_match && s.match_over && s.match_over->match && s.match_over->match != g_over_seen) {
        g_over_seen = s.match_over->match;
        g_over_at = app.now();
    }
    // The server lets go of a match's recording as the match ends: asked for a moment after.
    if (g_over_seen && g_over_at > 0 && app.now() - g_over_at > 2.0 && !g_keep && (!s.replay || s.replay->done || !s.replay->error.empty())) {
        g_keep = Keep{g_over_seen, false};
        s.get_replay(g_over_seen);
        g_over_at = 0;
    }
    if (!g_keep || !s.replay || s.replay->match != g_keep->match) return;
    if (!s.replay->error.empty()) {
        ui::toast(ui::Toast::Warning, "%s", s.replay->error.c_str());
        g_keep.reset();
        s.replay.reset();
        return;
    }
    if (!s.replay->done) return;
    const Keep k = *g_keep;
    g_keep.reset();
    if (!keep_file(*s.replay)) ui::toast(ui::Toast::Bad, "The recording of match %u could not be kept on this PC.", k.match);
    else if (!k.watch) ui::toast(ui::Toast::Good, "The recording of match %u is kept under Replay.", k.match);
    if (k.watch && app.screen() == Screen::Recordings) {
        std::vector<u8> file = std::move(s.replay->bytes);
        s.replay.reset();
        app.watch_replay(std::move(file));
        return;
    }
    s.replay.reset();
}

void draw_recordings(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!st.replays_asked && s.signed_in()) {
        s.list_replays();
        st.replays_asked = true;
        g_local.read = false;
    }
    const ui::Page& page = lobby_page(app, "PageReplay");
    ui::page_begin("##page_replay");
    const Nav nav = common_chrome(app, 5);
    ui::draw_static(page, {26, 27, 28, 29, 30, 31, 35});
    ui::text_at(36, 86, 664, 108, "The matches you played: the server keeps them a while, and this PC keeps what you play or record.", kSoft, Align::Left, true, 11.0f);

    const std::vector<Entry> list = merged(s);
    int sel = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (int(list[i].match) == st.replay_sel) sel = int(i);
    std::vector<ui::Row> rows;
    for (size_t i = 0; i < list.size(); ++i) {
        const Entry& e = list[i];
        ui::Row r;
        r.key = int(e.match);
        r.cells.push_back({std::to_string(e.match), kDim});
        r.cells.push_back({date_text(e.started), kSoft});
        r.cells.push_back({mode_name(Mode(e.mode)), kInk, Align::Left, true});
        r.cells.push_back({std::to_string(e.players), kSoft});
        r.cells.push_back({app.map_title(e.map), kInk, Align::Left});
        r.cells.push_back({result_text(e), result_ink(e), Align::Center, true});
        rows.push_back(std::move(r));
    }
    static const std::string titles[] = {"No.", "Date", "Game", "Players", "Map", "Result"};
    const ui::ListResult picked = ui::list(page, 25, rows, st.replay_sel, titles);
    if (picked.clicked && picked.key >= 0) st.replay_sel = picked.key, st.replay_memo_editing = false;
    if (rows.empty())
        ui::text_at(30, 300, 669, 330, !s.replays ? "Asking the server..." : "No recordings yet: finish a match and it is here.", kDim, Align::Center, true);
    const Entry* e = sel >= 0 ? &list[size_t(sel)] : nullptr;

    // Play, Delete, Refresh.
    const auto& dl = s.replay;
    const bool busy = g_keep.has_value();
    const bool in_match = in_a_match(app);
    if (ui::button(page, 33, e && !busy && !in_match, nullptr, in_match ? "Leave the match first" : "Watch it") || (picked.activated && e && !busy && !in_match))
        (void)play_recording(app, e->match);
    if (ui::button(page, 34, e && !e->local.empty() && !busy, nullptr, "Delete this PC's copy (the server keeps its own a while)")) {
        std::error_code ec;
        fs::remove(e->local, ec);
        g_local.read = false;
        ui::toast(ui::Toast::Info, "This PC's copy of match %u is deleted.", e->match);
    }
    if (ui::button(page, 37, !busy, nullptr, "Ask the server again")) {
        s.list_replays();
        g_local.read = false;
    }

    // The match picked: its map, its game, how it went, the file, the memo.
    if (e) {
        ui::picture(page, 32, app.atlas().map_picture(e->map));
        ui::text(page, 26, mode_name(Mode(e->mode)), kInk);
        ui::text(page, 27, e->on_server ? std::string(result_text(*e)) + (e->team == u8(Team::Red) ? "  (Red)" : e->team == u8(Team::Blue) ? "  (Blue)" : "") : "-",
                 result_ink(*e));
        ui::text(page, 28, e->seconds ? length_text(e->seconds) : std::string("-"), kSoft);
        ui::text(page, 29, eng::str::format("%u soldiers", e->players), kSoft);
        std::string info = eng::str::format("Match #%u on %s\n%s\n%s", e->match, e->server.empty() ? "?" : e->server.c_str(), date_text(e->started).c_str(),
                                            app.map_title(e->map).c_str());
        info += e->on_server ? "\nOn the server: kept" : "\nOn the server: no longer kept";
        info += e->local.empty() ? "\nOn this PC: not yet (Play keeps it)" : eng::str::format("\nOn this PC: kept, %.0f KB", double(e->bytes) / 1024.0);
        ui::paragraph(page, 30, info, kSoft);
        ui::text_at(710, 430, 900, 448, "Memo", kGold, Align::Left, true, 11.0f);
        std::string& memo = g_local.memos[e->key()];
        if (st.replay_memo_editing) {
            const bool enter = ui::edit_at(9901, 703, 453, 977, 557, st.replay_memo, 200, "a note of your own on this match");
            if (enter) {
                memo = eng::str::sanitize_line(st.replay_memo, 200);
                write_memos();
                st.replay_memo_editing = false;
            }
        } else {
            ui::paragraph(page, 31, memo.empty() ? std::string("(none)") : memo, memo.empty() ? kDim : kInk);
        }
        if (ui::button(page, 40, true, nullptr, st.replay_memo_editing ? "Keep the memo" : "Write a memo")) {
            if (st.replay_memo_editing) {
                memo = eng::str::sanitize_line(st.replay_memo, 200);
                write_memos();
                st.replay_memo_editing = false;
            } else {
                st.replay_memo = memo;
                st.replay_memo_editing = true;
            }
        }
    }
    // A download under way.
    if (busy && dl && dl->match == g_keep->match) {
        const float f = dl->total ? float(dl->got) / float(dl->total) : 0.0f;
        ui::fill_at(682, 672, 997, 680, VAN_COL32(20, 20, 18, 255));
        ui::fill_at(682, 672, 682 + 315 * f, 680, VAN_COL32(150, 210, 80, 255));
        ui::text_at(682, 650, 997, 670, dl->total ? eng::str::format("Downloading match %u: %.0f%%", dl->match, double(f) * 100.0) : std::string("Asking the server..."),
                    kSoft, Align::Left, false, 11.0f);
    }
    if (ui::button(page, 24, true, nullptr, "Back")) {
        st.replay_memo_editing = false;
        app.go(st.shop_return == Screen::Recordings || st.shop_return == Screen::Shop ? Screen::Channels : st.shop_return);
    }
    bottom_strip(app, "Replay");
    ui::page_end();
    handle_nav(app, nav);
}

}  // namespace lsf
