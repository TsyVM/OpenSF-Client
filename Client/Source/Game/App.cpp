#include "Game/App.hpp"

#include "Game/Replay.hpp"

#include "Game/Render/ModelStage.hpp"
#include "Game/Screens/Screens.hpp"
#include "Game/Ui/Kit.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"
#include "Game/World/GameWorld.hpp"
#if LSF_WITH_SERVER
#include "Server.hpp"
#endif
#include "Engine/Core/Crypto.hpp"
#include "Game/Tvas.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Platform/Keys.hpp"
#include "Engine/Platform/System.hpp"

#include <vangui/vangui.h>
#include <vangui/misc/vangui_anim.h>
#include <vangui/misc/vangui_notify.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <bit>
#include <cmath>

namespace lsf {

std::filesystem::path user_directory();
std::filesystem::path cache_directory();
ListedServer direct_server(const ServerEntry& e);

const char* screen_name(Screen s) {
    switch (s) {
        case Screen::Boot: return "boot";
        case Screen::Servers: return "servers";
        case Screen::CodeName: return "codename";
        case Screen::Channels: return "channels";
        case Screen::Lobby: return "lobby";
        case Screen::Room: return "room";
        case Screen::Shop: return "shop";
        case Screen::Loading: return "loading";
        case Screen::Match: return "match";
        case Screen::Result: return "result";
        case Screen::Replay: return "replay";
        case Screen::Recordings: return "recordings";
    }
    return "?";
}

App::App(eng::Api api) : device_owner_(eng::Device::make(api)), device_(*device_owner_), state_(std::make_unique<ScreenState>()) {}

App::~App() {
    if (boot_.joinable()) boot_.join();
    world_.reset();
    stage_.reset();
    session_.disconnect("closed");
    // Nothing of a server's is left lying in the cache folder.
    registry::clear();
    data_.set_session({});
    pack::remove_layer(mounted_);
#if LSF_WITH_SERVER
    if (local_server_) local_server_->stop_thread();
#endif
    ui::set_atlas(nullptr);
    ui_.shutdown();
    device_.destroy();
}

bool App::init(const LaunchOptions& opts) {
    opts_ = opts;
    settings_.load();
    const bool automated = automated_ = !opts.shot.empty() || !opts.autotest.empty();
    eng::WindowDesc wd;
    wd.title = L"Soldier Front Legacy";
    wd.width = opts.width > 0 ? opts.width : settings_.width;
    wd.height = opts.height > 0 ? opts.height : settings_.height;
    // A window no bigger than the screen has room for (settings from another screen, or a first run on a small one).
    if (const eng::ScreenSize room = window_.screen_room(); !automated && room.width >= 640 && room.height >= 480) {
        wd.width = std::min(wd.width, room.width);
        wd.height = std::min(wd.height, room.height);
    }
    wd.mode = settings_.borderless && !automated ? eng::DisplayMode::Borderless : eng::DisplayMode::Windowed;
    wd.background = automated;
    if (!window_.create(wd)) return false;
    eng::Device::Options dev{settings_.vsync, opts.d3d_debug, opts.warp};
    dev.angle_backend = opts.angle_backend.c_str();
#ifndef _WIN32
    dev.native_display = window_.native_display();
#endif
    if (!device_.create(window_.hwnd(), window_.width(), window_.height(), dev)) {
        device_failed_ = true;
        return false;
    }
    LOG_INFO("Renderer: %s on %s", eng::api_name(device_.api()), device_.adapter_name().c_str());
    const eng::ScreenSize screen = window_.screen();
    LOG_INFO("Screen: %dx%d; the window is %dx%d (%s)", screen.width, screen.height, window_.width(), window_.height(),
             window_.display_mode() == eng::DisplayMode::Borderless ? "the whole screen" : "windowed");
    pipe_ok_ = pipe_.init(device_);
    if (!pipe_ok_) LOG_ERROR("The frame's targets could not be set up: the match will not draw");
    else pipe_.load_art(opts.content_dir);
    if (!ui_.init(window_, device_, ui::apply_theme)) return false;
    ui::set_fonts(&ui_.fonts());
    ui::set_atlas(&atlas_);
    VanGui::SetNotificationsPos(VanGui::VanNotifyPos_TopRight);
    VanGui::SetNotificationsMaxCount(6);
#ifndef _WIN32
    window_.on_native_window = [this](void* w) {
        if (w) device_.attach_window(w);
        else device_.detach_window();
    };
    window_.on_background = [this](bool away) { mixer_.set_device_paused(away); };
#endif
    if (!automated) mixer_.open_device();
    if (!data_.open(opts.data_dir)) {
        eng::platform::fatal_message("Soldier Front Legacy",
                                     "The Soldier Front client data could not be opened.\nSee game.log for details.", window_.hwnd());
        return false;
    }
    sounds_.attach(&mixer_, &data_);
    sounds_.apply(settings_);
    session_.set_global_chat(settings_.global_chat);
    session_.set_room_invites(settings_.room_invites);
    // Team Vanilla's account service (TV-15): the address is a setting with the built-in one as its
    // default; a test names its own. The sign-in kept from last time is tried at once (ID-9).
    {
        const std::string tvas_address = !opts.tvas.empty() ? opts.tvas : !settings_.tvas.empty() ? settings_.tvas : std::string(tvas::kTvasAddress);
        eng::net::set_user_agent(eng::str::format("LegacySF/%u (%s)", unsigned(kBuildNumber), proto::platform_name(u8(
#if defined(__ANDROID__)
                                                                                                    proto::Platform::Android
#elif defined(__APPLE__)
                                                                                                    proto::Platform::MacOS
#elif defined(_WIN32)
                                                                                                    proto::Platform::Windows
#else
                                                                                                    proto::Platform::Linux
#endif
                                                                                                    ))));
        session_.set_tvas(tvas_address, user_directory() / (automated ? "session-test.dat" : "session.dat"));
        session_.cache_dir = automated ? eng::fs::executable_directory() / "content-cache-test" : cache_directory() / "content";
        session_.cache_cap = u64(settings_.cache_mb) << 20;
        session_.ask_over = u64(settings_.download_ask_mb) << 20;
        session_.mount = [this](const std::vector<std::filesystem::path>& packs, const eng::json::Value& manifest, const std::string& hash, std::string* why) {
            return mount_packs(packs, manifest, hash, why);
        };
        // The player's own servers, by address (SL-8).
        for (const ServerEntry& e : settings_.servers) session_.servers.push_back(direct_server(e));
        session_.tv_status();
        if (!automated && settings_.remember) session_.tv_resume();
    }
    sounds_.set_logging(!opts.autotest.empty());
    ui::set_click_sound([this] { sounds_.play(Sounds::Menu::Click); });
    atlas_.set_device(&device_, &data_);
    atlas_.set_clan_marks(&clan_marks_);
    stage_ = std::make_unique<ModelStage>(*this);
    state_->account = settings_.account;
    // The server last signed in to, picked on the list already.
    state_->server_sel = 0;
    const auto rows = servers();
    for (size_t i = 0; i < rows.size(); ++i)
        if (!settings_.last_server.empty() && rows[i].name == settings_.last_server) state_->server_sel = int(i);
    boot_ = std::thread([this] { boot_thread(); });
    return true;
}

std::string App::boot_status() const {
    std::lock_guard lock(boot_mutex_);
    return boot_status_;
}

void App::boot_thread() {
    auto step = [&](float p, const char* what) {
        {
            std::lock_guard lock(boot_mutex_);
            boot_status_ = what;
        }
        boot_progress_ = p;
    };
    step(0.05f, "Opening the lobby archives");
    data_.library(sf::Pack::Lobby);
    step(0.15f, "Reading the interface");
    atlas_.prepare(data_);
    step(0.45f, "Opening the map archives");
    data_.library(sf::Pack::Area);
    step(0.60f, "Listing the maps");
    auto levels = sf::list_levels(data_);
    std::erase_if(levels, [](const sf::LevelListing& l) { return !l.playable; });
    step(0.70f, "Reading the map notes");
    auto infos = sf::load_map_names(data_);
    std::string notice = sf::load_lobby_text(data_, "Notice.txt").value_or("");
    // Notice.txt ends each line with " endl" and opens with a version tag.
    std::string clean;
    for (std::string_view line : eng::str::split(notice, '\n', false)) {
        std::string l(eng::str::trim(line));
        if (l.starts_with("*VERSION")) continue;
        if (l.ends_with("endl")) l = std::string(eng::str::trim(std::string_view(l).substr(0, l.size() - 4)));
        clean += l + "\n";
    }
    step(0.80f, "Opening the weapon and force archives");
    data_.library(sf::Pack::Weapon);
    data_.library(sf::Pack::Force);
    step(0.92f, "Opening the menu archives");
    data_.library(sf::Pack::Menu);
    step(0.96f, "Tuning the radio");
    sounds_.preload();
    clan_marks_.open(data_.root());
    {
        std::lock_guard lock(boot_mutex_);
        levels_ = std::move(levels);
        map_infos_ = std::move(infos);
        notice_ = std::move(clean);
    }
    step(1.0f, "Ready");
    boot_done_ = true;
}

const sf::MapInfo* App::map_info(std::string_view level_id) const {
    const std::string id = sf::lower(level_id);
    auto norm = [](std::string s) {
        std::string o;
        for (char c : sf::lower(s))
            if (std::isalnum((unsigned char)c)) o += c;
        return o;
    };
    for (const sf::MapInfo& m : map_infos_) {
        std::string pic = sf::lower(m.picture);
        if (pic.starts_with("sf_m_")) pic.erase(0, 5);
        if (auto dot = pic.rfind('.'); dot != std::string::npos) pic.resize(dot);
        if (norm(pic) == norm(id) || norm(m.name) == norm(id)) return &m;
    }
    return nullptr;
}

std::string App::map_title(std::string_view level_id) const {
    // A server's own map is called what its pack calls it.
    if (const PackMap* m = registry::map(level_id)) return m->title;
    if (const sf::MapInfo* m = map_info(level_id)) return m->name;
    for (const sf::LevelListing& l : levels_)
        if (l.id == level_id) return l.title;
    return std::string(level_id);
}

Mission App::map_mission(std::string_view level_id) {
    (void)maps_for(Mode::TeamBattle);   // the rules read
    for (const MapRules& r : map_rules_)
        if (r.id == level_id) return r.mission;
    return Mission::Elimination;
}

std::vector<std::string> App::maps_for(Mode mode, bool switched_off_too) {
    if (map_rules_.empty() || map_rules_stale_) {
        // Each map's world script once; a server's own maps' when its packs are mounted.
        for (const sf::LevelListing& l : levels_) {
            if (std::any_of(map_rules_.begin(), map_rules_.end(), [&](const MapRules& r) { return r.id == l.id; })) continue;
            if (auto level = sf::load_level(data_, l.id, sf::kLevelGameplay)) map_rules_.push_back(lsf::map_rules(*level));
        }
        map_rules_stale_ = false;
    }
    std::vector<std::string> ids;
    for (const MapRules& r : map_rules_)
        if (mode_offers(mode, r) && (switched_off_too || session_.games.takes_map(r.id))) ids.push_back(r.id);
    std::sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b) { return map_title(a) < map_title(b); });
    std::vector<std::string> out;
    if (ids.size() > 1) {
        out.emplace_back(kAllRandom);
        for (const char* hot : kHotMaps)
            if (std::find(ids.begin(), ids.end(), hot) != ids.end()) {
                out.emplace_back(kHotRandom);
                break;
            }
    }
    out.insert(out.end(), ids.begin(), ids.end());
    return out;
}

std::vector<std::string> App::every_map() {
    std::vector<std::string> ids;
    for (int k = 0; k < int(Mode::Count); ++k)
        for (std::string& id : maps_for(Mode(k), true))
            if (!is_random_map(id) && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(std::move(id));
    std::sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b) { return map_title(a) < map_title(b); });
    return ids;
}

std::vector<App::ServerRow> App::servers() const {
    std::vector<ServerRow> out;
#if LSF_WITH_SERVER
    // SL-9: the server inside the game, on Windows and Linux only (PF-1, PF-2).
    ServerRow here;
    here.name = "This PC (play and host here)";
    here.address = "127.0.0.1";
    here.local = true;
    here.id = local_id_;
    here.answered = hosting();
    out.push_back(std::move(here));
#endif
    // D5: Team Vanilla's official servers first, then the community's, then the player's own by address.
    for (const ListedServer& s : session_.servers) out.push_back(s);
    return out;
}

// Where the player's own files are kept (1.1): beside the game on Windows, as always; the system's
// own places elsewhere, where the game's folder may not be written.
std::filesystem::path user_directory() {
#if defined(_WIN32) || defined(__ANDROID__)
    return eng::fs::executable_directory();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / "Library" / "Application Support" / "Soldier Front Legacy";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / "legacysf";
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / "legacysf";
#endif
}

// The pack cache's place (11.8).
std::filesystem::path cache_directory() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::filesystem::path(buffer) / "LegacySF";
    return eng::fs::executable_directory() / "cache";
#elif defined(__ANDROID__)
    return eng::fs::executable_directory() / "cache";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / "Library" / "Caches" / "Soldier Front Legacy";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / "legacysf";
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".cache" / "legacysf";
#endif
}

ListedServer direct_server(const ServerEntry& e) {
    ListedServer s;
    s.direct = true;
    s.name = e.name;
    const size_t colon = e.address.rfind(':');
    s.address = colon == std::string::npos ? e.address : e.address.substr(0, colon);
    int port = kDefaultPort;
    if (colon != std::string::npos) eng::str::parse_int(e.address.substr(colon + 1), port);
    s.port = u16(std::clamp(port, 1, 65535));
    return s;
}

void App::sign_in(bool create) {
    ScreenState& st = *state_;
    st.login_error.clear();
    st.signing_in = true;
    if (st.sign_in_toast) ui::resolve(st.sign_in_toast, false, "Cancelled.");
    st.sign_in_toast = ui::pending(create ? "Creating your account..." : "Signing in...");
    session_.tv_sign_in(st.account, st.password, create, settings_.remember);
}

// This PC's own server (SL-9): Team Vanilla gives this account an id for it, the server inside the
// game starts with a key made for this run, and the game joins it like any other. Unlisted; nothing
// played on it counts (D41).
void App::start_local() {
    auto failed = [this](const std::string& why) {
        ScreenState& s = *state_;
        s.joining = false;
        if (s.join_toast) ui::resolve(s.join_toast, false, why), s.join_toast = 0;
        else ui::toast(ui::Toast::Bad, "%s", why.c_str());
    };
#if LSF_WITH_SERVER
    if (local_server_) {
        session_.connect("127.0.0.1:" + std::to_string(local_server_->port()), now_, local_id_);
        return;
    }
    if (local_pending_) return;
    local_pending_ = true;
    const tvas::ServerKey key = tvas::make_server_key();
    eng::json::Value body;
    body["pubkey"] = eng::crypto::to_hex(key.pub);
    session_.tvas().post("/v1/thispc", body, [this, key, failed](const TvReply& r) {
        local_pending_ = false;
        if (!r.ok()) return failed(r.text());
        local_id_ = r.body["server_id"].as_uint();
        lsfs::ServerOptions o;
        o.tvas = session_.tvas().address();
        o.server_id = local_id_;
        o.key = key;
        o.name = "This PC";
        o.motd = "You are playing on your own PC. Friends on your network can join you here. Nothing played here counts toward your rank, record or SP.";
        o.no_progress = true;
        o.listed = false;
        o.loopback_only = !opts_.autotest.empty();
        const std::filesystem::path dir = eng::fs::executable_directory() / "thispc";
        o.accounts_dir = dir / "accounts";
        o.staff_file = dir / "staff.cfg";
        o.channels_file = dir / "channels.cfg";
        o.shop = dir / "shop.cfg";
        o.recordings = eng::fs::executable_directory() / "recordings";
        // The client's data, for the server's maps (bots, and what each soldier may be told of).
        o.data = data_.root();
        auto server = std::make_unique<lsfs::Server>();
        std::string why;
        if (!server->start_thread(o, &why)) {
            // The usual port taken (a dedicated server on this PC): any free one does for a private host.
            o.port = 0;
            server = std::make_unique<lsfs::Server>();
            if (!server->start_thread(o, &why)) return failed("Could not start a server on this PC: " + why);
        }
        local_server_ = std::move(server);
        ui::toast(ui::Toast::Good, "Your own server is running on this PC.");
        session_.connect("127.0.0.1:" + std::to_string(local_server_->port()), now_, local_id_);
    });
#else
    failed("This game cannot host.");
#endif
}

void App::connect(const ServerRow& row, const std::string& password) {
    ScreenState& st = *state_;
    if (!session_.tv.signed_in) {
        st.login_open = true;
        return;
    }
    server_row_ = row;
    st.joining = true;
    st.join_started = now_;
    st.login_error.clear();
    if (st.join_toast) ui::resolve(st.join_toast, false, "Cancelled.");
    st.join_toast = ui::pending(("Joining " + row.name + "...").c_str());
    if (row.local) return start_local();
    session_.connect(row.endpoint(), now_, row.id, password);
}

void App::add_direct_server(const ServerEntry& e) {
    session_.servers.push_back(direct_server(e));
    session_.ping(session_.servers.back(), now_);
}

void App::switch_server(const ServerRow& row) {
    // Leaving a match is a forfeit, as always (the server counts it when the connection goes).
    if (world_ && !world_->room().map.empty() && session_.signed_in()) session_.leave_match();
    session_.flush(now_);
    session_.disconnect("switching servers");
    world_.reset();
    window_.set_mouse_captured(false);
    go(Screen::Servers);
    connect(row);
}

// Tests: what the shop's draws come to (0: every try takes; -1: the real odds). Team Vanilla's own
// services roll at TVAS (a test's, on this machine); a server's own capsules on the server.
void App::tour_shop_roll(int roll) {
    if (tvas::loopback(session_.tvas().address())) {
        eng::json::Value b;
        b["dev_shop_roll"] = roll;
        session_.tvas().post("/v1/dev/settings", b, {});
    }
#if LSF_WITH_SERVER
    if (local_server_) local_server_->set_shop_roll(roll);
#endif
}

// The maps as they are now: the client's own, and the joined server's.
void App::refresh_levels() {
    auto levels = sf::list_levels(data_);
    std::erase_if(levels, [](const sf::LevelListing& l) { return !l.playable; });
    std::erase_if(map_rules_, [](const MapRules& r) { return r.id.starts_with("x-"); });
    map_rules_stale_ = true;
    std::lock_guard lock(boot_mutex_);
    levels_ = std::move(levels);
}

// MT-3, MT-4: leaving a server forgets everything made from its packs -- the match, the menus'
// soldiers, the registry, the maps, which sounds a weapon's number has -- and takes their files off
// the data. Only ever at a safe point (MT-2): the top of a frame, or a join's mounting.
void App::unmount_packs() {
    unmount_pending_ = false;
    if (mounted_.empty() && registry::empty()) return;
    LOG_INFO("Packs: unmounted");
    world_.reset();
    stage_.reset();   // its loader thread ends before the data changes under it
    registry::clear();
    data_.set_session({});
    pack::remove_layer(mounted_);
    mounted_.clear();
    sounds_.forget_weapons();
    refresh_levels();
    ++content_epoch_;
    stage_ = std::make_unique<ModelStage>(*this);
}

// MT-1, MT-2, PK-4: the session's packs, checked here as if nobody had checked them before, laid
// over the client's data and their content registered. With none: whatever the last server added
// goes at the next safe point (the match itself may be what is leaving).
bool App::mount_packs(const std::vector<std::filesystem::path>& packs, const eng::json::Value& manifest, const std::string& manifest_hash, std::string* why) {
    if (packs.empty()) {
        if (!mounted_.empty() || !registry::empty()) unmount_pending_ = true;
        return true;
    }
    auto refuse = [&](std::string text) {
        LOG_WARN("Packs: refused: %s", text.c_str());
        if (why) *why = std::move(text);
        return false;
    };
    if (!boot_done_) return refuse("The game is still starting: join again in a moment.");
    unmount_packs();
    world_.reset();
    if (!base_names_.built()) base_names_.build(data_);
    // MT-1: their files, as archives of this session's, over the client's data -- with the menus'
    // soldiers' loader stopped while the data changes under it.
    stage_.reset();
    u8 salt[4];
    eng::crypto::random_bytes(salt);
    mounted_ = session_.cache_dir.parent_path() / "mounted" / eng::str::format("%02x%02x%02x%02x", salt[0], salt[1], salt[2], salt[3]);
#if defined(__ANDROID__)
    const u32 max_texture = pack::kMaxTextureAndroid;
#else
    const u32 max_texture = pack::kMaxTexture;
#endif
    SessionContent content;
    std::string bad;
    if (!pack::mount_session(packs, manifest, data_, base_names_, mounted_, max_texture, content, &bad)) {
        mounted_.clear();
        stage_ = std::make_unique<ModelStage>(*this);
        return refuse(bad);
    }
    const size_t count = packs.size();
    content.manifest_hash = manifest_hash;
    LOG_INFO("Packs: mounted %zu (%zu weapons, %zu characters, %zu parts, %zu maps), manifest %s", count, content.weapons.size(), content.forces.size(), content.items.size(),
             content.maps.size(), manifest_hash.substr(0, 12).c_str());
    registry::set(std::move(content));
    sounds_.forget_weapons();
    refresh_levels();
    ++content_epoch_;
    stage_ = std::make_unique<ModelStage>(*this);
    return true;
}

void App::go(Screen s) {
    if (s == screen_) return;
    LOG_INFO("Screen: %s -> %s", screen_name(screen_), screen_name(s));
    screen_ = s;
    screen_since_ = now_;
    ui::reset_intros(screen_name(s));
    VanGui::SetWindowFocus(nullptr);
    // The page screens with a chat log say things there, as the original did (green lines),
    // and the toasts of the screen before do not follow you in.
    if (s == Screen::Channels || s == Screen::Lobby) {
        VanGui::ClearNotifications();
        ui::set_toast_sink([this](ui::Toast, const std::string& text) {
            proto::ChatLine line;
            line.scope = u8(proto::ChatScope::System);
            line.text = text;
            session_.lobby_chat.push_back({line, now_});
        });
    } else {
        ui::set_toast_sink(nullptr);
    }
}

bool App::watch_replay(std::vector<u8> file) {
    replay::Header h;
    std::vector<replay::Frame> frames;
    bool complete = false;
    std::string err;
    proto::MatchLoad load;
    if (!replay::read(file, h, frames, complete, &err) || !proto::decode(h.load, load)) {
        ui::toast(ui::Toast::Bad, "That recording cannot be read%s%s", err.empty() ? "" : ": ", err.c_str());
        return false;
    }
    if (h.game_version != kProtocolVersion)
        ui::toast(ui::Toast::Warning, "Recorded by game version %u (this is %u): it may not play back as it was.", h.game_version, kProtocolVersion);
    // MT-5: a match played with a server's packs is watched with exactly those: the ones mounted
    // now, or the cache's -- never half drawn.
    bool mounted_for_it = false;
    if (!h.manifest.empty() && h.manifest != registry::content().manifest_hash) {
        if (session_.state() != Session::State::Offline && session_.state() != Session::State::Failed) {
            ui::toast(ui::Toast::Bad, "This recording was made with other packs than this server's: leave the server to watch it.");
            return false;
        }
        std::vector<std::filesystem::path> files;
        std::vector<std::pair<pack::Pack, std::string>> named;
        std::vector<u32> sizes;
        std::string missing;
        for (const std::string& entry : h.packs) {
            // "id:sha256", from a file: neither is trusted to be what it says (SC-3).
            const size_t colon = entry.find(':');
            const std::string id = entry.substr(0, colon), sha = colon == std::string::npos ? std::string() : entry.substr(colon + 1);
            if (!pack::id_ok(id) || sha.size() != 64 || sha.find_first_not_of("0123456789abcdef") != std::string::npos) {
                ui::toast(ui::Toast::Bad, "That recording names packs that cannot be packs.");
                return false;
            }
            pack::Loaded l;
            const std::filesystem::path cached = session_.cache_dir / (sha + ".lsfpack");
            if (!pack::load_file(cached, l, nullptr) || l.sha256 != sha) {
                missing += (missing.empty() ? "" : ", ") + id;
                continue;
            }
            files.push_back(cached);
            sizes.push_back(u32(l.bytes.size()));
            named.emplace_back(std::move(l.pack), sha);
        }
        if (!missing.empty() || files.empty()) {
            ui::toast(ui::Toast::Bad, "This recording needs packs this PC does not have (%s): join the server it was made on once to get them.", missing.empty() ? "none named" : missing.c_str());
            return false;
        }
        const eng::json::Value manifest = pack::manifest(named, sizes);
        const std::string text = manifest.dump();
        std::string why;
        if (pack::sha256_hex(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size())) != h.manifest || !mount_packs(files, manifest, h.manifest, &why)) {
            ui::toast(ui::Toast::Bad, "This recording's packs cannot be used%s%s", why.empty() ? "" : ": ", why.c_str());
            return false;
        }
        mounted_for_it = true;
    }
    Watching w;
    w.mounted = mounted_for_it;
    w.match = h.match;
    w.started = h.started;
    w.map = load.settings.map;
    w.complete = complete;
    w.back = screen_ == Screen::Replay ? (watching_ ? watching_->back : Screen::Lobby) : screen_;
    for (replay::Frame& f : frames) {
        w.times.push_back(f.ms);
        w.frames.push_back(std::move(f.bytes));
    }
    load.you = 0;
    world_ = std::make_unique<GameWorld>(*this);
    world_->start_watching(load);
    watching_ = std::move(w);
    LOG_INFO("Watching match %u: %zu frames, %.0f s, %s", h.match, watching_->frames.size(), double(replay_length_ms()) / 1000.0,
             complete ? "complete" : "cut short");
    go(Screen::Replay);
    return true;
}

void App::leave_replay() {
    if (!watching_) return;
    const Screen back = watching_->back;
    // The packs mounted to watch it go with it (MT-3).
    if (watching_->mounted) unmount_pending_ = true;
    watching_.reset();
    world_.reset();
    window_.set_mouse_captured(false);
    go(session_.signed_in() ? back : Screen::Servers);
}

// The recording's clock: frames handed to the world as their moment comes.
void App::replay_tick() {
    if (!world_ || !world_->ready() || screen_ != Screen::Replay) return;
    Watching& w = *watching_;
    if (!w.paused) w.ms = std::min<double>(w.ms + double(dt_) * 1000.0 * double(w.speed), replay_length_ms());
    while (w.next < w.frames.size() && w.times[w.next] <= u32(w.ms)) world_->feed(w.frames[w.next++]);
}

// Forwards: the frames between, fast. Backwards: from the start again, fast up to a moment short of
// the place sought, so it plays in from there like the rest.
void App::replay_seek(double ms) {
    if (!watching_ || !world_ || !world_->ready()) return;
    Watching& w = *watching_;
    ms = std::clamp(ms, 0.0, double(replay_length_ms()));
    if (ms < w.ms) {
        world_->watch_reset();
        w.next = 0;
    }
    const double lead = std::max(0.0, ms - 600.0);
    while (w.next < w.frames.size() && w.times[w.next] <= u32(lead)) world_->feed(w.frames[w.next++], true);
    w.ms = lead;
}

void App::begin_match_load() {
    if (!session_.match_load) return;
    world_ = std::make_unique<GameWorld>(*this);
    world_->start_loading(*session_.match_load, false);
    session_.match_load.reset();
    go(Screen::Loading);
}

void App::leave_match() {
    // Called from inside the world's own menu: the world goes at the end of the frame.
    if (world_ && !world_->room().map.empty() && session_.signed_in()) session_.leave_match();
    leave_pending_ = true;
}

void App::handle_session() {
    Session& s = session_;
    ScreenState& st = *state_;
    // TV-8: how often Team Vanilla is asked for news.
    s.set_poll_pace(screen_ == Screen::Match || screen_ == Screen::Loading ? PollPace::Match : st.social_open || VanGui::GetIO().WantTextInput ? PollPace::Chat : PollPace::Menu);
    // Team Vanilla: signing in (ID-11).
    if (s.ev_tv_refused) {
        s.ev_tv_refused = false;
        st.signing_in = false;
        st.login_error = s.tv_error;
        if (st.sign_in_toast) ui::resolve(st.sign_in_toast, false, s.tv_error), st.sign_in_toast = 0;
        else ui::toast(ui::Toast::Bad, "%s", s.tv_error.c_str());
    }
    if (s.ev_tv_signed_in) {
        s.ev_tv_signed_in = false;
        st.signing_in = false;
        st.login_open = false;
        st.password.clear(), st.password2.clear();   // never kept once sent (ID-1)
        const std::string who = s.tv.code_name.empty() ? s.tv.username : s.tv.code_name;
        if (st.sign_in_toast) ui::resolve(st.sign_in_toast, true, who.empty() ? std::string("Signed in.") : "Signed in as " + who + "."), st.sign_in_toast = 0;
        if (settings_.remember && !st.account.empty()) {
            settings_.account = st.account;
            save_settings();
        }
        if (s.tv.must_change) st.change_password_open = true;   // ID-12: a staff reset's temporary password
        if (s.tv.code_name.empty() || s.tv.free_rename) go(Screen::CodeName);
        s.request_servers(now_);
    }
    if (s.ev_tv_password) {
        s.ev_tv_password = false;
        st.change_password_open = false;
        st.new_password.clear(), st.new_password2.clear(), st.password.clear();
        ui::toast(ui::Toast::Good, "Your password is changed.");
    }
    if (s.ev_named) {
        s.ev_named = false;
        ui::toast(ui::Toast::Good, "Welcome, %s.", s.tv.code_name.c_str());
        if (screen_ == Screen::CodeName) go(Screen::Servers);
    }
    if (s.ev_tv_signed_out) {
        s.ev_tv_signed_out = false;
        st.joining = false;
        if (!s.tv_error.empty()) ui::toast(ui::Toast::Warning, "%s", s.tv_error.c_str());
        if (screen_ != Screen::Boot) {
            world_.reset();
            window_.set_mouse_captured(false);
            go(Screen::Servers);
        }
    }
    // A game server: the join (5.1).
    if (s.ev_refused) {
        s.ev_refused = false;
        st.joining = false;
        st.login_error = s.refused;
        if (st.join_toast) ui::resolve(st.join_toast, false, s.refused), st.join_toast = 0;
        else ui::toast(ui::Toast::Bad, "%s", s.refused.c_str());
    }
    if (s.ev_welcomed) {
        s.ev_welcomed = false;
    }
    if (s.signed_in() && s.has_profile && st.joining) {
        st.joining = false;
        if (st.join_toast) ui::resolve(st.join_toast, true, "Joined " + s.server_name), st.join_toast = 0;
        settings_.last_server = server_row_.name;   // picked again on the server list next time
        if (const u64 id = s.server_info ? s.server_info->server_id : 0; id && !server_row_.local) {
            auto& recent = settings_.recent;
            std::erase(recent, id);
            recent.insert(recent.begin(), id);
            if (recent.size() > 12) recent.resize(12);
        }
        save_settings();
        if (s.server_info && s.server_info->no_progress) ui::toast(ui::Toast::Info, "This PC: nothing played here counts toward your rank, record or SP.");
        go(Screen::Channels);
    }
    if (s.ev_channel_joined) {
        s.ev_channel_joined = false;
        ui::toast(ui::Toast::Info, "Entered %s", s.channel_name.c_str());
        if (screen_ == Screen::Channels) go(Screen::Lobby);
    }
    if (s.ev_room_joined) {
        s.ev_room_joined = false;
        if (screen_ != Screen::Match && screen_ != Screen::Loading && screen_ != Screen::Result && screen_ != Screen::Replay) go(Screen::Room);
    }
    if (s.ev_room_left) {
        s.ev_room_left = false;
        if (!s.room_left_reason.empty()) ui::toast(ui::Toast::Warning, "%s", s.room_left_reason.c_str());
        if (watching_) {
            watching_->back = s.channel ? Screen::Lobby : Screen::Channels;   // where leaving the recording goes
        } else {
            if (world_) world_.reset();
            window_.set_mouse_captured(false);
            if (s.channel) go(Screen::Lobby);
        }
    }
    if (s.ev_disconnected) {
        s.ev_disconnected = false;
        world_.reset();
        window_.set_mouse_captured(false);
        ui::toast(ui::Toast::Bad, "Disconnected from the server%s%s", s.disconnect_reason.empty() ? "" : ": ", s.disconnect_reason.c_str());
        go(Screen::Servers);
    }
    while (!s.notices.empty()) {
        const auto n = s.notices.front();
        s.notices.pop_front();
        const ui::Toast k = n.kind == proto::NoticeKind::Good      ? ui::Toast::Good
                            : n.kind == proto::NoticeKind::Warning ? ui::Toast::Warning
                            : n.kind == proto::NoticeKind::Bad     ? ui::Toast::Bad
                                                                   : ui::Toast::Info;
        ui::toast(k, "%s", n.text.c_str());
    }
    while (!s.shop_results.empty()) {
        const auto r = s.shop_results.front();
        s.shop_results.pop_front();
        ui::toast(r.ok ? ui::Toast::Good : ui::Toast::Warning, "%s", r.text.c_str());
        if (r.ok) sounds_.play(Sounds::Menu::Spend);   // the till: SP spent
    }
    // What a request to the shop came to; a capsule's, while its machine is not up (it plays its own out).
    while (!s.service_results.empty()) {
        const auto r = s.service_results.front();
        s.service_results.pop_front();
        ui::toast(r.ok ? ui::Toast::Good : ui::Toast::Warning, "%s", r.text.c_str());
    }
    if (screen_ != Screen::Shop || st.shop_tab != 4)
        while (!s.capsule_results.empty()) {
            const auto r = s.capsule_results.front();
            s.capsule_results.pop_front();
            ui::toast(r.ok ? ui::Toast::Good : ui::Toast::Warning, "%s", r.text.c_str());
        }
    recordings_tick(*this);
    while (!s.staff_results.empty()) {
        const auto r = s.staff_results.front();
        s.staff_results.pop_front();
        ui::toast(r.ok ? ui::Toast::Good : ui::Toast::Warning, "%s", r.text.c_str());
        st.staff_asked = false;   // the open tab asks again: the action may have changed it
    }
    // The vote has ended: its result, once.
    if (s.vote && !s.vote->active && !s.vote->result.empty()) {
        ui::toast(ui::Toast::Info, "%s", s.vote->result.c_str());
        s.vote->result.clear();
    }
    // Called into a match: a recording being watched ends.
    if (s.match_load && watching_) leave_replay();
    if (s.match_load && screen_ != Screen::Loading) begin_match_load();
}

void App::draw_screen() {
    switch (screen_) {
        case Screen::Boot: draw_boot(*this); break;
        case Screen::Servers: draw_servers(*this); break;
        case Screen::CodeName: draw_code_name(*this); break;
        case Screen::Channels: draw_channels(*this); break;
        case Screen::Lobby: draw_lobby(*this); break;
        case Screen::Room: draw_room(*this); break;
        case Screen::Shop: draw_shop(*this); break;
        case Screen::Loading: draw_loading(*this); break;
        case Screen::Match: draw_match(*this); break;
        case Screen::Result: draw_result(*this); break;
        case Screen::Replay: draw_replay(*this); break;
        case Screen::Recordings: draw_recordings(*this); break;
    }
    // The credits take the options' place while they are up, as does the touch controls' arranging
    // screen; a restart is asked once they close.
    if (state_->touch_arrange) touch_arrange_modal(*this);
    else if (state_->credits_open) credits_modal(*this);
    else if (state_->restart_ask) restart_modal(*this);
    else if (state_->settings_open) settings_modal(*this);
    // Staff (F9 from anywhere signed in), reports and votes.
    if (session_.signed_in() && session_.profile.staff() && !VanGui::GetIO().WantTextInput && window_.input().key_pressed(VK_F9)) {
        state_->staff_open = !state_->staff_open;
        state_->staff_asked = false;
        if (state_->staff_open) window_.set_mouse_captured(false);
    }
    if (state_->staff_open) staff_modal(*this);
    if (state_->report_open) report_modal(*this);
    if (state_->vote_open) vote_call_modal(*this);
    if (session_.room) vote_banner(*this);
    if (screen_ != Screen::Room) state_->invite_open = false;   // the room's Invite dialog goes with the room
    // Clans: the Clan Lobby plate's dialog, and an officer's invitation (not over a match).
    const bool front_end = screen_ == Screen::Channels || screen_ == Screen::Lobby || screen_ == Screen::Room || screen_ == Screen::Shop || screen_ == Screen::Recordings;
    if (state_->emblem_open && front_end) emblem_modal(*this);
    else if (state_->clan_open && front_end) clan_modal(*this);
    else if (state_->rewards_open && front_end) rewards_modal(*this);
    else if (state_->social_open && front_end) social_modal(*this);
    else if (state_->invite_open && screen_ == Screen::Room) room_invite_dialog(*this);
    else if (state_->card_open && front_end) id_card_modal(*this);
    if (!front_end) state_->card_open = false;
    if (session_.box_opened && front_end) box_opened_modal(*this);
    else if (front_end && !state_->settings_open && !session_.clan_invites.empty()) clan_invite_modal(*this);
    else if (front_end && !state_->settings_open && !session_.room_invites.empty()) room_invited_modal(*this);
    // A friend's server, or a room's invitation from one (FR-2, FR-4): asked, wherever you are.
    if (session_.switch_request && screen_ != Screen::Boot && screen_ != Screen::CodeName && !state_->joining) switch_modal(*this);
}

void App::capture(const std::string& path) {
    eng::Image img;
    if (!device_.capture(img)) {
        LOG_WARN("Screenshot failed");
        return;
    }
    const auto png = eng::encode_png(img);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    eng::fs::write_file(path, png.data(), png.size());
    LOG_INFO("Screenshot: %s", path.c_str());
}

void App::frame() {
    now_ = eng::time::now();
    dt_ = float(std::clamp(now_ - last_, 0.0, 0.1));
    last_ = now_;
    fps_ = fps_ <= 0 ? 60.0f : fps_ + (1.0f / std::max(dt_, 1e-4f) - fps_) * 0.05f;
    ++frame_;
    if (window_.consume_resize()) device_.resize(window_.width(), window_.height());
    layout();
    pad_frame();

    // A server left: what its packs added goes here, with nothing of a match or a menu reading (MT-2).
    if (unmount_pending_) unmount_packs();
    session_.poll(now_);
    // No sound device (the automated runs): mix this frame's worth anyway, into a capture if one is open.
    if (!mixer_.has_device()) mixer_.pump(dt_);
    handle_session();

    // The atlases go onto the card on the main thread once the loader has read them.
    if (boot_done_ && !atlas_uploaded_) {
        atlas_.upload(device_);
        atlas_uploaded_ = true;
    }

    if (watching_) replay_tick();
    if (world_) world_->update(dt_);

    ui::set_kit_sharp(state_->settings_open ? state_->edit.sharp_ui : settings_.sharp_ui);   // seen as it is set
    ui::tick(now_, dt_);
    // The lobby's music plays through the front end and stops for the match.
    sounds_.tick();
    const bool front_end = screen_ != Screen::Boot && screen_ != Screen::Loading && screen_ != Screen::Match && screen_ != Screen::Result &&
                           screen_ != Screen::Replay;
    sounds_.set_music(front_end && boot_done_ && settings_.lobby_music);
    const float ui_scale = std::max(0.5f, ui::view().h / ui::kStageH);

    // Black: what shows beside a picture kept in shape on a screen of another.
    device_.begin_frame({0, 0, 0, 1});
    if (world_ && pipe_ok_ && (screen_ == Screen::Match || screen_ == Screen::Result || screen_ == Screen::Replay) && world_->ready()) {
        const ui::ViewRect pic = ui::view();
        FrameView v;
        v.width = picture_w_, v.height = picture_h_;
        v.samples = settings_.antialiasing;
        // Fidelity is seen as it is set: with the options open over a match, theirs is drawn.
        const Settings& look = state_->settings_open ? state_->edit : settings_;
        v.fidelity = look.fidelity;
        v.finish = look.finish;
        if (v.fidelity) {
            v.width = std::clamp(int(std::lround(float(picture_w_) * look.finish.render_scale)), 16, 8192);
            v.height = std::clamp(int(std::lround(float(picture_h_) * look.finish.render_scale)), 16, 8192);
        }
        v.x = pic.x, v.y = pic.y, v.w = pic.w, v.h = pic.h;
        pipe_.begin(v, {0.02f, 0.02f, 0.018f, 1});
        world_->render();
        pipe_.end();
    }
    ui_.begin_frame(ui_scale);
    // The front end's top bar (the card, the nav plates, Exit) stays clear of toasts.
    const bool in_world = screen_ == Screen::Loading || screen_ == Screen::Match || screen_ == Screen::Result || screen_ == Screen::Replay;
    ui_.set_overlay_top(in_world ? 0.0f : ui::stage(0, 80).y);
    draw_screen();
    ui::phone_keyboard_frame();
    if (!opts_.autotest.empty()) autotest_step();
    ui_.end_frame();

    if (leave_pending_) {
        leave_pending_ = false;
        world_.reset();
        window_.set_mouse_captured(false);
        if (session_.signed_in()) go(session_.room ? Screen::Room : Screen::Lobby);
        else go(Screen::Servers);
    }
    if (!pending_shot_.empty()) {
        const std::string dir = !opts_.autotest.empty() ? opts_.autotest : ".";
        capture((std::filesystem::path(dir) / (pending_shot_ + ".png")).string());
        pending_shot_.clear();
    }
    device_.present();
    session_.flush(now_);
    window_.input().end_frame();

    // The frame limit (vsync off).
    if (!settings_.vsync && settings_.max_fps > 0) {
        const double budget = 1.0 / double(settings_.max_fps);
        const double spent = eng::time::now() - now_;
        if (spent < budget) eng::time::sleep_precise(budget - spent);
    }
}

int App::run(const LaunchOptions& opts) {
    if (!init(opts)) return device_failed_ ? kRunDeviceFailed : 1;
    last_ = eng::time::now();
    if (!opts.renderer_note.empty()) ui::toast(ui::Toast::Warning, "%s; drawing with %s instead.", opts.renderer_note.c_str(), eng::api_name(device_.api()));
    else if (opts.restarted) ui::toast(ui::Toast::Good, "Now drawing with %s.", eng::api_name(device_.api()));
    while (!quit_ && window_.pump()) {
        if (device_.lost()) {
            eng::platform::fatal_message("Soldier Front Legacy", "The graphics driver stopped responding, so the game has to close.", window_.hwnd());
            break;
        }
        if (window_.minimized()) {
            eng::time::sleep_precise(0.05);
            session_.poll(eng::time::now());
            session_.flush(eng::time::now());
            continue;
        }
        frame();
        if (!opts_.shot.empty() && data_ready() && atlas_uploaded_ && screen_ != Screen::Boot) {
            if (shot_countdown_ < 0) shot_countdown_ = opts_.shot_frames;
            if (--shot_countdown_ == 0) {
                capture(opts_.shot);
                quit_ = true;
            }
        }
    }
    pad_.rumble(0, 0);
    // Gone from the screen now: what is left (saving, leaving the server, the network's last
    // words) happens behind it, never as a window that will not close.
    window_.hide();
    // Saved already when a restart was asked for (the next game may be reading the file by now).
    if (!restarting_) save_settings();
    return 0;
}

// ── The window, the picture and the controller ─────────────────────────────────

void App::save_settings() {
    if (automated_ && !opts_.own_settings) return;   // a test never writes the player's settings
    if (!settings_.save()) LOG_WARN("Could not write %s", eng::str::narrow(Settings::file().wstring()).c_str());
}

// Where the picture goes this frame, and how many pixels it has. In a window the picture is the
// window. With the whole screen, the picture is the size chosen for it (the screen's own unless
// another was picked), pulled over the screen or kept in shape between bars.
void App::layout() {
    const float ww = float(std::max(1, window_.width())), wh = float(std::max(1, window_.height()));
    ui::ViewRect pic{0, 0, ww, wh};
    int w = window_.width(), h = window_.height();
    bool whole = window_.display_mode() == eng::DisplayMode::Borderless;
    bool bars = settings_.scaling == Settings::Scaling::Bars;
    if (whole && settings_.full_width > 0 && settings_.full_height > 0) w = settings_.full_width, h = settings_.full_height;
    if (opts_.picture_width > 0 && opts_.picture_height > 0) w = opts_.picture_width, h = opts_.picture_height, whole = true, bars = opts_.picture_bars;
    w = std::clamp(w, 320, 8192), h = std::clamp(h, 200, 8192);
    if (whole && bars) {
        const float shape = float(w) / float(h);
        float pw = ww, ph = ww / shape;
        if (ph > wh) ph = wh, pw = wh * shape;
        pic = {std::floor((ww - pw) * 0.5f), std::floor((wh - ph) * 0.5f), std::floor(pw), std::floor(ph)};
    }
    picture_w_ = w, picture_h_ = h;
    ui::set_view(ww, wh, pic);
}

void App::apply_display() {
    device_.set_vsync(settings_.vsync);
    if (automated_) return;   // a test keeps the window it was given
    const eng::DisplayMode want = settings_.borderless ? eng::DisplayMode::Borderless : eng::DisplayMode::Windowed;
    int w = settings_.width, h = settings_.height;
    if (const eng::ScreenSize room = window_.screen_room(); room.width >= 640 && room.height >= 480) w = std::min(w, room.width), h = std::min(h, room.height);
    if (window_.display_mode() != want || (want == eng::DisplayMode::Windowed && (window_.width() != w || window_.height() != h)))
        window_.set_display_mode(want, w, h);
}

std::vector<eng::ScreenSize> App::resolutions(bool whole_screen, int aspect) const {
    // The sizes Soldier Front and its players knew, and the ones screens are sold in now.
    static const eng::ScreenSize kUsual[] = {
        {800, 600},   {1024, 768},  {1152, 864},  {1280, 960},  {1400, 1050}, {1600, 1200}, {1920, 1440},   // 4:3
        {1280, 1024},                                                                                      // 5:4
        {1280, 800},  {1440, 900},  {1680, 1050}, {1920, 1200}, {2560, 1600}, {2880, 1800},                // 16:10
        {1280, 720},  {1366, 768},  {1600, 900},  {1920, 1080}, {2560, 1440}, {3840, 2160},                // 16:9
        {2560, 1080}, {3440, 1440}, {3840, 1600},                                                          // 21:9
    };
    const eng::ScreenSize screen = window_.screen();
    const eng::ScreenSize limit = whole_screen ? screen : window_.screen_room();
    std::vector<eng::ScreenSize> out;
    auto add = [&](const eng::ScreenSize& s) {
        if (s.width < 800 || s.height < 600 || s.width > limit.width || s.height > limit.height) return;
        if (aspect > 0 && aspect_of(s.width, s.height) != aspect) return;
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    };
    for (const eng::ScreenSize& s : kUsual) add(s);
    for (const eng::ScreenSize& s : window_.screen_modes()) add(s);
    if (whole_screen) add(screen);
    std::sort(out.begin(), out.end(), [](const eng::ScreenSize& a, const eng::ScreenSize& b) { return a.width != b.width ? a.width < b.width : a.height < b.height; });
    return out;
}

void App::restart() {
#ifdef _WIN32
    save_settings();
    wchar_t exe[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    std::wstring args = L"\"" + std::wstring(exe) + L"\" --data \"" + opts_.data_dir.wstring() + L"\" --restarted";
    // The renderer chosen, said outright as well (settings.cfg may not be writable).
    switch (settings_.renderer) {
        case Settings::Renderer::D3D12: args += L" --d3d12"; break;
        case Settings::Renderer::D3D11: args += L" --d3d11"; break;
        case Settings::Renderer::OpenGL: args += L" --opengl"; break;
        default: break;
    }
    LOG_INFO("Restarting: %s", eng::str::narrow(args).c_str());
    if (automated_) return;   // a test says what it would start, and goes on
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd(args.begin(), args.end());
    cmd.push_back(0);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, eng::fs::executable_directory().c_str(), &si, &pi)) {
        LOG_ERROR("Restart failed (error %lu): the new renderer is taken the next time the game starts", GetLastError());
        ui::toast(ui::Toast::Warning, "The game could not start itself again. The new renderer is used from the next start.");
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    restarting_ = true;
    quit_ = true;
#endif
}

void App::rumble(float strength, float seconds) {
    if (!pad_live_ || !settings_.pad.vibration) return;
    rumble_ = std::max(now_ < rumble_until_ ? rumble_ : 0.0f, std::clamp(strength, 0.0f, 1.0f));
    rumble_until_ = std::max(rumble_until_, now_ + double(seconds));
}

// The controller, once a frame. In a match it is read by the world (GameWorld: the sticks, the
// buttons as bound). Everywhere the mouse is free -- the lobby, a dialog, the match's own menu --
// it works the pointer instead: the left stick moves it, A (Cross) clicks, X (Square) is the
// right button, B (Circle) is Esc, the right stick turns the wheel. Start (Options) is Esc
// anywhere: the match's menu.
void App::pad_frame() {
    pad_.poll();
    pad_live_ = settings_.pad.enabled && pad_.connected() && (window_.focused() || pad_.fed());
    VanGuiIO& io = VanGui::GetIO();
    eng::Input& in = window_.input();
    // What the pad holds for the interface: let go when the pad goes, or the mouse is taken.
    auto hold = [&](bool& held, bool now_down, auto&& change) {
        if (held == now_down) return;
        held = now_down;
        change(now_down);
    };
    // (Asked of the world rather than the window: a window that is not in front never takes the mouse.)
    const bool playing = world_ && screen_ == Screen::Match && world_->ready() && world_->in_control();
    const bool pointer = pad_live_ && !playing && state_->pad_binding < 0;
    hold(pad_start_, pad_live_ && state_->pad_binding < 0 && pad_.down(eng::kPadStart), [&](bool d) {
        in.on_key(VK_ESCAPE, d);
        io.AddKeyEvent(VanGuiKey_Escape, d);
    });
    hold(pad_escape_, pointer && pad_.down(eng::kPadB), [&](bool d) {
        in.on_key(VK_ESCAPE, d);
        io.AddKeyEvent(VanGuiKey_Escape, d);
    });
    hold(pad_click_, pointer && pad_.down(eng::kPadA), [&](bool d) {
        in.on_mouse_button(eng::kMouseLeft, d);
        io.AddMouseButtonEvent(0, d);
    });
    hold(pad_context_, pointer && pad_.down(eng::kPadX), [&](bool d) {
        in.on_mouse_button(eng::kMouseRight, d);
        io.AddMouseButtonEvent(1, d);
    });
    if (pointer) {
        // Squared, so a nudge places it and a push crosses the screen in under a second.
        const float sx = pad_.lx(), sy = pad_.ly();
        const float len = std::sqrt(sx * sx + sy * sy);
        if (len > 0.01f) {
            const float speed = 1500.0f * (float(window_.height()) / 900.0f) * std::min(1.0f, len) * dt_;
            float x = pad_x_, y = pad_y_;
            if (!automated_) {
                // From wherever the pointer is when the mouse has moved it since; else from where
                // the stick left it, to the fraction of a pixel (Windows keeps whole ones, and a
                // gentle push moves less than one a frame).
                float rx = x, ry = y;
                (void)window_.pointer(rx, ry);
                if (x < 0 || std::fabs(rx - std::floor(x)) > 1.5f || std::fabs(ry - std::floor(y)) > 1.5f) x = rx, y = ry;
            } else if (x < 0) {
                x = float(window_.width()) * 0.5f, y = float(window_.height()) * 0.5f;
            }
            x = std::clamp(x + sx * speed, 0.0f, float(window_.width() - 1));
            y = std::clamp(y - sy * speed, 0.0f, float(window_.height() - 1));
            pad_x_ = x, pad_y_ = y;
            if (automated_) ui_.set_test_pointer(x, y);   // no real pointer is ever moved by a test
            else window_.set_pointer(x, y);
        }
        if (const float wheel = pad_.ry(); std::fabs(wheel) > 0.01f) io.AddMouseWheelEvent(0, wheel * 14.0f * dt_);
    }
    // The shake runs out.
    const bool shaking = pad_live_ && settings_.pad.vibration && now_ < rumble_until_;
    const float want = shaking ? rumble_ : 0.0f;
    if (want != rumble_sent_) {
        pad_.rumble(want * 0.6f, want);
        rumble_sent_ = want;
    }
}

// ── Automated walk: every screen with a server of our own, photographed ─────────

void App::pad_feed() { pad_.feed(&test_pad_); }

bool App::pad_point(float page_x, float page_y) {
    const VanVec2 want = ui::pg(page_x, page_y);
    float x = pad_x_, y = pad_y_;
    if (x < 0) x = float(window_.width()) * 0.5f, y = float(window_.height()) * 0.5f;
    const float dx = want.x - x, dy = want.y - y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const u32 held = test_pad_.buttons;
    test_pad_ = {};
    test_pad_.buttons = held;
    if (dist > 2.5f) {
        // Hard over from afar, a nudge close by.
        const float push = std::clamp(dist / 160.0f, 0.06f, 1.0f);
        test_pad_.lx = dx / dist * push;
        test_pad_.ly = -dy / dist * push;
    }
    pad_feed();
    return dist <= 2.5f;
}

// The --shop tour's: the shop as it was before it (put back at the end), and the matches this PC
// kept before its match.
namespace {
std::optional<ShopConfig> g_tour_shop;
int g_tour_kept = 0;
}  // namespace

void App::autotest_step() {
    ScreenState& st = *state_;
    const double since = now_ - autotest_at_;
    const bool first = autotest_fresh_;
    autotest_fresh_ = false;
    auto next = [&](int stage) {
        autotest_stage_ = stage;
        autotest_at_ = now_;
        autotest_fresh_ = true;
    };
    switch (autotest_stage_) {
        case 0:
            if (screen_ == Screen::Boot && boot_progress_ > 0.3f && since > 0.4) {
                shot("01_boot");
                next(1);
            }
            break;
        case 1:
            if (screen_ == Screen::Servers && since > 1.2) {
                shot("02_servers");
                next(2);
            }
            break;
        case 2:
            // A Team Vanilla account of the test's own (on the test's own TVAS: --tvas).
            st.account = opts_.user.empty() ? "autotest" : opts_.user;
            st.password = opts_.pass.empty() ? "LsfAutotest#1" : opts_.pass;
            st.password2 = st.password;
            st.registering = true;
            st.login_open = true;
            ui::open_modal("Sign in");
            next(3);
            break;
        case 3:
            if (since > 0.8) {
                shot("03_sign_in");
                // A new account the first time; a second run signs in to it (a refusal is expected and ignored).
                sign_in(true);
                next(4);
            }
            break;
        case 4:
            // The account from an earlier run: sign in to it instead.
            if (!st.login_error.empty()) {
                const bool taken = st.login_error.find("taken") != std::string::npos;
                if (!taken) LOG_ERROR("autotest: signing in failed: %s  <-- FAILED", st.login_error.c_str());
                st.login_error.clear();
                if (taken) {
                    st.registering = false;
                    sign_in(false);
                }
            }
            if (screen_ == Screen::CodeName && since > 1.0 && !autotest_named_) {
                shot("04_code_name");
                session_.set_code_name(opts_.user.empty() ? std::string("Autotest") : opts_.user, 0);
                autotest_named_ = true;
            }
            if (session_.tv.signed_in && !session_.tv.code_name.empty() && screen_ == Screen::Servers && !st.joining && !session_.signed_in() && since > 0.5) {
                // The test's own Team Vanilla makes the test's account SFLegacy staff, so a tour can
                // give itself what it shows (only a TVAS on this machine has that door: /v1/dev).
                if (tvas::loopback(session_.tvas().address()) && !autotest_promoted_) {
                    autotest_promoted_ = true;
                    eng::json::Value b;
                    b["username"] = st.account;
                    b["role"] = 2;
                    session_.tvas().post("/v1/dev/role", b, {});
                    autotest_at_ = now_;
                    break;
                }
                // --server: a LegacySFServer somewhere else instead of this PC's own (a phone or a Mac
                // has none of its own: without --server its list may be empty).
                if (!opts_.server.empty()) connect(direct_server(ServerEntry{"Test server", opts_.server}));
                else if (!servers().empty()) connect(servers().front());
                else {
                    LOG_ERROR("autotest: no server to join (this game hosts none: name one with --server)  <-- FAILED");
                    quit_ = true;
                    break;
                }
                next(5);
            }
            break;
        case 5:
            // A server with packs asks before it downloads (§11.7): photographed, then a yes. While
            // the packs come down and are checked, the run is not stuck.
            if (session_.content && session_.content->ask) {
                shot("04b_packs_asked");
                LOG_INFO("autotest: the server's packs: %zu, %llu bytes to download", session_.content->packs.size(),
                         (unsigned long long)session_.content->need_bytes);
                session_.content_accept();
                autotest_at_ = now_;
                break;
            }
            if (session_.join_stage() == Session::JoinStage::Downloading || session_.join_stage() == Session::JoinStage::Mounting) {
                autotest_at_ = now_;
                break;
            }
            if (screen_ == Screen::Channels && since > 1.2) {
                shot("05_channels");
                session_.join_channel(10);
                next(6);
            }
            break;
        case 6:
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.join) {
                next(40);   // someone else's room
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.modes) {
                next(500);   // --modes: a room of each game type
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.movement) {
                next(700);   // --movement: a ladder up and down, a flight of stairs
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.killmarks) {
                next(950);   // --killmarks: every kill mark, one a picture (pictures K*)
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.social) {
                next(900);   // --social: friends, messages, chat and the clan (pictures S*)
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.shop) {
                next(1000);   // --shop: the shops, sprays, capsules, the staff's Shop, recordings (pictures P*)
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.knife) {
                next(1300);   // --knife: the melee weapon in hand (--weapon picks which) swung, seen both ways (pictures K*)
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.throws) {
                next(1400);   // --throws: three throwables carried, each taken out with the fourth key (pictures T*)
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.games) {
                next(1500);   // --games: game types and a map switched off for the whole server (pictures G*)
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.cannon) {
                next(1200);   // --cannon: one of the Pirate Ship's cannons manned and fired (pictures C*)
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2 && opts_.wear) {
                next(1100);   // --wear: guns' wear, Repair and Sell in the inventory and the room (pictures W*)
                break;
            }
            if (screen_ == Screen::Lobby && since > 1.2) {
                shot("06_lobby");
                // A controller in the menus: its stick takes the pointer to the Options plate and
                // A clicks it. Then every page of the options, the credits, and Make Room.
                if (opts_.pad_test) {
                    next(200);
                } else {
                    open_settings(*this, 0, 0);
                    next(30);
                }
            }
            break;
        case 200:
            // pagecommon's Options tab (the seventh plate of the row), by the pad's pointer.
            if (pad_point(792, 43)) {
                test_pad_.buttons = eng::kPadA;
                pad_feed();
                next(201);
            } else if (since > 8.0) {
                LOG_ERROR("autotest: the pad's pointer never reached the Options plate  <-- FAILED");
                open_settings(*this, 0, 0);
                next(30);
            }
            break;
        case 201:
            if (since > 0.15) {
                test_pad_.buttons = 0;
                pad_feed();
                next(202);
            }
            break;
        case 202:
            if (since > 0.4) {
                LOG_INFO("autotest: the pad's pointer and A (Cross) on the Options plate: the options are %s", st.settings_open ? "open" : "NOT open  <-- FAILED");
                if (!st.settings_open) open_settings(*this, 0, 0);
                st.settings_tab = 0, st.system_sub = 0;
                next(30);
            }
            break;
        case 30: {
            // Every page of the options.
            struct Shown {
                const char* name;
                int tab, sub;
                float px = 0, py = 0;   // where the pointer rests (page units; 0: off the dialog), for the line along the bottom
            };
#ifdef __ANDROID__
            // A phone's also: the touch page, and the arranging screen with Fire taken somewhere else.
            static const Shown pages[] = {{"06b_options_display", 0, 0},   {"06b2_options_graphics", 0, 1, 250, 315}, {"06b3_options_sound", 0, 2},
                                          {"06b4_options_game", 0, 3, 250, 320}, {"06b5_options_crosshair", 0, 4}, {"06c_options_controls", 1, 0},
                                          {"06c2_options_controller", 1, 1}, {"06c3_options_touch", 1, 4}, {"06c4_touch_arrange", 1, 4},
                                          {"06d_options_radio", 1, 2}, {"06e_options_macro", 2, 0}};
#else
            static const Shown pages[] = {{"06b_options_display", 0, 0},   {"06b2_options_graphics", 0, 1, 250, 315}, {"06b3_options_sound", 0, 2},
                                          {"06b4_options_game", 0, 3, 250, 320}, {"06b5_options_crosshair", 0, 4}, {"06c_options_controls", 1, 0},
                                          {"06c2_options_controller", 1, 1}, {"06d_options_radio", 1, 2}, {"06e_options_macro", 2, 0}};
#endif
            static size_t at = 0;
            if (pages[at].px > 0) ui_.set_test_pointer(ui::pg(pages[at].px, pages[at].py).x, ui::pg(pages[at].px, pages[at].py).y);
            else ui_.set_test_pointer(-FLT_MAX, -FLT_MAX);
            if (since > 0.7) {
                shot(pages[at].name);
                if (++at == std::size(pages)) {
                    st.credits_open = true, st.credits_since = now_, st.credits_scroll = 0, st.credits_by_hand = false;
                    next(31);
                } else {
                    st.settings_tab = pages[at].tab;
                    (pages[at].tab == 0 ? st.system_sub : st.settings_sub) = pages[at].sub;
                    // The arranging screen: Fire moved in from its corner and made bigger, picked.
                    TouchPlace& fire = st.edit.touch.places[size_t(TouchPart::Fire)];
                    st.touch_arrange = std::string_view(pages[at].name) == "06c4_touch_arrange";
                    fire = st.touch_arrange ? TouchPlace{0.70f, 0.55f, 1.3f} : TouchPlace{};
                    st.touch_picked = int(TouchPart::Fire);
                    autotest_at_ = now_;
                }
            }
            break;
        }
        case 31:
            if (since > 0.8) {
                shot("06h_credits");
                LOG_INFO("autotest: the credits name %d who donated and %d beta testers", credits_names("DONATED DURING DEVELOPMENT"), credits_names("BETA TESTERS"));
                st.credits_scroll = 470, st.credits_by_hand = true;
                next(32);
            }
            break;
        case 32:
            if (since > 0.5) {
                shot("06i_credits_names");
                st.credits_scroll = 1e6f;
                next(33);
            }
            break;
        case 33:
            if (since > 0.5) {
                shot("06j_credits_end");
                // Back to the options, on Graphics, for what they do not list: seven taps on the heading.
                st.credits_open = false;
                st.settings_tab = 0, st.system_sub = 1, st.fidelity_taps = 0;
                next(203);
            }
            break;
        case 203:
        case 204: {
            // The word GRAPHICS, by the pad's pointer when there is one (else as seven clicks counted).
            static int taps = 0;
            const bool found = st.edit.fidelity_found;
            if (found || taps > 10 || since > 12.0) {
                LOG_INFO("autotest: seven taps on GRAPHICS: Fidelity is %s", found ? "found" : "NOT found  <-- FAILED");
                test_pad_ = {};
                pad_feed();
                next(205);
                break;
            }
            if (!opts_.pad_test) {
                // No pad: six taps taken as made, the pointer put on the word for the seventh.
                if (since > 0.4) st.fidelity_taps = 6, ui_.set_test_pointer(ui::pg(232, 246).x, ui::pg(232, 246).y), next(206);
                break;
            }
            if (autotest_stage_ == 203) {
                if (pad_point(232, 246) && since > 0.12) {
                    test_pad_.buttons = eng::kPadA;
                    pad_feed();
                    next(204);
                }
            } else if (since > 0.08) {
                test_pad_.buttons = 0;
                pad_feed();
                ++taps;
                next(203);
            }
            break;
        }
        case 206:
            if (since > 0.1) VanGui::GetIO().AddMouseButtonEvent(0, true), next(207);
            break;
        case 207:
            if (since > 0.1) VanGui::GetIO().AddMouseButtonEvent(0, false), next(208);
            break;
        case 208:
            if (since > 0.3) {
                LOG_INFO("autotest: the seventh tap on GRAPHICS: Fidelity is %s", st.edit.fidelity_found ? "found" : "NOT found  <-- FAILED");
                next(205);
            }
            break;
        case 205:
            if (since > 0.6) {
                shot("06k_options_fidelity");
                st.settings_open = false;
                next(34);
            }
            break;
        case 34:
            // Clans: the mark builder, found one, see it, close it again (so the next run starts clanless).
            if (since > 0.5) {
                st.clan_open = true;
                st.clan_layer = 2;
                st.clan_mark = {7, 4, 20};
                st.clan_name = "Night Owls";
                st.clan_notice = "Scrims on Fridays.";
                session_.request_clan();
                next(35);
            }
            break;
        case 35:
            if (since > 1.2 && session_.clan) {
                shot("06f_clan_found");
                if (session_.clan->member) session_.leave_clan();   // a clan left over from a failed run
                else session_.create_clan(st.clan_name, st.clan_notice, st.clan_mark);
                next(36);
            }
            break;
        case 36:
            if (since > 1.0 && session_.clan && session_.clan->member) {
                shot("06g_clan");
                next(300);
            } else if (since > 4.0) {
                next(37);
            }
            break;
        case 37:
            if (since > 0.3) {
                if (session_.clan && session_.clan->member) session_.leave_clan();
                st.clan_open = false;
                next(38);
            }
            break;
        case 38:
            if (since > 0.5) {
                st.create.title = "Autotest room";
                st.create.map = opts_.map.empty() ? "crossroad" : opts_.map;
                st.create.time_of_day = opts_.daytime == "night" ? TimeOfDay::Night
                                        : opts_.daytime == "day" ? TimeOfDay::Day
                                                                 : baked_time_of_day(st.create.map);
                st.create_hour_set = !opts_.daytime.empty();
                st.create_open = true;
                next(7);
            }
            break;
        case 7:
            if (since > 1.0) {
                shot("07_make_room");
                RoomSettings rs;
                rs.title = "Autotest room";
                rs.mode = Mode::TeamDeathmatch;
                rs.map = opts_.map.empty() ? "crossroad" : opts_.map;
                // --packs: the joined server's own map, gun and character (Docs/UniversalServerDeploy.md §11).
                std::string want_code = opts_.weapon;
                if (opts_.packs) {
                    const SessionContent& c = registry::content();
                    if (c.maps.empty() && c.weapons.empty() && c.forces.empty()) LOG_ERROR("autotest: --packs, and this server has none  <-- FAILED");
                    if (opts_.map.empty() && !c.maps.empty()) rs.map = c.maps.front().id;
                    for (const WeaponDef& w : c.weapons)
                        if (want_code.empty() && w.model.starts_with("x/")) want_code = w.code;
                    if (want_code.empty() && !c.weapons.empty()) want_code = c.weapons.front().code;
                    if (!c.forces.empty() && !session_.owns_force(c.forces.front().def.id)) session_.buy(proto::ShopKind::Force, c.forces.front().def.id);
                    LOG_INFO("autotest: packs: the map %s (%s), the gun %s, the character %s", rs.map.c_str(), map_title(rs.map).c_str(), want_code.c_str(),
                             c.forces.empty() ? "none" : c.forces.front().code.c_str());
                }
                // A map made for one game type (the Pirate Ship, the horror maps) is played as that.
                for (Mode m : {Mode::TeamDeathmatch, Mode::Pirate, Mode::Horror2, Mode::Horror, Mode::Occupy, Mode::TeamBattle}) {
                    const auto ids = maps_for(m);
                    if (std::find(ids.begin(), ids.end(), rs.map) != ids.end()) {
                        rs.mode = m;
                        break;
                    }
                }
                rs.goal = 100;
                rs.minutes = 10;
                rs.time_of_day = st.create.time_of_day;
                st.create_open = false;
                VanGui::CloseCurrentPopup();
                // --weapon: that gun in hand for the match (equipped when owned, else bought,
                // which equips it).
                if (!want_code.empty()) {
                    const WeaponDef* want = nullptr;
                    for (const WeaponDef* w : session_weapons())
                        if (eng::str::iequals(w->code, want_code)) want = w;
                    if (!want) {
                        LOG_ERROR("autotest: no weapon %s", want_code.c_str());
                    } else if (std::find(session_.owned_weapons.begin(), session_.owned_weapons.end(), want->id) != session_.owned_weapons.end()) {
                        auto lo = session_.profile.loadout;
                        lo[equip_cell(lo, *want)] = want->id;
                        session_.set_loadout(session_.profile.force, lo);
                        LOG_INFO("autotest: carrying the %s (%s)", want->name.c_str(), want->code.c_str());
                    } else {
                        if (session_.profile.sp < want->price) session_.recharge_sp();
                        session_.buy(proto::ShopKind::Weapon, want->id);
                        LOG_INFO("autotest: buying the %s (%s) to carry it", want->name.c_str(), want->code.c_str());
                    }
                }
                session_.create_room(rs);
                next(8);
            }
            break;
        case 8:
            if (screen_ == Screen::Room && since > 1.5) {
                // --packs: the server's own character, once it is owned.
                if (opts_.packs && !registry::content().forces.empty()) {
                    const u8 mine = registry::content().forces.front().def.id;
                    if (session_.owns_force(mine)) session_.set_loadout(mine, session_.profile.loadout);
                    else LOG_ERROR("autotest: the pack's character was not bought  <-- FAILED");
                }
                shot("08_room");
                st.shop_return = Screen::Room;
                go(Screen::Shop);
                next(9);
            }
            break;
        case 9:
            if (screen_ == Screen::Shop && since > 1.2) {
                shot("09_shop");
                // The Character Shop, previewing one of the forces.
                st.shop_tab = 1;
                st.view_force = st.shop_sel = int(forces()[std::min<size_t>(4, forces().size() - 1)].id);
                if (opts_.packs && !registry::content().forces.empty()) st.view_force = st.shop_sel = int(registry::content().forces.front().def.id);
                next(20);
            }
            break;
        case 20: {
            bool all = true;
            for (const ForceDef& f : forces()) all &= !stage_->loading(f.id);
            if ((all && since > 1.0) || since > 15.0) {
                shot("09b_character_shop");
                st.shop_tab = 2;   // the inventory: weapons, then characters
                st.inv_tab = 1;
                st.shop_sel = -1;
                st.view_force = -1;
                next(22);
            }
            break;
        }
        case 22:
            if (since > 1.0) {
                shot("09d_inventory_weapons");
                st.inv_tab = 0;
                next(23);
            }
            break;
        case 23:
            if (since > 1.0) {
                shot("09e_inventory_characters");
                // Soldier Front's items: the Item Shop, a purchase's days, two bought.
                st.shop_tab = 3;
                st.item_sel = 1010;   // Points X2
                next(24);
            }
            break;
        case 24:
            if (since > 1.0) {
                shot("09f_item_shop");
                st.buy_item = 1010;
                st.buy_offer = 1;
                next(25);
            }
            break;
        case 25:
            if (since > 0.8) {
                shot("09g_buy_days");
                st.buy_item = -1;
                session_.buy(proto::ShopKind::Item, 1010, 0);
                session_.buy(proto::ShopKind::Item, 1031, 0);   // Colored Codename
                st.shop_tab = 2;
                st.inv_tab = 2;
                st.item_sel = 1031;
                next(26);
            }
            break;
        case 26:
            if (since > 1.2) {
                shot("09h_inventory_items");
                st.use_item = 1031;
                st.use_value = 2;
                next(27);
            }
            break;
        case 27:
            if (since > 0.8) {
                shot("09i_use_colour");
                st.use_item = -1;
                session_.use_item(1031, 2);
                // The parts: Delta Force (ARTC, the trainees' force, has none in the client's tables),
                // its Character Shop's Torso tab, the cheapest armoured piece bought (it goes on).
                constexpr u8 kDelta = 1;
                if (!session_.owns_force(kDelta)) session_.buy(proto::ShopKind::Force, kDelta);
                else session_.set_loadout(kDelta, session_.profile.loadout);
                st.shop_tab = 1;
                st.char_tab = 3;
                st.view_force = kDelta;
                const ItemDef* part = nullptr;
                for (const ItemDef& d : items())
                    if (d.kind == ItemKind::Part && d.force == kDelta && std::string_view(d.tab) == "Torso" && d.upper.any() &&
                        (!part || d.offers[0].price < part->offers[0].price))
                        part = &d;
                if (part) {
                    session_.buy(proto::ShopKind::Item, part->id, 0);
                    st.item_sel = part->id;
                    LOG_INFO("autotest: buying the part %s (%s)", part->name, part->code);
                }
                next(28);
            }
            break;
        case 28:
            if (since > 1.5) {
                shot("09j_charshop_torso");
                st.shop_tab = 2;
                st.inv_tab = 0;
                st.inv_sub = 3;
                next(29);
            }
            break;
        case 29:
            if (since > 1.2) {
                shot("09k_parts_worn");
                st.shop_tab = 0;
                st.inv_tab = 1;
                st.inv_sub = 0;
                st.item_sel = -1;
                st.inv_snapshot.reset();
                go(Screen::Room);
                next(21);
            }
            break;
        case 21:
            if (screen_ == Screen::Room && since > 1.0) {
                shot("09c_room_soldier");
                next(10);
            }
            break;
        // ── --join: another player in the host's room, on the host's side ──
        case 40:
            if (screen_ == Screen::Lobby && !session_.rooms.empty() && since > 0.5) {
                session_.join_room(session_.rooms.begin()->first);
                next(41);
            }
            break;
        case 41:
            if (screen_ == Screen::Room && since > 1.0 && session_.room) {
                shot("08_room_joined");
                // Beside the host (the first seat's side), ready, and waiting for the start.
                // --watch: across from the host instead (a team game wants both sides filled).
                for (const auto& m : session_.room->members)
                    if (m.host) session_.set_team(!opts_.watch ? Team(m.team) : Team(m.team) == Team::Red ? Team::Blue : Team::Red);
                session_.set_ready(true);
                next(11);
            }
            break;
        case 10:
            if (screen_ == Screen::Room && since > 0.6 && session_.room && int(session_.room->members.size()) < opts_.expect && since < 35.0)
                break;   // --expect: the others first
            if (screen_ == Screen::Room && since > 0.6) {
                // --weapon: in hand for the match (the parts bought since may have saved the old loadout).
                static bool carried = false;
                static int buys = 0;
                if (!opts_.weapon.empty() && !carried)
                    for (const WeaponDef& w : weapons()) {
                        if (!eng::str::iequals(w.code, opts_.weapon)) continue;
                        const bool owned = std::find(session_.owned_weapons.begin(), session_.owned_weapons.end(), w.id) != session_.owned_weapons.end();
                        if (!owned && buys < 3) {
                            // The first purchase can beat the SP it was recharged with to the server.
                            ++buys;
                            if (session_.profile.sp < w.price) session_.recharge_sp();
                            session_.buy(proto::ShopKind::Weapon, w.id);
                            LOG_INFO("autotest: buying the %s (%s) again", w.name.c_str(), w.code.c_str());
                            autotest_at_ = now_;
                            return;
                        }
                        if (owned && !in_kit(session_.profile.loadout, w.id)) {
                            auto lo = session_.profile.loadout;
                            lo[equip_cell(lo, w)] = w.id;
                            session_.set_loadout(session_.profile.force, lo);
                            LOG_INFO("autotest: carrying the %s (%s)", w.name.c_str(), w.code.c_str());
                            autotest_at_ = now_;
                            carried = true;
                            return;   // the room hears of it before the match starts
                        }
                    }
                session_.start_match();
                next(11);
            }
            break;
        case 11:
            // A small map can load within a frame or two, so photograph the first loading frame
            // and carry on if the match is already up.
            if (screen_ == Screen::Loading) {
                shot("10_loading");
                next(12);
            } else if (screen_ == Screen::Match) {
                LOG_INFO("autotest: the map loaded before the loading screen could be photographed");
                next(12);
            }
            break;
        case 12:
            if (screen_ == Screen::Match && since > 2.0 && opts_.view_glow) {
                next(75);   // --view-glow: the map's glows, close up
                break;
            }
            if (screen_ == Screen::Match && since > 2.0 && opts_.watch) {
                next(70);   // --watch: the others, as this client draws them
                break;
            }
            // --stand: on the floor under a point of the map, looking a given way (a flag, a sky, a prop).
            if (screen_ == Screen::Match && world_ && world_->ready() && opts_.stand) {
                static double stood = -1;
                if (stood < 0) stood = now_;
                world_->test_stand(eng::Vec3{opts_.stand_at[0], opts_.stand_at[1], opts_.stand_at[2]}, opts_.stand_at[3], opts_.stand_at[4]);
                if (now_ - stood < 1.0) break;
                if (since > 2.5) {
                    shot("11_match");
                    quit_ = true;
                    break;
                }
            }
            // --spot: from a fixed place, held there for a moment before the picture is taken.
            if (screen_ == Screen::Match && world_ && world_->ready() && opts_.spot >= 0) {
                static double placed = -1;
                if (placed < 0) placed = now_;
                world_->test_place(opts_.spot, opts_.spot_yaw, opts_.spot_back, opts_.spot_sun);
                if (opts_.spot_shadow) {
                    world_->test_shadow_view();
                    static bool said = false;
                    if (!said && now_ - placed > 0.9)
                        LOG_INFO("autotest: soldiers' shadows: %s, the sun %s", Settings::shadows_key(settings_.shadows),
                                 world_->shadows_cast() ? "casts" : "casts none (no sun to speak of, or off)"),
                            said = true;
                }
                if (opts_.spot_water) {
                    static bool said = false;
                    const bool found = world_->view_water();
                    if (!said) LOG_INFO("autotest: the map's water is %s", found ? "found: facing it" : "NOT found");
                    said = true;
                }
                if (now_ - placed < 1.0) break;
            }
            if (screen_ == Screen::Match && since > 2.5 && opts_.pad_test && world_) {
                shot("11_match");
                next(210);
                break;
            }
            if (screen_ == Screen::Match && since > 2.5) {
                shot("11_match");
                if (settings_.fidelity && world_)
                    LOG_INFO("autotest: Fidelity: surfaces %s, %d lights drawn", pipe_.surfaces() ? "on" : "off", pipe_.lights_drawn());
                // Run and hold the trigger through a magazine and its reload, and record it all.
                mixer_.start_capture(std::filesystem::path(opts_.autotest) / "match_audio.wav");
                LOG_INFO("autotest: recording the match's sound from t %.3f", eng::time::now());
                float peak, rms;
                mixer_.take_levels(peak, rms);
                if (world_) world_->autopilot(true, false);   // footsteps first, on their own
                if (world_) world_->test_radio(0, 0);           // "Go go go!", to the team
                next(16);
            }
            break;
        case 16:
            if (since > 1.5) {
                if (world_) world_->autopilot(true, true);
                next(13);
            }
            break;
        case 13:
            if (screen_ == Screen::Match && since > 3.0) {
                shot("12_match_2");
                // Turned round and up a little: the sky and the far side of the map.
                if (world_) world_->look(world_->view_yaw() + 160.0f, 14.0f), world_->open_radio(1);
                next(15);
            }
            break;
        case 15:
            if (screen_ == Screen::Match && since > 0.5) {
                shot("13_match_sky");
                if (opts_.quick) {
                    next(14);
                    break;
                }
                // The HUD's kill art, fed a head-shot kill and then a four-kill streak.
                if (world_) world_->test_hud(0);
                next(17);
            }
            break;
        case 17:
            if (screen_ == Screen::Match && since > 0.3) {
                shot("14_hud_headshot");
                if (world_) world_->test_hud(1);
                next(18);
            }
            break;
        case 18:
            if (screen_ == Screen::Match && since > 0.3) {
                shot("15_hud_streak");
                // Running, jumping, turning and firing all at once: the mouse and the trigger must
                // work in the air as on the ground (a player report).
                if (world_) world_->test_full_clip(), world_->look(world_->view_yaw(), 0), world_->autopilot(true, true),
                    world_->autopilot_jump(true), world_->autopilot_turn(90.0f);
                next(50);
            }
            break;
        case 50:
        case 51: {
            // 50: waiting to leave the ground; 51: in the air, until the feet are down again.
            static float yaw0 = 0;
            static int shots0 = 0, airborne_frames = 0;
            static bool photographed = false;
            if (!world_) break;
            if (autotest_stage_ == 50 && !world_->on_ground()) {
                yaw0 = world_->view_yaw(), shots0 = world_->shots_fired(), airborne_frames = 0, photographed = false;
                next(51);
            } else if (autotest_stage_ == 51) {
                ++airborne_frames;
                if (!photographed && since > 0.15) shot("16_air_fire"), photographed = true;
                if (world_->on_ground()) {
                    const float turned = std::fabs(eng::wrap_degrees(world_->view_yaw() - yaw0));
                    const int shots = world_->shots_fired() - shots0;
                    LOG_INFO("autotest: in the air %.2f s (%d frames): turned %.1f degrees, fired %d shots%s", since, airborne_frames, double(turned),
                             shots, turned > 5.0f && shots > 0 ? "" : "  <-- FAILED");
                    world_->autopilot_jump(false), world_->autopilot_turn(0), world_->autopilot(false, true);
                    next(52);
                }
            }
            break;
        }
        case 52:
            // Standing and firing: photographed in a frame with the flash, and the streak's start
            // measured against the drawn barrel.
            if (world_ && since > 0.4 && (now_ - world_->last_shot_at()) < 0.012) {
                shot("17_muzzle_flash");
                if (settings_.fidelity) LOG_INFO("autotest: Fidelity: %d lights on the muzzle flash's frame (the most so far %d)", pipe_.lights_drawn(), pipe_.lights_peak());
                LOG_INFO("autotest: barrel drawn and tracer start %.1f px apart (0 = from the muzzle)", double(world_->tracer_gap_px()));
                world_->face_open_space();
                world_->autopilot(true, false);
                world_->toggle_third_person();
                world_->test_orbit(65.0f);
                next(53);
            } else if (since > 6.0) {
                LOG_ERROR("autotest: no shot left the gun to photograph");
                next(53);
            }
            break;
        case 53:
            if (world_ && since > 1.2) {
                shot("18_third_run");
                LOG_INFO("autotest: running, your body plays %s", world_->body_clips().c_str());
                world_->autopilot(false, false), world_->autopilot_side(1.0f);
                next(54);
            }
            break;
        case 54:
            if (world_ && since > 1.0) {
                shot("19_third_strafe");
                LOG_INFO("autotest: strafing, your body plays %s", world_->body_clips().c_str());
                world_->autopilot(false, true);
                next(55);
            }
            break;
        case 55:
            if (world_ && since > 0.5 && (now_ - world_->last_shot_at()) < 0.012) {
                shot("20_third_fire");
                LOG_INFO("autotest: strafing and firing, your body plays %s", world_->body_clips().c_str());
                world_->face_open_space();
                world_->autopilot(true, false), world_->autopilot_side(0), world_->autopilot_jump(false, true);
                next(56);
            } else if (since > 6.0) {
                next(56);
            }
            break;
        case 56:
            if (world_ && since > 1.2) {
                shot("21_third_crouch_walk");
                LOG_INFO("autotest: crouch-walking, your body plays %s", world_->body_clips().c_str());
                world_->autopilot_jump(true, false);
                next(57);
            }
            break;
        case 57:
            if (world_ && !world_->on_ground() && since > 0.1) {
                shot("22_third_jump");
                LOG_INFO("autotest: jumping, your body plays %s", world_->body_clips().c_str());
                world_->autopilot_jump(false), world_->autopilot(false, false);
                next(58);
            } else if (since > 5.0) {
                next(58);
            }
            break;
        case 58:
            if (world_ && since > 1.5) {
                shot("23_third_stand");
                LOG_INFO("autotest: standing, your body plays %s", world_->body_clips().c_str());
                world_->test_orbit(0);
                world_->toggle_third_person();
                // Firing at the floor a few metres ahead: spent cases, bullet marks, dust.
                world_->test_full_clip();
                world_->look(world_->view_yaw(), -35.0f);
                world_->autopilot(false, true);
                next(60);
            }
            break;
        case 60:
            if (world_ && since > 0.35 && (now_ - world_->last_shot_at()) < 0.03) {
                shot("24_cases_marks");
                LOG_INFO("autotest: firing at the floor: %s", world_->effects_summary().c_str());
                LOG_INFO("autotest: a burst in: %s", world_->recoil_summary().c_str());
                next(61);
            } else if (since > 6.0) {
                next(61);
            }
            break;
        case 61:
            if (world_ && since > 0.8) {
                world_->autopilot(false, false);
                shot("25_marks_left");
                LOG_INFO("autotest: trigger let go 0.8 s: %s", world_->recoil_summary().c_str());
                LOG_INFO("autotest: after firing: %s", world_->effects_summary().c_str());
                // The Esc menu: its buttons must be drawn over its panel.
                world_->test_menu(true);
                next(62);
            }
            break;
        case 62:
            if (world_ && since > 0.5) {
                shot("26_menu");
                world_->test_menu(false);
                world_->face_open_space();
                world_->look(world_->view_yaw(), -18.0f);   // onto the floor a few metres off, in view
                if (!world_->test_throw()) {
                    LOG_ERROR("autotest: no grenade in the loadout to throw");
                    next(14);
                } else {
                    next(63);
                }
            }
            break;
        case 63:
            // The pin pulled (the hold clip), then let go: photographed in flight.
            if (world_ && world_->grenades_in_air() > 0 && since > 0.35) {
                shot("27_grenade_flight");
                LOG_INFO("autotest: thrown: %s", world_->effects_summary().c_str());
                next(64);
            } else if (since > 4.0) {
                LOG_ERROR("autotest: the grenade never left the hand  <-- FAILED");
                next(14);
            }
            break;
        case 64:
            if (world_ && world_->blast_count() > 0 && since > 0.1) {
                static double seen = -1;
                if (seen < 0) seen = now_;
                if (now_ - seen > 0.18) {
                    shot("28_grenade_blast");
                    if (settings_.fidelity) LOG_INFO("autotest: Fidelity: %d lights on the blast's frame (the most so far %d)", pipe_.lights_drawn(), pipe_.lights_peak());
                    LOG_INFO("autotest: went off: %s", world_->effects_summary().c_str());
                    seen = -1;
                    next(65);
                }
            } else if (since > 8.0) {
                LOG_ERROR("autotest: the grenade never went off  <-- FAILED");
                next(14);
            }
            break;
        case 65:
            if (world_ && since > 0.7) {
                shot("29_grenade_plume");
                // A hit's blood in front of the wall ahead, and splashed on it.
                world_->look(world_->view_yaw(), 0.0f);
                if (!world_->test_blood()) LOG_ERROR("autotest: no wall ahead for the blood");
                next(66);
            }
            break;
        case 66:
            if (world_ && since > 0.12) {
                shot("30_blood");
                LOG_INFO("autotest: blood: %s", world_->effects_summary().c_str());
                // A smoke grenade onto the floor ahead.
                world_->look(world_->view_yaw(), -18.0f);
                if (!world_->test_throw_kind(GrenadeKind::Smoke)) {
                    LOG_ERROR("autotest: no smoke grenade to throw  <-- FAILED");
                    next(68);
                } else {
                    next(67);
                }
            }
            break;
        case 67: {
            static double popped = -1;
            if (!world_) break;
            if (popped < 0 && world_->smoke_count() > 0) popped = now_, LOG_INFO("autotest: smoke went off: %s", world_->effects_summary().c_str());
            if (popped >= 0 && now_ - popped > 1.0 && now_ - popped < 1.2) shot("31_smoke_building");
            if (popped >= 0 && now_ - popped > 4.0) {
                shot("32_smoke_full");
                popped = -1;
                next(68);
            } else if (since > 10.0) {
                LOG_ERROR("autotest: the smoke grenade never went off  <-- FAILED");
                next(68);
            }
            break;
        }
        case 68:
            if (world_ && since > 0.3) {
                world_->look(world_->view_yaw() + 90.0f, -18.0f);   // clear of the smoke
                next(78);
            }
            break;
        case 78:
            // Thrown once the view has turned (the throw leaves along the camera as last drawn).
            if (world_ && since > 0.3) {
                if (!world_->test_throw_kind(GrenadeKind::Flash)) {
                    LOG_ERROR("autotest: no flash-bang to throw  <-- FAILED");
                    next(80);
                } else {
                    next(69);
                }
            }
            break;
        case 69: {
            static double popped = -1;
            if (!world_) break;
            if (popped == -1 && world_->blast_count() > 0) popped = now_;
            if (popped >= 0 && now_ - popped > 0.06 && now_ - popped < 0.2) {
                shot("33_flash_bang");
                LOG_INFO("autotest: flash-bang: %s; you are %s", world_->effects_summary().c_str(), world_->flashed() ? "blinded" : "NOT blinded  <-- FAILED");
                popped = -10;   // photographed
            }
            if (popped == -10 && since > 2.2) {
                shot("34_flash_fading");
                popped = -1;
                next(80);
            } else if (since > 10.0) {
                LOG_ERROR("autotest: the flash-bang never went off  <-- FAILED");
                popped = -1;
                next(80);
            }
            break;
        }
        case 80:
        case 81:
        case 82:
            // The scope: in (a sniper rifle's first step), all the way, out again (once the
            // flash-bang's white has gone).
            if (world_ && world_->flashed()) autotest_at_ = now_;
            else if (world_ && since > 0.6) {
                const int s = world_->scoped_slot();
                if (s < 0) {
                    LOG_INFO("autotest: nothing scoped carried (--weapon A009 brings the PSG-1)");
                    next(90);
                    break;
                }
                if (autotest_stage_ > 80) {
                    shot(autotest_stage_ == 81 ? "35_scope_first" : "36_scope_full");
                    LOG_INFO("autotest: scope step %d: field of view %.1f degrees", autotest_stage_ - 80, double(world_->view_fov()));
                }
                if (autotest_stage_ == 82) {
                    world_->test_scope(s, 0);
                    next(90);
                } else {
                    world_->look(world_->view_yaw(), 0.0f);
                    world_->test_scope(s, autotest_stage_ - 80 + 1);
                    next(autotest_stage_ + 1);
                }
            }
            break;
        // Staff: the Esc menu's new lines, the panel's three tabs in the match, then out of the match
        // to watch its recording back (Chase, Eyes, a seek and Free), and back to the room.
        case 90:
            // A death staged (nobody here to do it), its screen, and a round's end.
            if (world_ && since > 0.4) {
                world_->test_death();
                next(86);
            }
            break;
        case 86:
            if (world_ && since > 0.6) {
                shot("45_death");
                next(87);
            }
            break;
        case 87:
            if (world_ && since > 2.6) {
                shot("46_death_respawning");
                world_->test_round_end();
                next(88);
            }
            break;
        case 88:
            if (world_ && since > 1.2) {
                shot("47_round_summary");
                world_->test_menu(true);
                next(91);
            }
            break;
        case 91:
            if (world_ && since > 0.5) {
                shot("37_menu_staff");
                world_->test_menu(false);
                st.staff_open = true, st.staff_tab = 0, st.staff_asked = false;
                next(92);
            }
            break;
        case 92:
            if (since > 1.0) {
                shot("38_staff_reports");
                st.staff_tab = 1, st.staff_search = st.account;
                session_.staff_find(st.account);
                next(93);
            }
            break;
        case 93:
            if (since > 1.0) {
                shot("39_staff_account");
                // The Owner's and Admins' channels and message of the day (§10.2, ML-8).
                st.staff_tab = 6, st.staff_motd_loaded = false;
                if (!session_.channels.empty()) {
                    st.staff_channel = int(session_.channels.front().id);
                    st.staff_channel_edit = session_.channels.front();
                    st.staff_channel_maps.clear();
                    st.staff_channel_loaded = true;
                }
                next(970);
            }
            break;
        case 970:
            if (since > 1.0) {
                shot("39b_staff_channels");
                // SFLegacy Staff's reach: the account itself, at TVAS (RL-1).
                st.staff_tab = 1, st.staff_global = true;
                session_.staff_card.reset();
                session_.global_staff_find(st.account);
                next(971);
            }
            break;
        case 971:
            if (since > 1.2) {
                shot("39c_staff_account_tv");
                LOG_INFO("autotest: staff: Team Vanilla's card %s", session_.staff_card ? session_.staff_card->account.c_str() : "(none)");
                st.staff_global = false;
                st.staff_tab = 7, st.staff_asked = false;
                next(972);
            }
            break;
        case 972:
            if (since > 1.2) {
                shot("39d_staff_servers");
                LOG_INFO("autotest: staff: %zu servers registered", session_.staff_servers ? session_.staff_servers->size() : size_t(0));
                st.staff_tab = 2, st.staff_asked = false;
                next(94);
            }
            break;
        case 94:
            if (since > 1.0) {
                shot("40_staff_recordings");
                st.staff_open = false;
                leave_match();
                next(95);
            }
            break;
        case 95:
            if (screen_ == Screen::Room && since > 1.5) {
                st.staff_open = true, st.staff_tab = 2, st.staff_asked = false;
                session_.staff_recordings.reset();
                next(96);
            }
            break;
        case 96:
            if (since > 1.0 && session_.staff_recordings && !session_.staff_recordings->recordings.empty()) {
                const u32 id = session_.staff_recordings->recordings.front().match;
                st.staff_recording_sel = int(id);
                session_.request_replay(id);
                LOG_INFO("autotest: watching match %u (%s)", id, session_.staff_recordings->recordings.front().names.c_str());
                next(97);
            }
            break;
        case 97: {
            static double ready_at = -1;
            if (screen_ == Screen::Replay && world_ && world_->ready()) {
                if (ready_at < 0) ready_at = now_;
                if (now_ - ready_at > 2.5) {
                    shot("41_replay_chase");
                    LOG_INFO("autotest: replay at %.1f of %.1f s, following %u", watching_ ? watching_->ms / 1000.0 : 0.0, replay_length_ms() / 1000.0,
                             world_->followed());
                    world_->set_watch_view(GameWorld::WatchView::Eyes);
                    ready_at = -1;
                    next(98);
                }
            }
            break;
        }
        case 98:
            if (world_ && since > 1.5) {
                shot("42_replay_eyes");
                replay_seek(replay_length_ms() * 0.5);
                world_->set_watch_view(GameWorld::WatchView::Free);
                next(99);
            }
            break;
        case 99:
            if (world_ && since > 1.5) {
                shot("43_replay_free_seek");
                LOG_INFO("autotest: after the seek, the replay is at %.1f s", watching_ ? watching_->ms / 1000.0 : -1.0);
                leave_replay();
                next(100);
            }
            break;
        case 100:
            if (screen_ != Screen::Replay && since > 1.0) {
                shot("44_after_replay");
                LOG_INFO("autotest: back on %s", screen_name(screen_));
                next(14);
            }
            break;
        // ── --pad-test: the match played on a controller (fed, not plugged in) ──
        case 210: {
            // The left stick forward for a second: how far you went.
            static eng::Vec3 from;
            if (first) from = world_->position(), world_->face_open_space();
            test_pad_ = {};
            test_pad_.ly = 1.0f;
            pad_feed();
            if (since > 1.0) {
                const float went = eng::length(world_->position() - from);
                LOG_INFO("autotest: pad: the left stick forward for a second moved you %.0f cm%s", double(went),
                         went > 150.0f && went < 600.0f ? "" : "  <-- FAILED");
                test_pad_ = {};
                pad_feed();
                next(211);
            }
            break;
        }
        case 211: {
            // The right stick hard over for half a second: how far you turned (220 degrees a second at look 1).
            static float yaw0 = 0;
            if (first) yaw0 = world_->view_yaw();
            test_pad_ = {};
            test_pad_.rx = 1.0f;
            pad_feed();
            if (since > 0.5) {
                const float turned = eng::wrap_degrees(world_->view_yaw() - yaw0);
                LOG_INFO("autotest: pad: the right stick hard over for %.2f s turned you %.0f degrees (%.0f a second)%s", since, double(turned),
                         double(turned) / since, turned > 60.0f && turned < 180.0f ? "" : "  <-- FAILED");
                test_pad_ = {};
                pad_feed();
                next(212);
            }
            break;
        }
        case 212: {
            // The right trigger: shots.
            static int shots0 = 0;
            if (first) shots0 = world_->shots_fired(), world_->test_full_clip();
            test_pad_ = {};
            test_pad_.rt = 1.0f;
            pad_feed();
            if (since > 0.6) {
                const int shots = world_->shots_fired() - shots0;
                LOG_INFO("autotest: pad: the right trigger held 0.6 s fired %d shots%s", shots, shots > 1 ? "" : "  <-- FAILED");
                test_pad_ = {};
                pad_feed();
                next(213);
            }
            break;
        }
        case 213:
            // The d-pad up: the radio's General list. Down: its second line. A says it (and is not a jump).
            if (since > 0.3) {
                test_pad_.buttons = eng::kPadUp;
                pad_feed();
                next(214);
            }
            break;
        case 214:
            if (since > 0.12) {
                test_pad_.buttons = 0;
                pad_feed();
                next(215);
            }
            break;
        case 215:
            if (since > 0.12) {
                LOG_INFO("autotest: pad: d-pad up opened radio list %d%s", world_->radio_open(), world_->radio_open() == 1 ? "" : "  <-- FAILED");
                test_pad_.buttons = eng::kPadDown;
                pad_feed();
                next(216);
            }
            break;
        case 216:
            if (since > 0.12) {
                test_pad_.buttons = 0;
                pad_feed();
                next(217);
            }
            break;
        case 217:
            if (since > 0.3) {
                shot("pad_radio");
                LOG_INFO("autotest: pad: d-pad down picked line %d of list %d%s", world_->radio_picked(), world_->radio_open(),
                         world_->radio_picked() == 1 && world_->radio_open() == 1 ? "" : "  <-- FAILED");
                test_pad_.buttons = eng::kPadA;
                pad_feed();
                next(218);
            }
            break;
        case 218:
            if (since > 0.25) {
                const bool jumped = !world_->on_ground();
                LOG_INFO("autotest: pad: A said the line (list now %d) and you %s%s", world_->radio_open(), jumped ? "jumped" : "stayed down",
                         world_->radio_open() == -1 && !jumped ? "" : "  <-- FAILED");
                test_pad_.buttons = 0;
                pad_feed();
                next(219);
            }
            break;
        case 219: {
            // Y: the next weapon carried.
            static int slot0 = 0;
            if (first) slot0 = world_->slot();
            if (since > 0.3 && since < 0.45) test_pad_.buttons = eng::kPadY, pad_feed();
            if (since > 0.45 && test_pad_.buttons) test_pad_.buttons = 0, pad_feed();
            if (since > 0.8) {
                LOG_INFO("autotest: pad: Y took you from slot %d to slot %d%s", slot0 + 1, world_->slot() + 1, world_->slot() != slot0 ? "" : "  <-- FAILED");
                next(220);
            }
            break;
        }
        case 220:
            // Start: the match's menu.
            if (since > 0.2 && since < 0.35) test_pad_.buttons = eng::kPadStart, pad_feed();
            if (since > 0.35 && test_pad_.buttons) test_pad_.buttons = 0, pad_feed();
            if (since > 0.8) {
                shot("pad_menu");
                LOG_INFO("autotest: pad: Start: the menu is %s", world_->menu_open() ? "open" : "NOT open  <-- FAILED");
                next(221);
            }
            break;
        case 221:
            // B: back out of it (the pointer has the pad while the menu is up).
            if (since > 0.2 && since < 0.35) test_pad_.buttons = eng::kPadB, pad_feed();
            if (since > 0.35 && test_pad_.buttons) test_pad_.buttons = 0, pad_feed();
            if (since > 0.8) {
                LOG_INFO("autotest: pad: B: the menu is %s", world_->menu_open() ? "still open  <-- FAILED" : "closed");
                world_->test_menu(false);
                pad_.feed(nullptr);
                world_->set_slot_for_test(0);
                // On with the walk, as without a pad.
                mixer_.start_capture(std::filesystem::path(opts_.autotest) / "match_audio.wav");
                float peak, rms;
                mixer_.take_levels(peak, rms);
                world_->autopilot(true, false);
                next(16);
            }
            break;
        // ── The emblem maker: the clan's master makes one by hand, and the clan wears it ──
        case 300:
            if (since > 0.4 && session_.clan && session_.clan->member) {
                open_emblem_maker(*this, session_.clan->mark, true);
                // A known emblem, so the pictures are the same each run: a ground, its rim, a
                // chevron and a snowflake (in hand).
                auto layer = [](EmblemShape shape, u8 x, u8 y, u8 w, u8 h, u16 turn, u8 r, u8 g, u8 b, u8 style) {
                    EmblemLayer l;
                    l.shape = u8(shape), l.x = x, l.y = y, l.w = w, l.h = h, l.turn = turn, l.r = r, l.g = g, l.b = b, l.style = style;
                    return l;
                };
                st.emblem.layers = {layer(EmblemShape::Shield, 100, 100, 186, 190, 0, 26, 44, 96, 0),
                                    layer(EmblemShape::Shield, 100, 100, 168, 172, 0, 224, 196, 110, 1 | (2 << 1)),
                                    layer(EmblemShape::Chevron, 100, 150, 90, 40, 0, 224, 196, 110, 0),
                                    layer(EmblemShape::Snowflake, 100, 84, 80, 80, 0, 255, 255, 255, 2 << 1)};
                st.emblem_sel = 3;
                next(301);
            } else if (since > 4.0) {
                LOG_ERROR("autotest: emblem: no clan to make one for  <-- FAILED");
                next(37);
            }
            break;
        case 301:
        case 302:
        case 303:
        case 304: {
            // 301 drags the snowflake 30 units right; 303 turns it by its handle to face right
            // (90 degrees). The pointer is the test's own: press, travel, release.
            if (!st.emblem_open) break;
            VanGuiIO& io = VanGui::GetIO();
            const float* c = st.emblem_canvas;
            auto at = [&](float ex, float ey) { return VanVec2{c[0] + ex / 200.0f * (c[2] - c[0]), c[1] + ey / 200.0f * (c[3] - c[1])}; };
            const bool drag = autotest_stage_ <= 302;
            // The turning handle stands 16 units above the shape's box (Clan.cpp), the box 80 high.
            const VanVec2 from = drag ? at(100, 84) : at(130, 84 - 40 - 16), to = drag ? at(130, 84) : at(190, 84);
            if (autotest_stage_ == 301 || autotest_stage_ == 303) {
                if (autotest_stage_ == 301 && since < 0.8) break;   // the maker settles in
                ui_.set_test_pointer(from.x, from.y);
                if (since > (autotest_stage_ == 301 ? 1.0 : 0.2)) {
                    if (autotest_stage_ == 301) shot("06l_emblem_maker");
                    io.AddMouseButtonEvent(0, true);
                    next(autotest_stage_ + 1);
                }
                break;
            }
            const float t = std::clamp(float(since) / 0.5f, 0.0f, 1.0f);
            ui_.set_test_pointer(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
            if (since > 0.7) {
                io.AddMouseButtonEvent(0, false);
                const EmblemLayer& l = st.emblem.layers.back();
                if (drag)
                    LOG_INFO("autotest: emblem: dragged the snowflake to %d, %d (wanted 130, 84)%s", l.x, l.y,
                             std::abs(int(l.x) - 130) <= 2 && std::abs(int(l.y) - 84) <= 2 ? "" : "  <-- FAILED");
                else
                    LOG_INFO("autotest: emblem: turned the snowflake to %d degrees (wanted 90)%s", l.turn, std::abs(int(l.turn) - 90) <= 3 ? "" : "  <-- FAILED");
                next(autotest_stage_ == 302 ? 303 : 305);
            }
            break;
        }
        case 305:
        case 306: {
            // Ctrl+Z takes the turn back; Ctrl+Y puts it back again.
            VanGuiIO& io = VanGui::GetIO();
            const bool undoing = autotest_stage_ == 305;
            if (since > 0.2 && since < 0.3 && !io.KeyCtrl) io.AddKeyEvent(VanGuiMod_Ctrl, true), io.AddKeyEvent(undoing ? VanGuiKey_Z : VanGuiKey_Y, true);
            if (since > 0.4 && io.KeyCtrl) io.AddKeyEvent(undoing ? VanGuiKey_Z : VanGuiKey_Y, false), io.AddKeyEvent(VanGuiMod_Ctrl, false);
            if (since > 0.7) {
                const EmblemLayer& l = st.emblem.layers.back();
                const int want = undoing ? 0 : 90;
                LOG_INFO("autotest: emblem: %s: the snowflake is at %d, %d turned %d (wanted %d)%s", undoing ? "Ctrl+Z" : "Ctrl+Y", l.x, l.y, l.turn, want,
                         std::abs(int(l.turn) - want) <= 3 && l.x == 130 ? "" : "  <-- FAILED");
                next(autotest_stage_ + 1);
            }
            break;
        }
        case 307: {
            // A picture from the second page of the palette: a skull, white, beside the snowflake.
            if (since < 0.3) break;
            st.emblem_palette = 1;
            EmblemLayer skull;
            skull.shape = u8(EmblemShape::Skull), skull.x = 66, skull.y = 92, skull.w = skull.h = 58, skull.r = 236, skull.g = 236, skull.b = 230;
            st.emblem.layers.push_back(skull);
            st.emblem_sel = int(st.emblem.layers.size()) - 1;
            next(308);
            break;
        }
        case 308: {
            // Confirm, pressed where the button is drawn (the dialog's own geometry: Clan.cpp).
            const VanVec2 p = ui::pg(902 - 170 + 36, 676 - 50 + 20);
            ui_.set_test_pointer(p.x, p.y);
            if (since > 0.5 && since < 0.6) {
                shot("06m_emblem_made");
                VanGui::GetIO().AddMouseButtonEvent(0, true);
            }
            if (since > 0.7 && st.emblem_open) VanGui::GetIO().AddMouseButtonEvent(0, false);
            if (since > 0.9 && !st.emblem_open) next(309);
            if (since > 3.0) {
                LOG_ERROR("autotest: emblem: Confirm did not close the maker  <-- FAILED");
                st.emblem_open = false;
                next(309);
            }
            break;
        }
        case 309:
            // The server's word: the clan, its card and the lobby's list wear it.
            if (session_.clan && session_.clan->mark.layers.size() == 5 && session_.profile.clan_mark.layers.size() == 5 && since > 0.8) {
                LOG_INFO("autotest: emblem: the clan wears its own emblem (%zu shapes)", session_.clan->mark.layers.size());
                ui_.set_test_pointer(-FLT_MAX, -FLT_MAX);
                shot("06n_clan_emblem");
                next(320);
            } else if (since > 4.0) {
                LOG_ERROR("autotest: emblem: the server never sent the new emblem back  <-- FAILED");
                next(37);
            }
            break;
        // ── Rewards and events: the dialog, a box opened, the Game Master's editor ──
        case 320:
            if (since > 0.3) {
                st.clan_open = false;
                open_rewards(*this, 0);
                next(321);
            }
            break;
        case 321:
            if (session_.rewards && since > 0.8) {
                shot("06p_rewards_today");
                LOG_INFO("autotest: rewards: %zu quests, week day %u, %s today, %u minutes, %zu events listed", session_.rewards->quests.size(),
                         unsigned(session_.rewards->week_at), session_.rewards->signed_today ? "signed in" : "not signed in", unsigned(session_.rewards->minutes),
                         session_.rewards->events.size());
                if (!session_.rewards->signed_today) session_.claim_reward(proto::ClaimKind::SignIn);
                next(322);
            } else if (since > 5.0) {
                LOG_ERROR("autotest: rewards: the server sent no rewards  <-- FAILED");
                next(322);
            }
            break;
        case 322:
            if (since > 1.0) {
                LOG_INFO("autotest: rewards: signed in today: %s", session_.rewards && session_.rewards->signed_today ? "yes" : "NO  <-- FAILED");
                shot("06q_rewards_collected");
                st.rewards_tab = 1;
                next(323);
            }
            break;
        case 323:
            if (since > 0.7) {
                shot("06r_rewards_quests");
                st.rewards_tab = 2;
                next(324);
            }
            break;
        case 324:
            if (since > 0.7) {
                shot("06s_rewards_events");
                st.rewards_open = false;
                // A Game Master's gift to themselves: two Duffle Bags (C) and a Gift Box, then the Gift tab.
                session_.global_staff_edit(std::to_string(session_.tv.account), proto::AccountField::GrantBox, 2, "duffle_c");
                session_.global_staff_edit(std::to_string(session_.tv.account), proto::AccountField::GrantBox, 1, "gift");
                next(325);
            }
            break;
        case 325: {
            // The grants land at TVAS; the menus ask for news every 6 s (TV-8), so the rewards are
            // asked for once here rather than waited on.
            static bool asked = false;
            if (first) asked = false;
            if (since > 1.5 && !asked) session_.request_rewards(), asked = true;
            if (since > 1.0 && session_.rewards && session_.rewards->boxes[size_t(BoxKind::DuffleC)] >= 2) {
                st.shop_return = screen_;
                st.shop_tab = 2, st.inv_tab = 3, st.gift_sel = int(BoxKind::DuffleC);
                go(Screen::Shop);
                next(326);
            } else if (since > 10.0) {
                LOG_ERROR("autotest: rewards: the granted boxes never arrived  <-- FAILED");
                next(326);
            }
            break;
        }
        case 326:
            if (since > 1.0) {
                shot("06t_gift_tab");
                st.box_opened_at = -1;
                session_.open_box(u8(BoxKind::DuffleC));
                next(327);
            }
            break;
        case 327:
            if (session_.box_opened && st.box_opened_at > 0 && now_ - st.box_opened_at > 0.45 && now_ - st.box_opened_at < 0.8) {
                shot("06u_box_opening");
                next(328);
            } else if (since > 5.0) {
                LOG_ERROR("autotest: rewards: the box never opened  <-- FAILED");
                next(328);
            }
            break;
        case 328:
            if (session_.box_opened && now_ - st.box_opened_at > 1.8) {
                LOG_INFO("autotest: rewards: a %s held %s", box_info(session_.box_opened->box).name, session_.box_opened->text.c_str());
                shot("06v_box_opened");
                session_.box_opened.reset();
                st.box_opened_at = -1;
                go(st.shop_return);
                st.staff_open = true, st.staff_tab = 3, st.staff_cfg_asked = false;
                next(329);
            } else if (since > 5.0) {
                session_.box_opened.reset();
                next(329);
            }
            break;
        case 329:
            if (since > 1.2) {
                shot("06w_staff_events");
                // Christmas, picked, to show an event's own page.
                if (st.staff_cfg)
                    for (size_t i = 0; i < st.staff_cfg->events.size(); ++i)
                        if (st.staff_cfg->events[i].theme == u8(EventTheme::Christmas)) st.staff_event_sel = int(i);
                next(330);
            }
            break;
        case 330:
            if (since > 0.6) {
                shot("06x_staff_event_christmas");
                st.staff_tab = 4, st.staff_rewards_sub = 0, st.staff_box_sel = int(BoxKind::DuffleD);
                next(331);
            }
            break;
        case 331:
            if (since > 0.6) {
                shot("06y_staff_rewards_boxes");
                st.staff_rewards_sub = 3;
                next(332);
            }
            break;
        case 332:
            if (since > 0.6) {
                shot("06z_staff_rewards_quests");
                st.staff_open = false;
                st.clan_open = true;   // stage 37 closes the clan it left open
                next(37);
            }
            break;
        // ── --modes: a room of each game type with bots: the room, the match at its objective, the scores ──
        case 500: case 510: case 520: case 530: case 540: case 550: case 560: case 570: case 580: case 590: case 600: {
            struct ModeCase {
                Mode mode;
                const char* map;
                const char* tag;
                Objective look;          // what to stand facing (Objective::Count: nothing)
                double wait;             // seconds in the match before the picture
            };
            static const ModeCase kCases[] = {
                {Mode::TeamBattle, "missile", "tb_blast", Objective::Site, 4.0},
                {Mode::TeamBattle, "nervegas", "tb_takeback", Objective::Item, 4.0},
                {Mode::CaptureTheCaptain, "crossroad", "ctc", Objective::Count, 4.0},
                {Mode::Captain, "train", "captain", Objective::Count, 4.0},
                {Mode::Horror, "nervegashorror", "horror", Objective::Count, 24.0},
                {Mode::Horror2, "villagehorror", "horror2", Objective::Girl, 3.0},
                {Mode::TeamSlayer, "satellite", "slayer", Objective::Count, 4.0},
                {Mode::Occupy, "silo", "occupy", Objective::Console, 4.0},
                {Mode::Pirate, "pirateship", "pirate", Objective::Stronghold, 6.0},
                {Mode::Sniper, "basecamp", "sniper", Objective::Count, 4.0},
            };
            const int k = (autotest_stage_ - 500) / 10;
            if (k >= int(std::size(kCases))) {
                quit_ = true;
                break;
            }
            static int sub_step = 0;
            static double step_at = 0;
            if (first) sub_step = 0, step_at = now_;
            const double in_step = now_ - step_at;
            auto step = [&](int s) { sub_step = s, step_at = now_; };
            const ModeCase& c = kCases[size_t(k)];
            const std::string tag = eng::str::format("M%d_%s", k, c.tag);
            switch (sub_step) {
                case 0:
                    if (screen_ == Screen::Lobby && in_step > 0.8) {
                        RoomSettings rs;
                        rs.title = std::string("Autotest ") + mode_name(c.mode);
                        rs.mode = c.mode;
                        rs.map = c.map;
                        rs.bots = 7;
                        rs.bot_skill = u8(BotSkill::Easy);
                        rs.goal = mode_info(c.mode).goal_default;
                        rs.minutes = mode_info(c.mode).minutes_default;
                        rs.time_of_day = baked_time_of_day(c.map);
                        session_.create_room(rs);
                        step(1);
                    }
                    break;
                case 1:
                    if (screen_ == Screen::Room && in_step > 1.2) {
                        shot(tag + "_room");
                        session_.start_match();
                        step(2);
                    }
                    break;
                case 2:
                    if (screen_ == Screen::Match && world_ && world_->ready() && in_step > c.wait) {
                        if (c.look != Objective::Count && !world_->view_objective(c.look))
                            LOG_ERROR("autotest: modes: %s: nothing of that kind to stand at  <-- FAILED", c.tag);
                        step(3);
                    } else if (in_step > 90.0) {
                        LOG_ERROR("autotest: modes: %s: the match never began  <-- FAILED", c.tag);
                        step(5);
                    }
                    break;
                case 3:
                    if (in_step > 0.8) {
                        shot(tag + "_match");
                        const proto::ModeState& ms = world_->mode_state();
                        const proto::RoleNow* mine = world_->role_of(world_->me());
                        const char* you = !mine ? "a soldier"
                                          : mine->undead ? undead_def(Undead(mine->undead)).name
                                          : MatchRole(mine->role) == MatchRole::Captain ? "a captain"
                                                                                         : "a soldier";
                        LOG_INFO("autotest: modes: %s on %s: %zu objectives, %zu roles, phase %u; you are %s%s", mode_name(c.mode), c.map, ms.objectives.size(),
                                 ms.roles.size(), unsigned(ms.phase), you,
                                 (c.look != Objective::Count || c.mode == Mode::CaptureTheCaptain || c.mode == Mode::Captain || c.mode == Mode::Horror) &&
                                         ms.objectives.empty() && ms.roles.empty()
                                     ? "  <-- FAILED (the game type said nothing)"
                                     : "");
                        world_->test_scores(true);
                        step(4);
                    }
                    break;
                case 4:
                    if (in_step > 0.5) {
                        shot(tag + "_scores");
                        world_->test_scores(false);
                        // Horror Mode 2's window and then the skill bar, as a Hunter's: yours when the
                        // game made you one of the first undead, drawn as one while you still stand
                        // when it did not. Then the undead, as drawn: the nearest one in sight, from in
                        // front (they come looking).
                        if (c.mode == Mode::Horror2) {
                            const proto::RoleNow* mine = world_->role_of(world_->me());
                            if (!(mine && mine->undead)) world_->test_undead_hud(Undead::Hunter);
                            step(8);
                        } else if (c.mode == Mode::Horror) {
                            step(9);
                        } else {
                            leave_match();
                            step(5);
                        }
                    }
                    break;
                case 8:
                    if (in_step > 0.5) {
                        shot(tag + "_select");
                        const proto::RoleNow* mine = world_->role_of(world_->me());
                        if (mine && mine->undead) world_->test_pick_class(Undead::Hunter);
                        else world_->test_undead_hud(Undead::Hunter, false);
                        step(12);
                    }
                    break;
                case 12:
                    if (in_step > 0.5 && world_->alive()) {
                        shot(tag + "_skills");
                        world_->test_undead_hud(Undead::None);
                        step(9);
                    } else if (in_step > 20.0) {
                        LOG_ERROR("autotest: modes: %s: never came in as a Hunter  <-- FAILED", c.tag);
                        world_->test_undead_hud(Undead::None);
                        step(9);
                    }
                    break;
                case 9:
                    if (world_ && world_->face_undead()) {
                        step(7);
                    } else if (in_step > 30.0) {
                        LOG_ERROR("autotest: modes: %s: no undead came into sight  <-- FAILED", c.tag);
                        step(7);
                    }
                    break;
                case 7:
                    // In front of him again just before the picture: in the wait he comes for you.
                    if (in_step > 0.7) {
                        world_->face_undead();
                        step(10);
                    }
                    break;
                case 10:
                    if (in_step > 0.15) {
                        shot(tag + "_undead");
                        if (c.mode == Mode::Horror2) {
                            // An undead's death card: back in a few seconds, not at the next round.
                            world_->test_undead_hud(Undead::Hunter, false);
                            world_->test_death();
                            step(11);
                        } else {
                            leave_match();
                            step(5);
                        }
                    }
                    break;
                case 11:
                    if (in_step > 0.8) {
                        shot(tag + "_death");
                        world_->test_undead_hud(Undead::None);
                        leave_match();
                        step(5);
                    }
                    break;
                case 5:
                    if (screen_ == Screen::Room && in_step > 1.0) {
                        session_.leave_room();
                        step(6);
                    } else if (in_step > 15.0) {
                        go(Screen::Lobby);
                        step(6);
                    }
                    break;
                case 6:
                    if (screen_ == Screen::Lobby && in_step > 1.0) next(500 + 10 * (k + 1));
                    break;
            }
            break;
        }
        // ── --movement: Missile's ladder climbed up and back down, Nuclear's flight of twelve stairs run up
        // (sfcheck ladders / stairs found them), a run on Snowcamp's snow, and the footsteps heard
        // (yours crouched, walking and running; a bot's crouched and running): pictures in first and
        // third person, and the numbers ──
        case 700: case 710: case 720: case 730: case 740: {
            struct MoveCase {
                const char* map;
                const char* tag;
                eng::Vec3 at;            // where to stand: the ladder's foot, the bottom of the flight
                float yaw;
                bool ladder;
                bool steps = false;      // a run from the spawn over what the map is made of: its footsteps seen
                bool bot = false;        // a bot in front, crouch-walking then running: its footsteps heard
            };
            static const MoveCase kCases[] = {
                {"missile", "ladder", {-2519.0f, 5.0f, 163.0f}, 0.0f, true},
                {"nuclear", "stairs", {-2697.0f, 5.0f, 4498.0f}, 0.0f, false},
                {"snowcamp", "snow", {}, 0.0f, false, true},
                {"snowcamp", "bot", {}, 0.0f, false, false, true},
            };
            const int k = (autotest_stage_ - 700) / 10;
            if (k >= int(std::size(kCases))) {
                quit_ = true;
                break;
            }
            static int sub_step = 0;
            static double step_at = 0, began = 0;
            static float start_y = 0, last_eye = 0, last_feet = 0, eye_jump = 0, feet_jump = 0;
            static bool down_shot = false;
            static int steps_mark = 0;   // the footsteps heard when the stage counting them began
            if (first) sub_step = 0, step_at = now_;
            const double in_step = now_ - step_at;
            auto step = [&](int s) { sub_step = s, step_at = now_; };
            const MoveCase& c = kCases[size_t(k)];
            const std::string tag = eng::str::format("V%d_%s", k, c.tag);
            // The eye and the feet, frame to frame: the most each moved, as centimetres in a 144th of
            // a second (a long frame, a picture being written, moves them far without jumping).
            static double last_t = 0;
            auto track = [&] {
                const float e = world_->eye().y, f = world_->feet().y;
                const float per = float(std::min(1.0, (1.0 / 144.0) / std::max(1e-4, now_ - last_t)));
                eye_jump = std::max(eye_jump, std::fabs(e - last_eye) * per), feet_jump = std::max(feet_jump, std::fabs(f - last_feet) * per);
                last_eye = e, last_feet = f, last_t = now_;
            };
            auto stand = [&](float pitch) {
                world_->test_stand(c.at, c.yaw, pitch);
                start_y = world_->feet().y, last_feet = start_y, last_eye = world_->eye().y + 0.0f;
                eye_jump = feet_jump = 0;
                began = now_;
            };
            switch (sub_step) {
                case 0:
                    if (screen_ == Screen::Lobby && in_step > 0.8) {
                        RoomSettings rs;
                        rs.title = std::string("Autotest ") + c.tag;
                        rs.mode = Mode::TeamDeathmatch;
                        rs.map = c.map;
                        rs.goal = 100;
                        rs.minutes = 10;
                        rs.time_of_day = baked_time_of_day(c.map);
                        rs.bots = c.bot ? 1 : 0;
                        session_.create_room(rs);
                        step(1);
                    }
                    break;
                case 1:
                    if (screen_ == Screen::Room && in_step > 1.2) {
                        session_.start_match();
                        step(2);
                    }
                    break;
                case 2:
                    if (screen_ == Screen::Match && world_ && world_->ready() && in_step > 2.0 && c.steps) {
                        // From the spawn the longest clear way, seen from the side: the boots and what
                        // they kick up.
                        world_->face_open_space();
                        world_->autopilot(true, false);
                        world_->toggle_third_person();
                        world_->test_orbit(115.0f, -32.0f);
                        step(12);
                    } else if (screen_ == Screen::Match && world_ && world_->ready() && in_step > 2.0 && c.bot) {
                        // The bot stood in front of you (the server's test hook), then crouch-walking
                        // side to side.
#if LSF_WITH_SERVER
                        if (local_server_) local_server_->test_bots_before(session_.session_id, 450.0f), local_server_->test_bots_pace(session_.session_id, 1);
                        else
#endif
                            LOG_ERROR("autotest: movement: not this PC's server: no bot to listen to  <-- FAILED");
                        step(20);
                    } else if (screen_ == Screen::Match && world_ && world_->ready() && in_step > 2.0) {
                        stand(c.ladder ? 40.0f : 0.0f);   // a ladder looked up, a flight looked along
                        last_eye = world_->eye().y;
                        world_->autopilot(true, false);
                        down_shot = false;
                        step(3);
                    } else if (in_step > 90.0) {
                        LOG_ERROR("autotest: movement: %s: the match never began  <-- FAILED", c.tag);
                        step(9);
                    }
                    break;
                case 3:
                    if (in_step < 0.1) {
                        last_eye = world_->eye().y, last_feet = world_->feet().y, last_t = now_;   // the stand's own frame, not a move
                        break;
                    }
                    track();
                    if (c.ladder) {
                        // Halfway up: the climb from the eye, then from the side.
                        if (world_->on_ladder() && world_->feet().y > start_y + 300.0f) {
                            shot(tag + "_climb");
                            world_->toggle_third_person();
                            world_->test_orbit(70.0f);
                            step(4);
                        } else if (in_step > 8.0) {
                            LOG_ERROR("autotest: movement: the ladder was not climbed (on it %d, %.0f cm up)  <-- FAILED", int(world_->on_ladder()),
                                      double(world_->feet().y - start_y));
                            step(9);
                        }
                    } else if (in_step > 0.42 && in_step < 0.5) {
                        shot(tag + "_run");
                        step(7);
                    }
                    break;
                case 4:
                    track();
                    if (in_step > 0.35) {
                        shot(tag + "_third");
                        LOG_INFO("autotest: movement: on the ladder your body plays %s", world_->body_clips().c_str());
                        world_->test_orbit(0);
                        world_->toggle_third_person();
                        step(5);
                    }
                    break;
                case 5:
                    track();
                    // Off the top, standing on what it leads to.
                    if (world_->on_ground() && !world_->on_ladder() && world_->feet().y > start_y + 600.0f) {
                        LOG_INFO("autotest: movement: the ladder climbed %.0f cm in %.1f s", double(world_->feet().y - start_y), now_ - began);
                        world_->autopilot(false, false);
                        shot(tag + "_top");
                        // Back down: toward the drop it came up from, looking down, at a walk.
                        world_->look(c.yaw + 180.0f, -60.0f);
                        world_->autopilot_walk(true);
                        world_->autopilot(true, false);
                        began = now_;
                        step(6);
                    } else if (in_step > 8.0) {
                        LOG_ERROR("autotest: movement: never off the top of the ladder (%.0f cm up)  <-- FAILED", double(world_->feet().y - start_y));
                        step(9);
                    }
                    break;
                case 6:
                    track();
                    if (world_->on_ladder() && !down_shot && in_step > 1.0) {
                        shot(tag + "_down");
                        down_shot = true;
                    }
                    if (down_shot && world_->on_ground() && !world_->on_ladder() && world_->feet().y < start_y + 60.0f) {
                        LOG_INFO("autotest: movement: the ladder climbed down in %.1f s, the feet %.0f cm from where they began", now_ - began,
                                 double(world_->feet().y - start_y));
                        world_->autopilot(false, false), world_->autopilot_walk(false);
                        step(9);
                    } else if (in_step > 10.0) {
                        LOG_ERROR("autotest: movement: the ladder was not climbed down (on it %d, %.0f cm up)  <-- FAILED", int(world_->on_ladder()),
                                  double(world_->feet().y - start_y));
                        world_->autopilot(false, false), world_->autopilot_walk(false);
                        step(9);
                    }
                    break;
                case 7:
                    // The flight to its top, then the numbers.
                    if (in_step < 0.05) {
                        last_eye = world_->eye().y, last_feet = world_->feet().y, last_t = now_;
                        break;
                    }
                    track();
                    if (in_step > 1.6) {
                        const float rose = world_->feet().y - start_y;
                        LOG_INFO("autotest: movement: the stairs: %.0f cm up in %.1f s; the eye moved at most %.1f cm between frames, the feet %.1f%s",
                                 double(rose), now_ - began, double(eye_jump), double(feet_jump),
                                 rose > 250.0f && eye_jump < 10.0f ? "" : "  <-- FAILED");
                        // Again, seen from the side.
                        stand(0.0f);
                        world_->toggle_third_person();
                        world_->test_orbit(70.0f);
                        step(8);
                    }
                    break;
                case 8:
                    if (in_step > 0.6) {
                        shot(tag + "_third");
                        LOG_INFO("autotest: movement: on the stairs your body plays %s", world_->body_clips().c_str());
                        world_->test_orbit(0);
                        world_->toggle_third_person();
                        world_->autopilot(false, false);
                        step(9);
                    }
                    break;
                case 12:
                    if (in_step > 1.1) {
                        shot(tag + "_steps");
                        LOG_INFO("autotest: movement: running on %s: %s", c.map, world_->effects_summary().c_str());
                        world_->test_orbit(0);
                        world_->toggle_third_person();
                        // Then the footsteps heard: crouched, walking, running, a second and a half
                        // each, turning slowly so the run stays on open ground.
                        world_->autopilot_turn(40.0f);
                        world_->autopilot_jump(false, true);
                        steps_mark = world_->steps_heard();
                        step(13);
                    }
                    break;
                case 13: case 14: case 15: {
                    static int heard[3] = {};
                    const int how = sub_step - 13;
                    if (in_step > 1.5) {
                        heard[how] = world_->steps_heard() - steps_mark;
                        steps_mark = world_->steps_heard();
                        world_->autopilot_jump(false, false), world_->autopilot_walk(how == 0);
                        if (how < 2) {
                            step(sub_step + 1);
                            break;
                        }
                        world_->autopilot_turn(0), world_->autopilot_walk(false), world_->autopilot(false, false);
                        LOG_INFO("autotest: movement: footsteps heard in a second and a half: crouched %d, walking %d, running %d%s", heard[0], heard[1], heard[2],
                                 heard[0] == 0 && heard[1] == 0 && heard[2] > 0 ? "" : "  <-- FAILED (crouching and walking are silent; running is not)");
                        step(9);
                    }
                    break;
                }
                case 20:
                    // Placed and pacing: the count begins.
                    if (in_step > 1.2) {
                        steps_mark = world_->others_steps_heard();
                        step(21);
                    }
                    break;
                case 21: case 22: {
                    static int heard[2] = {};
                    const int how = sub_step - 21;
                    if (in_step > 2.0) {
                        heard[how] = world_->others_steps_heard() - steps_mark;
                        steps_mark = world_->others_steps_heard();
#if LSF_WITH_SERVER
                        if (local_server_) local_server_->test_bots_pace(session_.session_id, how == 0 ? 2 : 0);
#endif
                        if (how == 0) {
                            step(22);
                            break;
                        }
                        LOG_INFO("autotest: movement: a bot's footsteps heard in two seconds: crouch-walking %d, running %d%s", heard[0], heard[1],
                                 heard[0] == 0 && heard[1] > 0 ? "" : "  <-- FAILED (a crouched soldier is silent; a running one is not)");
                        // Then the gun: a whole magazine held down, standing still.
                        world_->look(world_->view_yaw() + 90.0f, 0.0f);
                        world_->test_full_clip();
                        world_->clear_shot_offsets();
                        world_->autopilot(false, true);
                        step(23);
                    }
                    break;
                }
                case 23:
                    // The spray, then single shots a third of a second apart.
                    if (in_step > 3.0) {
                        world_->autopilot(false, false);
                        const auto o = world_->shot_offsets();
                        auto mean = [&](size_t from, size_t to, bool cross) {
                            double sum = 0;
                            size_t n = 0;
                            for (size_t i = from; i < to && i < o.size(); ++i) sum += cross ? o[i].second : o[i].first, ++n;
                            return n ? sum / double(n) : 0.0;
                        };
                        float most = 0, most_cross = 0;
                        for (const auto& [aim, cross] : o) most = std::max(most, aim), most_cross = std::max(most_cross, cross);
                        const WeaponDef* w = weapon(world_->loadout()[0]);
                        LOG_INFO("autotest: movement: %s, a magazine held down: %zu shots, off the aim on average %.2f deg (shots 1-5), %.2f (6-15), %.2f (16-30), at most %.2f; "
                                 "off the crosshair %.2f, %.2f, %.2f, at most %.2f",
                                 w ? w->name.c_str() : "?", o.size(), mean(0, 5, false), mean(5, 15, false), mean(15, 30, false), double(most), mean(0, 5, true),
                                 mean(5, 15, true), mean(15, 30, true), double(most_cross));
                        world_->test_full_clip();
                        world_->clear_shot_offsets();
                        step(24);
                    }
                    break;
                case 24: {
                    // One shot at a time: the trigger held until a shot leaves, then let go.
                    static int taps = 0;
                    static size_t before = 0;
                    if (in_step < 0.05) taps = 0, before = 0;
                    const size_t now_shots = world_->shot_offsets().size();
                    if (taps < 5 && in_step > 0.8 + 0.35 * taps) {
                        if (now_shots == before) world_->autopilot(false, true);
                        else world_->autopilot(false, false), before = now_shots, ++taps;
                    }
                    if (taps >= 5 && in_step > 3.0) {
                        world_->autopilot(false, false);
                        const auto o = world_->shot_offsets();
                        double sum = 0;
                        for (const auto& [aim, cross] : o) sum += aim;
                        LOG_INFO("autotest: movement: five single shots a third of a second apart: %zu fired, off the aim on average %.2f deg", o.size(),
                                 o.empty() ? 0.0 : sum / double(o.size()));
                        step(9);
                    }
                    break;
                }
                case 9:
                    leave_match();
                    step(10);
                    break;
                case 10:
                    if (screen_ == Screen::Room && in_step > 1.0) {
                        session_.leave_room();
                        step(11);
                    } else if (in_step > 15.0) {
                        go(Screen::Lobby);
                        step(11);
                    }
                    break;
                case 11:
                    if (screen_ == Screen::Lobby && in_step > 1.0) next(700 + 10 * (k + 1));
                    break;
            }
            break;
        }
        case 70: {
            // Facing whoever else is standing, a picture a second: his body's clips, his gun, his
            // flashes and tracers as this client draws them from the snapshots.
            static int taken = 0;
            if (!world_) break;
            const bool facing = world_->follow_other(650.0f, 75.0f);
            if (facing && since > 1.0) {
                shot(eng::str::format("remote_%02d", taken));
                LOG_INFO("autotest: remote_%02d: %s", taken, world_->other_clips().c_str());
                autotest_at_ = now_;
                if (++taken >= 12) {
                    LOG_INFO("autotest: the others' tracers: %d from their drawn muzzles, %d from their eyes", world_->tracers_from_muzzle(),
                             world_->tracers_from_eye());
                    quit_ = true;
                }
            }
            if (since > 30.0) {
                LOG_ERROR("autotest: nobody else to watch");
                quit_ = true;
            }
            break;
        }
        case 75:
        case 76:
        case 77: {
            // Each of three glows: stood a few metres off, facing it, then photographed.
            const int k = autotest_stage_ - 75;
            if (!world_) break;
            const bool found = world_->view_glow(k);
            if (!found) {
                LOG_INFO("autotest: the map has %d glow%s", k, k == 1 ? "" : "s");
                quit_ = true;
            } else if (since > 1.5) {
                shot(eng::str::format("glow_%d", k));
                if (settings_.fidelity) LOG_INFO("autotest: Fidelity: glow %d: %s", k, world_->lamp_report().c_str());
                if (k == 2) quit_ = true;
                else next(autotest_stage_ + 1);
            }
            break;
        }
        // ── --killmarks: every kill mark in turn, in a match of your own (pictures K*) ──
        case 950:
            if (since > 0.5) {
                RoomSettings rs;
                rs.title = "Autotest kill marks";
                rs.mode = Mode::TeamDeathmatch;
                rs.map = opts_.map.empty() ? "crossroad" : opts_.map;
                rs.goal = 100;
                rs.minutes = 10;
                session_.create_room(rs);
                next(951);
            }
            break;
        case 951:
            if (screen_ == Screen::Room && since > 1.0) {
                session_.start_match();
                next(952);
            }
            break;
        case 952:
            if (screen_ == Screen::Match && world_ && since > 3.0) next(953);
            break;
        case 953: {
            // One mark a step: shown, left to settle past its flash, photographed.
            static int index = 0;
            static const char* name = nullptr;
            if (first) {
                name = world_ ? world_->test_kill_mark(index) : nullptr;
                if (!name) {
                    LOG_INFO("autotest: kill marks: %d photographed", index);
                    quit_ = true;
                }
            } else if (since > 0.75 && name) {
                shot(eng::str::format("K%02d_%s", index, eng::str::lower(name).c_str()));
                ++index;
                next(953);
            }
            break;
        }
        // ── --social: friends, messages, chat and the clan, on a staged server (socialcheck --stage) ──
        case 900: {
            // The lobby with what waits: requests and messages on the Friends plate.
            const bool told = session_.friends && session_.mailbox && session_.clan;
            if (since > 1.5 && told && !session_.room_invites.empty()) {
                // A friend's room asks you in (the staged Bob does, the moment you are on): seen, and turned down.
                const proto::RoomInvited inv = session_.room_invites.front();
                shot("S00_room_invitation");
                LOG_INFO("autotest: social: invited by %s to room %u '%s' (%u / %u)", inv.from.c_str(), unsigned(inv.room), inv.title.c_str(), unsigned(inv.players),
                         unsigned(inv.max_players));
                session_.room_invite_answer(inv.room, false);
                session_.room_invites.clear();
                autotest_at_ = now_;
                break;
            }
            if (since > 1.5 && told) {
                LOG_INFO("autotest: social: %zu on the friend list (%d asking), %d unread messages; clan %s", session_.friends->entries.size(), session_.friend_requests(),
                         session_.unread_mail(), session_.clan->member ? session_.clan->name.c_str() : "(none)");
                shot("S01_lobby");
                st.user_tab = 3;
                next(901);
            } else if (since > 8.0) {
                LOG_ERROR("autotest: social: the server never sent the friends, the inbox and the clan  <-- FAILED");
                quit_ = true;
            }
            break;
        }
        case 901:
            if (since > 0.7) {
                shot("S02_lobby_friend_tab");
                open_social(*this, 0);
                st.friend_sel = "Bob";
                next(902);
            }
            break;
        case 902:
            if (since > 0.9) {
                shot("S03_friends");
                st.social_tab = 1;
                next(903);
            }
            break;
        case 903:
            if (since > 0.6) {
                shot("S04_friends_requests");
                st.social_tab = 2;
                next(904);
            }
            break;
        case 904:
            if (since > 0.6) {
                shot("S05_friends_blocked");
                st.social_tab = 3;
                if (session_.mailbox)
                    for (const proto::MailItem& m : session_.mailbox->items)
                        if (!m.system) {
                            st.mail_sel = m.id;   // the soldiers' newest (Team Vanilla's have their own tab)
                            break;
                        }
                next(905);
            }
            break;
        case 905:
            if (since > 0.6) {
                shot("S06_friends_inbox");
                // The first who asks is accepted, the newest message read: the plate's count answers.
                LOG_INFO("autotest: social: %d wait on the Friends plate", social_waiting(session_));
                if (session_.friends)
                    for (const proto::FriendEntry& e : session_.friends->entries)
                        if (e.state == u8(proto::FriendState::Incoming)) {
                            session_.friend_action(proto::FriendOp::Accept, e.name);
                            break;
                        }
                if (st.mail_sel) session_.mail_action(proto::MailOp::Read, st.mail_sel);
                // ML-2: the message read is saved; it shows Saved, the rest their days left.
                if (st.mail_sel) session_.mail_action(proto::MailOp::Save, st.mail_sel);
                next(960);
            }
            break;
        case 960:
            if (since > 1.0) {
                shot("S06b_friends_inbox_saved");
                // ML-4: Team Vanilla's own mail, on its own tab.
                st.social_tab = 4;
                if (session_.mailbox)
                    for (const proto::MailItem& m : session_.mailbox->items)
                        if (m.system) {
                            st.mail_sel = m.id;
                            break;
                        }
                int system = 0, saved = 0;
                if (session_.mailbox)
                    for (const proto::MailItem& m : session_.mailbox->items) system += m.system, saved += m.saved;
                LOG_INFO("autotest: social: %d from Team Vanilla, %d saved", system, saved);
                if (!system) LOG_ERROR("autotest: social: no system mail in the inbox  <-- FAILED");
                next(961);
            }
            break;
        case 961:
            if (since > 0.8) {
                shot("S06c_friends_system_mail");
                st.social_tab = 0;
                next(906);
            }
            break;
        case 906:
            if (since > 1.2) {
                shot("S07_friends_after");
                LOG_INFO("autotest: social: a request accepted and a message read: %d wait on the Friends plate now", social_waiting(session_));
                st.social_open = false;
                st.clan_open = true;
                st.clan_tab = 0;
                st.clan_browse_at = -100;
                session_.request_clan();
                next(907);
            }
            break;
        case 907:
            if (since > 1.2 && session_.clan) {
                if (!session_.clan->member) {
                    next(920);   // no clan: the clans there are, founding one, an application
                    break;
                }
                st.clan_member_sel = "Dave";
                LOG_INFO("autotest: social: in %s as its %s: %zu members, %zu applications, %zu log lines, %s", session_.clan->name.c_str(),
                         clan_rank_name(ClanRank(session_.clan->my_rank)), session_.clan->members.size(), session_.clan->applicants.size(), session_.clan->log.size(),
                         session_.clan->open ? "open" : "closed");
                next(908);
            }
            break;
        case 908:
            if (since > 0.6) {
                shot("S08_clan_members");
                st.clan_tab = 1;
                next(909);
            }
            break;
        case 909:
            if (since > 0.6) {
                shot("S09_clan_applications");
                st.clan_tab = 2;
                next(910);
            }
            break;
        case 910:
            if (since > 0.6) {
                shot("S10_clan_log");
                st.clan_tab = 3;
                next(911);
            }
            break;
        case 911:
            if (since > 1.5) {
                shot("S11_clan_other_clans");
                // What the rank may do, asked of the server: a Member promoted, an application
                // answered, the clan opened.
                session_.clan_action(proto::ClanOp::Promote, "Dave");
                if (session_.clan && !session_.clan->applicants.empty()) session_.clan_action(proto::ClanOp::Accept, session_.clan->applicants.front().name);
                session_.clan_action(proto::ClanOp::SetOpen, {}, session_.clan && session_.clan->open ? "closed" : "open");
                st.clan_tab = 0;
                st.clan_notice_editing = true;
                st.clan_notice_edit = "Scrims on Fridays. Recruits welcome.";
                next(912);
            }
            break;
        case 912:
            if (since > 1.5 && session_.clan) {
                shot("S12_clan_after");
                std::string dave = "?";
                for (const proto::ClanMember& m : session_.clan->members)
                    if (m.name == "Dave") dave = clan_rank_name(ClanRank(m.rank));
                LOG_INFO("autotest: social: Dave is now a %s; %zu members, %zu applications waiting; the clan is %s", dave.c_str(), session_.clan->members.size(),
                         session_.clan->applicants.size(), session_.clan->open ? "open" : "closed");
                session_.clan_action(proto::ClanOp::SetNotice, {}, st.clan_notice_edit);
                st.clan_notice_editing = false;
                st.clan_open = false;
                next(930);
            }
            break;
        case 920:
            if (since > 1.5) {
                st.clan_browse_sel = "Night Owls";
                next(921);
            }
            break;
        case 921:
            if (since > 0.5) {
                shot("S08_clans_join");
                LOG_INFO("autotest: social: no clan: %zu clans listed", session_.clan_directory ? session_.clan_directory->clans.size() : size_t(0));
                st.clan_tab = 4;
                next(922);
            }
            break;
        case 922:
            if (since > 0.6) {
                shot("S09_clan_found");
                st.clan_tab = 3;
                session_.clan_action(proto::ClanOp::Apply, {}, "Night Owls");
                next(923);
            }
            break;
        case 923:
            if (since > 1.2 && session_.clan) {
                shot("S10_clan_asked");
                LOG_INFO("autotest: social: asked to join Night Owls: %s", session_.clan->member ? "in at once (it is open)"
                                                                               : session_.clan->applied.empty() ? "nothing waits  <-- FAILED" : "an application waits");
                st.clan_open = false;
                next(930);
            }
            break;
        case 930:
            // A soldier's menu, as a right click on a name in the lobby's list opens it.
            if (since > 0.6) {
                st.user_tab = 0;
                for (const auto& [id, u] : session_.users)
                    if (id != session_.session_id) {
                        open_soldier_menu(*this, id, u.name, u.clan);
                        break;
                    }
                next(931);
            }
            break;
        case 931:
            if (since > 0.7) {
                shot("S13_soldier_menu");
                st.menu_name.clear();   // closes it
                (void)send_chat(*this, "/g Autotest reporting in", proto::ChatScope::Lobby);
                if (session_.clan && session_.clan->member) (void)send_chat(*this, "/c owls, I am on", proto::ChatScope::Lobby);
                (void)send_chat(*this, "/w Bob thanks for the invite", proto::ChatScope::Lobby);
                (void)send_chat(*this, "hello lobby", proto::ChatScope::Lobby);
                next(932);
            }
            break;
        case 932:
            if (since > 5.0) {
                int by_scope[8]{};
                for (const auto& e : session_.lobby_chat) ++by_scope[std::min<int>(e.line.scope, 7)];
                LOG_INFO("autotest: social: the lobby's chat holds %d lobby, %d whisper, %d clan, %d global lines%s", by_scope[int(proto::ChatScope::Lobby)],
                         by_scope[int(proto::ChatScope::Whisper)], by_scope[int(proto::ChatScope::Clan)], by_scope[int(proto::ChatScope::Global)],
                         by_scope[int(proto::ChatScope::Global)] >= 2 && by_scope[int(proto::ChatScope::Whisper)] >= 1 ? "" : "  <-- FAILED");
                shot("S14_lobby_chat");
                RoomSettings rs;
                rs.title = "Autotest social";
                rs.mode = Mode::TeamDeathmatch;
                rs.map = opts_.map.empty() ? "crossroad" : opts_.map;
                rs.goal = 100;
                rs.minutes = 10;
                session_.create_room(rs);
                next(933);
            }
            break;
        case 933:
            // The room's Invite plate: the soldiers free to come, one picked and asked.
            if (screen_ == Screen::Room && since > 1.0) {
                st.invite_open = true;
                st.invite_sel = "Carol";
                next(937);
            }
            break;
        case 937:
            if (since > 0.8) {
                shot("S15_room_invite");
                session_.room_invite("Carol");
                st.invite_open = false;
                next(938);
            }
            break;
        case 938: {
            // She comes (the staged Carol says yes and readies up), and global chat is heard in the room.
            const size_t here = session_.room ? session_.room->members.size() : 0;
            bool ready = here >= 2;
            if (session_.room)
                for (const auto& m : session_.room->members) ready &= m.host || m.state == u8(proto::SlotState::Ready);
            if (screen_ == Screen::Room && ((ready && since > 4.0) || since > 9.0)) {
                int global = 0;
                for (const auto& e : session_.room_chat) global += e.line.scope == u8(proto::ChatScope::Global);
                LOG_INFO("autotest: social: the soldier invited %s; in the room, %d global lines heard%s", here >= 2 ? "came" : "did NOT come  <-- FAILED", global,
                         global ? "" : "  <-- FAILED");
                shot("S15b_room_chat");
                session_.start_match();
                next(934);
            }
            break;
        }
        case 934:
            if (screen_ == Screen::Match && since > 2.0) {
                (void)send_chat(*this, "/g in a match now", proto::ChatScope::All);
                next(935);
            }
            break;
        case 935: {
            // A global line from somebody else, heard over the match.
            bool heard = false;
            for (const auto& e : session_.room_chat)
                heard |= e.line.scope == u8(proto::ChatScope::Global) && e.line.from != session_.profile.code_name && now_ - e.time < 4.0 && e.time > autotest_at_;
            if (heard || since > 12.0) {
                LOG_INFO("autotest: social: in the match, global chat from the others is %s", heard ? "heard" : "NOT heard  <-- FAILED");
                next(936);
            }
            break;
        }
        case 936:
            if (since > 0.5) {
                shot("S16_match_chat");
                // Out of the match and its room, and into a Clan War channel: a Clan Battle room.
                leave_match();
                next(939);
            }
            break;
        case 939:
            if (since > 1.5) {
                if (session_.room) session_.leave_room();
                session_.join_channel(7);
                next(940);
            }
            break;
        case 940:
            if (screen_ == Screen::Lobby && session_.channel == 7 && since > 1.5) {
                LOG_INFO("autotest: social: into Clan War (ch 7) as a member of %s", session_.profile.clan.empty() ? "no clan  <-- FAILED" : session_.profile.clan.c_str());
                RoomSettings rs;
                rs.title = session_.profile.clan + " at home";
                rs.mode = Mode::TeamBattle;
                rs.map = opts_.map.empty() ? "crossroad" : opts_.map;
                rs.clan_battle = true;
                session_.create_room(rs);
                next(941);
            } else if (since > 10.0) {
                LOG_ERROR("autotest: social: the Clan War channel never opened  <-- FAILED");
                quit_ = true;
            }
            break;
        case 941:
            if (screen_ == Screen::Room && session_.room && since > 1.5) {
                const RoomSettings& rs = session_.room->settings;
                LOG_INFO("autotest: social: the room there is a %s: %s against %s%s", game_type_name(rs).c_str(), rs.red_clan.c_str(), rs.blue_clan.empty() ? "any clan" : rs.blue_clan.c_str(),
                         rs.clan_battle && rs.red_clan == session_.profile.clan ? "" : "  <-- FAILED");
                shot("S17_clan_battle_room");
                st.clan_open = true, st.clan_tab = 0;
                session_.request_clan();
                next(942);
            } else if (since > 10.0) {
                LOG_ERROR("autotest: social: the Clan Battle room never opened  <-- FAILED");
                quit_ = true;
            }
            break;
        case 942:
            if (since > 1.2) {
                shot("S18_clan_card_battles");
                quit_ = true;
            }
            break;
        // ── --shop: the shops, a gun rented then bought, sprays, the capsule machine, the staff's
        // Shop, a match with a spray, and its recording kept (pictures P*). This PC's own server:
        // the account is its Game Master.
        case 1000: {
            if (first) {
                g_tour_shop = session_.shop_config();
                const WeaponDef* m4 = nullptr;
                for (const WeaponDef& w : weapons())
                    if (w.model == "m4a1" && !w.admin_only && w.skin.empty()) m4 = &w;
                // A clean start: plenty of SP, no M4A1 (a run before bought it), a few coins' worth.
                const std::string me = std::to_string(session_.tv.account);
                session_.global_staff_edit(me, proto::AccountField::Sp, 5000000, {});
                if (m4) session_.global_staff_edit(me, proto::AccountField::RevokeWeapon, 0, m4->code);
                tour_shop_roll(0);
                st.shop_tab = 0, st.shop_cat = 0, st.shop_sel = m4 ? int(m4->id) : -1;
                st.shop_return = Screen::Lobby;
                go(Screen::Shop);
            }
            if (screen_ == Screen::Shop && since > 1.6 && session_.profile.sp >= 4000000 && session_.shop) {
                LOG_INFO("autotest: shop: the server's shop came (%zu things off the catalog, %zu capsules); SP %u", session_.shop->entries.size(), session_.shop->capsules.size(),
                         session_.profile.sp);
                shot("P00_weapon_shop");
                st.buy_kind = int(ShopKind::Weapon), st.buy_item = st.shop_sel, st.buy_offer = 0;
                next(1001);
            } else if (since > 10.0) {
                LOG_ERROR("autotest: shop: no shop or no SP from the server  <-- FAILED");
                quit_ = true;
            }
            break;
        }
        case 1001:
            if (since > 0.8) {
                shot("P01_buy_rent");
                // Rented for its shortest, then bought for good on top.
                session_.buy(ShopKind::Weapon, u16(st.shop_sel), 0);
                st.buy_item = -1;
                st.shop_tab = 2, st.inv_tab = 1, st.inv_sub = 0;
                next(1002);
            }
            break;
        case 1002:
            if (since > 1.2 && session_.owns_weapon(u16(st.shop_sel))) {
                const u32 left = session_.weapon_seconds_left(u16(st.shop_sel), now_);
                LOG_INFO("autotest: shop: rented: %s%s", time_left(left).c_str(), left > 6 * 86400 && left <= 7 * 86400 ? "" : "  <-- FAILED (not 7 days)");
                shot("P02_inventory_rented");
                const ShopLine line = session_.shop_line(ShopKind::Weapon, u16(st.shop_sel));
                session_.buy(ShopKind::Weapon, u16(st.shop_sel), u8(line.offers.size() - 1));
                next(1003);
            } else if (since > 8.0) {
                LOG_ERROR("autotest: shop: the rental never arrived  <-- FAILED");
                quit_ = true;
            }
            break;
        case 1003:
            if (since > 1.2) {
                const u32 left = session_.weapon_seconds_left(u16(st.shop_sel), now_);
                LOG_INFO("autotest: shop: then bought: %s%s", time_left(left).c_str(), left == proto::OwnedItem::kForGood ? "" : "  <-- FAILED (not for good)");
                shot("P03_inventory_owned");
                st.shop_tab = 3, st.item_tab = 1, st.item_sel = 3;
                next(1009);
            }
            break;
        case 1009:
            if (since > 1.2) {
                shot("P11_item_shop_sprays");
                session_.buy(ShopKind::Spray, 3, 1);
                st.shop_tab = 2, st.inv_tab = 4, st.item_sel = -1;
                next(1010);
            }
            break;
        case 1010:
            if (since > 1.2) {
                LOG_INFO("autotest: shop: carrying spray %u (%s)%s", unsigned(session_.spray), time_left(session_.spray_seconds_left(3, now_)).c_str(),
                         session_.spray == 3 ? "" : "  <-- FAILED (Spray 03 expected)");
                shot("P12_inventory_sprays");
                session_.buy_coins(5);
                st.shop_tab = 4, st.capsule_page = 0;
                if (!session_.shop_config().capsules.empty()) st.capsule_sel = session_.shop_config().capsules.front().id;
                next(1011);
            }
            break;
        case 1011:
            if (since > 1.5) {
                LOG_INFO("autotest: shop: %u capsule coins", session_.coins);
                shot("P13_capsule_machine");
                tour_shop_roll(-1);   // the machine's own odds
                if (!capsule_turn(*this)) LOG_ERROR("autotest: shop: the capsule could not be turned  <-- FAILED");
                next(1012);
            }
            break;
        case 1012: {
            // The machine shakes, then opens on the prize: the screen plays it out from the answer.
            static int step = 0;
            if (first) step = 0;
            if (step == 0 && since > 0.8) shot("P14_capsule_shaking"), step = 1;
            if (step == 1 && since > 3.2) {
                shot("P15_capsule_prize");
                step = 2;
            }
            if (step == 2 && since > 3.6) {
                st.staff_open = true, st.staff_tab = 5, st.shop_sub = 0, st.shop_kind = int(ShopKind::Weapon), st.shop_ware = st.shop_sel;
                st.shop_cfg.reset(), st.shop_dirty = false;
                next(1013);
            }
            break;
        }
        case 1013:
            if (since > 1.2) {
                shot("P16_staff_shop_catalog");
                // The K2 made for good only, through the editor's own copy, and a line of our own.
                ShopConfig c = session_.shop_config();
                const WeaponDef* k2 = weapon_by_code("A011");
                if (k2) set_shop_line(c, ShopKind::Weapon, k2->id, true, {{0, k2->price}});
                c.marquee = "Autotest: the Game Master's own line, panning along the bottom.";
                st.shop_cfg = c, st.shop_dirty = true;
                st.shop_sub = 1;
                next(1014);
            }
            break;
        case 1014:
            if (since > 0.8) {
                shot("P17_staff_shop_coins");
                st.shop_sub = 2;
                next(1015);
            }
            break;
        case 1015:
            if (since > 0.8) {
                shot("P18_staff_shop_capsules");
                st.shop_sub = 3;
                next(1016);
            }
            break;
        case 1016: {
            static bool snapped = false;
            if (first) snapped = false;
            if (since > 1.0 && !snapped) shot("P19_staff_shop_line"), snapped = true;
            if (since > 1.4) {
                session_.staff_save_shop(*st.shop_cfg);
                st.shop_dirty = false;
                st.staff_open = false;
                go(Screen::Lobby);
                next(1017);
            }
            break;
        }
        case 1017:
            if (since > 2.5) {
                const WeaponDef* k2 = weapon_by_code("A011");
                const ShopLine l = k2 ? session_.shop_line(ShopKind::Weapon, k2->id) : ShopLine{};
                LOG_INFO("autotest: shop: saved: the line reads \"%s\"%s; the K2 has %zu offer(s)%s", session_.shop_config().marquee.c_str(),
                         session_.shop_config().marquee.starts_with("Autotest") ? "" : "  <-- FAILED", l.offers.size(), l.offers.size() == 1 ? "" : "  <-- FAILED");
                shot("P20_lobby_line");
                // A match with all of it, kept by the room's Record plate.
                st.record_match = true;
                (void)recordings_listed(*this, &g_tour_kept);
                RoomSettings rs;
                rs.title = "Autotest shop";
                rs.mode = Mode::TeamDeathmatch;
                rs.map = opts_.map.empty() ? "crossroad" : opts_.map;
                rs.goal = 100;
                rs.minutes = 1;
                rs.bots = 1;   // somebody to shoot at
                session_.create_room(rs);
                next(1018);
            }
            break;
        case 1018:
            if (screen_ == Screen::Room && since > 1.0) {
                session_.start_match();
                next(1019);
            }
            break;
        case 1019:
            if (screen_ == Screen::Match && world_ && world_->ready() && world_->alive() && since > 3.0) {
                world_->set_slot_for_test(0);
                world_->test_place(0, 0.0f, 120.0f);
                next(1020);
            }
            break;
        case 1020:
            if (since > 1.5) {
                shot("P21_match");
                LOG_INFO("autotest: shop: the spray key: %s", world_->test_spray() ? "sprayed" : "nothing to spray on  <-- FAILED");
                next(1021);
            }
            break;
        case 1021:
            if (world_ && (world_->sprays_shown() > 0 || since > 4.0)) {
                LOG_INFO("autotest: shop: %zu spray(s) on the walls%s", world_->sprays_shown(), world_->sprays_shown() ? "" : "  <-- FAILED (the server never showed it)");
                shot("P22_spray");
                world_->test_full_clip();
                world_->autopilot(false, true);
                next(1022);
            }
            break;
        case 1022:
            if (since > 0.6) {
                world_->autopilot(false, false);
                LOG_INFO("autotest: shop: %d shots fired", world_->shots_fired());
                next(1030);
            }
            break;
        case 1030: {
            // The match's bot stood in front of you (the server's test hook: he walks his rounds and
            // is never told of otherwise); once he is seen and near, one round at him (another if it
            // met nobody), for the damage numbers over him.
            static bool asked = false;
            static double seen_since = -1;
            static int shots_before = -1, tries = 0;
            if (first) {
                asked = false, seen_since = -1, shots_before = -1, tries = 0;
                world_->look(world_->view_yaw() + 180.0f, 0.0f);   // the wall the spray is on behind you
            }
            if (!asked && since > 0.4) {
                asked = true;
#if LSF_WITH_SERVER
                if (local_server_) local_server_->test_bots_before(session_.session_id, 400.0f);
                else
#endif
                    LOG_ERROR("autotest: shop: not this PC's server: no bot to stand in front  <-- FAILED");
            }
            bool seen = false;
            for (const auto& [id, p] : world_->players())
                if (id != world_->me() && p.alive && !p.hidden && eng::length(p.position - world_->feet()) < 700.0f) seen = true;
            if (!seen) seen_since = -1;
            else if (seen_since < 0) seen_since = now_;
            if (seen) (void)world_->look_at_other();
            world_->autopilot(false, false);
            if (shots_before < 0 && seen && now_ - seen_since > 0.6) {
                shots_before = world_->shots_fired();
                world_->test_full_clip(), world_->autopilot(false, true);
            } else if (shots_before >= 0 && world_->shots_fired() > shots_before) {
                // The round is away: judged by what it met.
                if (world_->last_shot().starts_with("nobody") && tries < 3) ++tries, shots_before = -1, seen_since = now_ - 0.3;
                else next(1031);
            }
            if (since > 14.0) {
                LOG_ERROR("autotest: shop: the bot never came into sight  <-- FAILED");
                next(1031);
            }
            break;
        }
        case 1031: {
            (void)world_->look_at_other();
            if (since > 0.9 && since < 1.0) shot("P23_target_hit");
            if (since > 1.3) {
                for (const auto& [id, p] : world_->players())
                    if (id != world_->me())
                        LOG_INFO("autotest: shop: %s (team %d, mine %d): %d health, %s%s, %.0f cm off", p.name.c_str(), int(p.team), int(world_->players().at(world_->me()).team), p.health, p.alive ? "alive" : "dead", p.hidden ? ", hidden" : "",
                                 double(eng::length(p.position - world_->feet())));
                for (const auto& [id, p] : world_->players())
                    if (id != world_->me()) LOG_INFO("autotest: shop: the round: %s; the server says %d dealt to %s", world_->last_shot().c_str(), world_->dealt_to(id), p.name.c_str());
                for (const auto& [id, p] : world_->players())
                    if (id != world_->me()) {
                        const std::string nums = world_->damage_numbers_of(id);
                        LOG_INFO("autotest: shop: the damage numbers over %s: %s%s", p.name.c_str(), nums.empty() ? "(none)" : nums.c_str(), nums.empty() ? "  <-- FAILED" : "");
                    }
                next(1032);
            }
            break;
        }
        case 1032:
            if (since > 1.0 && since < 1.1) world_->test_scores(true);   // the Tab board, the client's own
            if (since > 1.5 && since < 1.6) shot("P23d_scoreboard");
            if (since > 2.0) {
                world_->test_scores(false);
                next(1024);
            }
            break;
        case 1024: {
            // The minute runs out; the Record plate keeps the match on this PC.
            static double waited = 0;
            static bool snapped = false;
            if (first) waited = 0, snapped = false;
            waited += dt_;
            autotest_at_ = now_ - std::min(since, 1.0);
            int kept = 0;
            (void)recordings_listed(*this, &kept);
            if (screen_ == Screen::Result && !snapped) shot("P24_result"), snapped = true;
            if (kept > g_tour_kept && screen_ != Screen::Match) {
                LOG_INFO("autotest: shop: the match ended after %.0f s, and its recording is kept on this PC", waited);
                st.shop_return = Screen::Lobby;
                st.replays_asked = false;
                next(1025);
            } else if (waited > 120.0) {
                LOG_ERROR("autotest: shop: the match's recording was never kept  <-- FAILED");
                quit_ = true;
            }
            break;
        }
        case 1025:
            if (first) {
                if (session_.match_over) session_.match_over.reset();
                go(Screen::Recordings);
            }
            if (since > 2.0) {
                int kept = 0;
                const int listed = recordings_listed(*this, &kept);
                LOG_INFO("autotest: shop: Replay lists %d match(es), %d kept on this PC", listed, kept);
                if (session_.replays && !session_.replays->replays.empty()) st.replay_sel = int(session_.replays->replays.front().match);
                next(1026);
            }
            break;
        case 1026:
            if (since > 0.8) {
                shot("P25_recordings");
                if (!play_recording(*this, u32(st.replay_sel))) LOG_ERROR("autotest: shop: the recording could not be played  <-- FAILED");
                next(1027);
            }
            break;
        case 1027: {
            // Watched from the start, then taken half way on: the spray made in it is on its wall.
            static int step = 0;
            if (first) step = 0;
            if (step == 0 && screen_ == Screen::Replay && world_ && world_->ready() && since > 4.0) {
                shot("P26_recording_watched");
                replay_seek(replay_length_ms() * 0.5);
                step = 1;
                autotest_at_ = now_;
            } else if (step == 1 && since > 2.0) {
                LOG_INFO("autotest: shop: watching match %u half way: %zu spray(s) on the walls in it%s", watching_ ? watching_->match : 0u, world_ ? world_->sprays_shown() : size_t(0),
                         world_ && world_->sprays_shown() ? "" : "  <-- FAILED");
                leave_replay();
                // The shop as it was before the tour (the line, the K2).
                if (g_tour_shop) session_.staff_save_shop(*g_tour_shop);
                next(1028);
            } else if (step == 0 && since > 30.0) {
                LOG_ERROR("autotest: shop: the recording never played  <-- FAILED");
                quit_ = true;
            }
            break;
        }
        case 1028:
            if (since > 1.5) {
                LOG_INFO("autotest: shop: the shop put back: the line reads \"%s\"", session_.shop_config().marquee.c_str());
                quit_ = true;
            }
            break;
        // ── --cannon: Pirate Mode, a shore's cannon: seen, manned, fired (pictures C*) ──
        case 1200:
            if (first) {
                RoomSettings rs;
                rs.title = "Autotest cannon";
                rs.mode = Mode::Pirate;
                rs.map = "pirateship";
                rs.minutes = 5;
                rs.bots = 0;   // alone: nobody to shoot the gunner while he is photographed
                session_.create_room(rs);
            }
            if (screen_ == Screen::Room && since > 1.0) {
                session_.start_match();
                next(1201);
            } else if (since > 12.0) {
                LOG_ERROR("autotest: cannon: the room never opened  <-- FAILED");
                quit_ = true;
            }
            break;
        case 1201: {
            const auto spots = pirate_cannons("pirateship");
            if (screen_ == Screen::Match && world_ && world_->ready() && world_->alive() && since > 3.0 && spots.size() > 2) {
                // Behind the cannon, looking along it: is it there to see?
                const CannonSpot& c = spots[2];
                float yaw = 0, best = -2;
                for (int y = 0; y < 360; ++y)
                    if (const float d = eng::dot(eng::angles_to_forward(float(y), 0), c.forward); d > best) best = d, yaw = float(y);
                st.use_value = int(yaw);
                world_->test_stand(c.at - c.forward * 260.0f, yaw, -4.0f);
                next(1202);
            } else if (since > 40.0) {
                LOG_ERROR("autotest: cannon: the match never began  <-- FAILED");
                quit_ = true;
            }
            break;
        }
        case 1202:
            if (since > 1.2) {
                const auto spots = pirate_cannons("pirateship");
                const CannonSpot& c = spots[2];
                shot("C00_cannon_from_behind");
                // From its side: the way it points is the way its table says.
                const eng::Vec3 side{-c.forward.z, 0, c.forward.x};
                float yaw = 0, best = -2;
                for (int y = 0; y < 360; ++y)
                    if (const float d = eng::dot(eng::angles_to_forward(float(y), 0), side * -1.0f); d > best) best = d, yaw = float(y);
                st.use_value = int(yaw) * 1000 + st.use_value;   // the side's yaw, and the cannon's own
                // From behind and to one side, far enough to take the whole of it in.
                const eng::Vec3 from = c.at - c.forward * 520.0f + side * 420.0f;
                float look = 0;
                best = -2;
                for (int y = 0; y < 360; ++y)
                    if (const float d = eng::dot(eng::angles_to_forward(float(y), 0), eng::normalize(c.at - from)); d > best) best = d, look = float(y);
                world_->test_stand(from, look, -10.0f);
                next(1206);
            }
            break;
        case 1206:
            if (since > 1.0) {
                const auto spots = pirate_cannons("pirateship");
                const CannonSpot& c = spots[2];
                shot("C00b_cannon_from_its_side");
                world_->test_stand(c.at - c.forward * 70.0f, float(st.use_value % 1000), 12.0f);
                next(1203);
            }
            break;
        case 1203:
            if (since > 1.0) {
                shot("C01_cannon_beside_it");
                proto::CannonUse m;
                m.op = u8(proto::CannonOp::Man), m.index = 2;
                session_.send(m);
                next(1204);
            }
            break;
        case 1204:
            if (since > 0.8) {
                LOG_INFO("autotest: cannon: %s", world_->manning_cannon_for_test() ? "manned" : "NOT manned  <-- FAILED");
                shot("C02_cannon_manned");
                proto::CannonUse m;
                m.op = u8(proto::CannonOp::Fire), m.index = 2, m.aim = eng::angles_to_forward(float(st.use_value % 1000), 12.0f);
                session_.send(m);
                const auto spots = pirate_cannons("pirateship");
                const CannonSpot& c = spots[2];
                const eng::Vec3 side{-c.forward.z, 0, c.forward.x};
                // From behind the cannon and a little to its right, along the ball's flight: it is in the
                // picture whenever it is in the air, clear of the gun in hand.
                world_->test_stand(c.at - c.forward * 320.0f - side * 220.0f, float(st.use_value % 1000), 4.0f);
                next(1205);
            }
            break;
        case 1205: {
            static int step = 0;
            static double seen_at = -1;
            if (first) step = 0, seen_at = -1;
            // The ball is the server's to launch: waited for, then photographed a quarter of a second
            // into its flight (some four metres out of the barrel).
            if (seen_at < 0 && world_->balls_for_test() > 0) seen_at = now_;
            if (step == 0 && (seen_at >= 0 ? now_ - seen_at > 0.25 : since > 2.5)) {
                LOG_INFO("autotest: cannon: %zu ball(s) in the air%s", world_->balls_for_test(), world_->balls_for_test() ? "" : "  <-- FAILED");
                LOG_INFO("autotest: cannon: the ball is %s", world_->ball_report().c_str());
                shot("C03_cannon_ball"), step = 1;
            } else if (step == 1 && since > 2.6) {
                shot("C04_cannon_loading");
                quit_ = true;
            }
            break;
        }
        // ── --knife: the melee weapon swung, from the eye and from outside (pictures K*). Alone in a
        // room that lets the camera out (POV), so the soldier's own body can be photographed.
        case 1300: {
            // The blade asked for, bought if need be and put in the kit: the room is made only once
            // the server says it is carried (a loadout naming a blade not yet owned is refused, and
            // the M9 stays in hand).
            static int tries = 0;
            static double acted_at = -10;
            static bool rebuy = false;
            const WeaponDef* want = nullptr;
            for (const WeaponDef& w : weapons())
                if (!opts_.weapon.empty() && eng::str::iequals(w.code, opts_.weapon)) want = &w;
            if (first) tries = 0, acted_at = -10, rebuy = false;
            const bool owned = !want || std::find(session_.owned_weapons.begin(), session_.owned_weapons.end(), want->id) != session_.owned_weapons.end();
            const bool carried = !want || in_kit(session_.profile.loadout, want->id);
            // Given when it is not owned; put in the kit when it is; given afresh when the kit still
            // does not have it a try later.
            if (want && !carried && since > 0.5 && now_ - acted_at > 2.0 && tries < 6) {
                if (!owned || rebuy) {
                    // Given for good by this PC's server's Game Master (the autotest account): no SP
                    // needed, and no rental to run out between runs.
                    session_.global_staff_edit(std::to_string(session_.tv.account), proto::AccountField::GrantWeapon, 0, want->code);
                    rebuy = false;
                } else {
                    auto lo = session_.profile.loadout;
                    lo[equip_cell(lo, *want)] = want->id;
                    session_.set_loadout(session_.profile.force, lo);
                    rebuy = true;
                }
                ++tries, acted_at = now_;
                LOG_INFO("autotest: knife: try %d: the %s %s, %s; kit slot %u holds %u; %u SP", tries, want->name.c_str(), owned ? "owned" : "not owned",
                         rebuy ? "asked for in the kit" : "given", unsigned(want->slot), unsigned(session_.profile.loadout[equip_cell(session_.profile.loadout, *want)]),
                         unsigned(session_.profile.sp));
            }
            if (carried && screen_ == Screen::Lobby && since > 1.0) {
                if (want) LOG_INFO("autotest: knife: carrying the %s (%s)", want->name.c_str(), want->code.c_str());
                RoomSettings rs;
                rs.title = "Autotest knife";
                rs.mode = Mode::TeamDeathmatch;
                rs.map = opts_.map.empty() ? "killhouse" : opts_.map;
                rs.minutes = 5;
                rs.bots = 0;
                rs.third_person = true;
                session_.create_room(rs);
                next(1303);
            } else if (since > 20.0) {
                LOG_ERROR("autotest: knife: the %s never came into the kit  <-- FAILED", want ? want->name.c_str() : "blade");
                quit_ = true;
            }
            break;
        }
        case 1303:
            if (screen_ == Screen::Room && since > 1.5) {
                session_.start_match();
                next(1301);
            } else if (since > 15.0) {
                LOG_ERROR("autotest: knife: the room never opened  <-- FAILED");
                quit_ = true;
            }
            break;
        case 1301:
            if (screen_ == Screen::Match && world_ && world_->ready() && world_->alive() && since > 3.0) {
                world_->face_open_space();
                world_->set_slot_for_test(int(Slot::Melee));
                next(1302);
            } else if (since > 40.0) {
                LOG_ERROR("autotest: knife: the match never began  <-- FAILED");
                quit_ = true;
            }
            break;
        case 1302: {
            // Each step: wait, photograph (or act), move on. The swings are caught twice each: as
            // the blade goes out and as it comes back.
            struct Step {
                double wait;
                const char* picture;   // nullptr: no picture
                int act;               // 0 none, 1 attack, 2 trigger up, 3 run, 4 stop, 5 outside the body, 6 seen from the front
            };
            static const Step kSteps[] = {
                {1.6, "K00_knife_at_rest", 1},      {0.12, "K01_first_swing_out", 2},  {0.22, "K02_first_swing_through", 0},
                {0.9, "K03_at_rest_again", 1},      {0.12, "K04_second_swing_out", 2}, {0.22, "K05_second_swing_through", 0},
                {0.9, nullptr, 1},                  {0.12, "K06_third_swing_out", 2},  {1.2, nullptr, 3},
                {1.0, "K07_running", 4},            {0.5, "K08_stopping", 0},          {1.2, nullptr, 5},
                {0.8, "K09_outside_at_rest", 1},    {0.12, "K10_outside_swing_out", 2}, {0.22, "K11_outside_swing_through", 0},
                {0.9, nullptr, 1},                  {0.12, "K12_outside_second_swing", 2}, {0.9, nullptr, 6},
                {0.6, "K13_front_at_rest", 1},      {0.15, "K14_front_swing", 2},      {0.8, nullptr, 3},
                {0.8, "K15_outside_running", 4},
            };
            static size_t step = 0;
            static double step_at = 0;
            if (first) step = 0, step_at = now_;
            if (step >= std::size(kSteps)) {
                quit_ = true;
                break;
            }
            const Step& s = kSteps[step];
            if (now_ - step_at < s.wait || !world_) break;
            if (s.picture) {
                LOG_INFO("autotest: knife: %s: view %s; body %s; %d swings", s.picture, world_->view_clip().c_str(), world_->body_clips().c_str(),
                         world_->shots_fired());
                shot(s.picture);
            }
            switch (s.act) {
                case 1: world_->autopilot(false, true); break;
                case 2: world_->autopilot(false, false); break;
                case 3: world_->autopilot(true, false); break;
                case 4: world_->autopilot(false, false); break;
                case 5: world_->toggle_third_person(), world_->test_orbit(50.0f, -8.0f); break;
                case 6: world_->test_orbit(165.0f, -6.0f); break;
                default: break;
            }
            ++step, step_at = now_;
            break;
        }
        // ── --throws: three throwables in the kit (the M67, a flash-bang and a smoke), each taken out
        // with the fourth key in turn, the switch bar photographed with each (pictures T*).
        case 1400: {
            const WeaponDef* frag = weapon_by_model("m67");
            const WeaponDef* flash = weapon_by_model("flashbang");
            const WeaponDef* smoke = weapon_by_model("m18");
            if (!frag || !flash || !smoke) {
                LOG_ERROR("autotest: throws: the roster has no M67, flash-bang or M18  <-- FAILED");
                quit_ = true;
                break;
            }
            static double asked_at = -10;
            if (first) {
                asked_at = -10;
                for (const WeaponDef* w : {flash, smoke}) session_.global_staff_edit(std::to_string(session_.tv.account), proto::AccountField::GrantWeapon, 0, w->code);
            }
            const Loadout& lo = session_.profile.loadout;
            const bool carried = lo[3] == frag->id && lo[4] == flash->id && lo[5] == smoke->id;
            const bool owned = session_.owns_weapon(flash->id) && session_.owns_weapon(smoke->id);
            if (!carried && owned && now_ - asked_at > 2.0) {
                Loadout want = lo;
                want[3] = frag->id, want[4] = flash->id, want[5] = smoke->id;
                session_.set_loadout(session_.profile.force, want);
                asked_at = now_;
            }
            if (carried && screen_ == Screen::Lobby && since > 1.0) {
                LOG_INFO("autotest: throws: the kit carries the %s, the %s and the %s", frag->name.c_str(), flash->name.c_str(), smoke->name.c_str());
                RoomSettings rs;
                rs.title = "Autotest throws";
                rs.mode = Mode::TeamDeathmatch;
                rs.map = opts_.map.empty() ? "killhouse" : opts_.map;
                rs.minutes = 5;
                rs.bots = 0;
                session_.create_room(rs);
                next(1403);
            } else if (since > 20.0) {
                LOG_ERROR("autotest: throws: the three throwables never came into the kit (%s; cells %u %u %u)  <-- FAILED", owned ? "owned" : "not owned",
                          unsigned(lo[3]), unsigned(lo[4]), unsigned(lo[5]));
                quit_ = true;
            }
            break;
        }
        case 1403:
            if (screen_ == Screen::Room && since > 1.5) {
                session_.start_match();
                next(1401);
            } else if (since > 15.0) {
                LOG_ERROR("autotest: throws: the room never opened  <-- FAILED");
                quit_ = true;
            }
            break;
        case 1401:
            if (screen_ == Screen::Match && world_ && world_->ready() && world_->alive() && since > 3.0) {
                world_->face_open_space();
                next(1402);
            } else if (since > 40.0) {
                LOG_ERROR("autotest: throws: the match never began  <-- FAILED");
                quit_ = true;
            }
            break;
        case 1402: {
            // Each step: wait, act, and a moment later photograph and say which cell is in hand.
            struct Step {
                double wait;
                const char* picture;
                int act;        // 0 none, 1 the fourth key, 2 the first key, 3 a grenade thrown
                int want_cell;  // the cell in hand by the picture (-1: not checked)
            };
            static const Step kSteps[] = {
                {1.0, "T00_rifle_in_hand", 1, 0},          {0.5, "T01_key4_frag", 1, 3},   {0.5, "T02_key4_flashbang", 1, 4},
                {0.5, "T03_key4_smoke", 1, 5},             {0.5, "T04_key4_round_to_frag", 2, 3}, {0.5, "T05_key1_rifle", 0, 0},
                {2.4, "T06_bar_gone", 1, 0},               {1.0, nullptr, 3, 3},                  {2.5, "T07_a_frag_thrown_one_left", 0, 3},
            };
            static size_t step = 0;
            static double step_at = 0;
            if (first) step = 0, step_at = now_;
            if (step >= std::size(kSteps)) {
                quit_ = true;
                break;
            }
            const Step& s = kSteps[step];
            if (now_ - step_at < s.wait || !world_) break;
            if (s.picture) {
                const WeaponDef* held = weapon(world_->loadout()[size_t(std::clamp(world_->slot(), 0, int(kLoadoutSlots) - 1))]);
                const bool right = s.want_cell < 0 || world_->slot() == s.want_cell;
                LOG_INFO("autotest: throws: %s: cell %d in hand (%s)%s", s.picture, world_->slot(), held ? held->name.c_str() : "nothing",
                         right ? "" : eng::str::format(", wanted %d  <-- FAILED", s.want_cell).c_str());
                shot(s.picture);
            }
            switch (s.act) {
                case 1: world_->test_next_throwable(); break;
                case 2: world_->set_slot_for_test(int(Slot::Primary)); break;
                case 3: (void)world_->test_throw(); break;
                default: break;
            }
            ++step, step_at = now_;
            break;
        }
        // ── --wear: guns' wear in the inventory and the room (pictures W*). This PC's own server:
        // the account is its Game Master, who gives itself three guns and wears them.
        case 1100: {
            const WeaponDef* g36c = weapon_by_model("g36c");
            const WeaponDef* k2 = weapon_by_model("k2");
            const WeaponDef* mp5 = weapon_by_model("mp5");
            const std::string me = std::to_string(session_.tv.account);
            if (first && g36c && k2 && mp5) {
                session_.global_staff_edit(me, proto::AccountField::Sp, 100000, {});
                for (const WeaponDef* w : {g36c, k2, mp5}) session_.global_staff_edit(me, proto::AccountField::GrantWeapon, 0, w->code);
                session_.global_staff_edit(me, proto::AccountField::Durability, 62, g36c->code);
                session_.global_staff_edit(me, proto::AccountField::Durability, 14, k2->code);
                session_.global_staff_edit(me, proto::AccountField::Durability, 0, mp5->code);
            }
            if (since > 1.2 && since < 1.3 && k2) {
                auto lo = session_.profile.loadout;
                lo[0] = k2->id;
                session_.set_loadout(session_.profile.force, lo);
                st.shop_tab = 2, st.inv_tab = 1, st.inv_sub = 0, st.shop_sel = int(k2->id);
                st.shop_return = Screen::Lobby;
                go(Screen::Shop);
            }
            if (since > 3.0 && g36c && k2 && mp5) {
                const bool right = session_.durability_of(g36c->id) == 62 && session_.durability_of(k2->id) == 14 && session_.durability_of(mp5->id) == 0;
                const WeaponDef* knife = weapon(starter_loadout()[2]);
                LOG_INFO("autotest: wear: the G36C %u%%, the K2 %u%%, the MP5 %u%%%s; the issued M4A1 %u%% and the knife %u%% (they never wear)", unsigned(session_.durability_of(g36c->id)),
                         unsigned(session_.durability_of(k2->id)), unsigned(session_.durability_of(mp5->id)), right ? "" : "  <-- FAILED",
                         unsigned(session_.durability_of(starter_loadout()[0])), knife ? unsigned(session_.durability_of(knife->id)) : 0u);
                shot("W00_inventory_wear");
                st.deal_kind = int(ShopKind::Weapon), st.deal_item = int(k2->id), st.deal_op = 0;
                next(1101);
            } else if (since > 10.0) {
                LOG_ERROR("autotest: wear: the guns never came  <-- FAILED");
                quit_ = true;
            }
            break;
        }
        case 1101:
            if (since > 0.8) {
                const u16 k2 = u16(st.deal_item);
                const u32 cost = session_.repair_price(k2);
                LOG_INFO("autotest: wear: mending the K2 from 14%% costs SP %u", cost);
                shot("W01_repair_dialog");
                session_.repair(k2);
                st.deal_item = -1;
                st.shop_sel = int(k2);
                next(1102);
            }
            break;
        case 1102:
            if (since > 1.2) {
                const u16 k2 = u16(st.shop_sel);
                LOG_INFO("autotest: wear: mended: the K2 is at %u%%, SP %u%s", unsigned(session_.durability_of(k2)), session_.profile.sp,
                         session_.durability_of(k2) == kDurabilityFull && session_.profile.sp < 100000 ? "" : "  <-- FAILED");
                const WeaponDef* g36c = weapon_by_model("g36c");
                st.deal_kind = int(ShopKind::Weapon), st.deal_item = g36c ? int(g36c->id) : -1, st.deal_op = 1;
                next(1103);
            }
            break;
        case 1103:
            if (since > 0.8) {
                const u16 gun = u16(std::max(0, st.deal_item));
                const u32 worth = session_.resale_of(ShopKind::Weapon, gun);
                const u32 before = session_.profile.sp;
                LOG_INFO("autotest: wear: the G36C at 62%% sells for SP %u", worth);
                shot("W02_sell_dialog");
                session_.sell(ShopKind::Weapon, gun);
                st.deal_item = -1;
                st.use_value = int(before + worth);   // what the SP should come to
                st.shop_sel = int(gun);
                next(1104);
            }
            break;
        case 1104:
            if (since > 1.2) {
                const bool sold = !session_.owns_weapon(u16(st.shop_sel)) && int(session_.profile.sp) == st.use_value;
                LOG_INFO("autotest: wear: sold: SP %u%s", session_.profile.sp, sold ? "" : "  <-- FAILED");
                st.inv_sub = 3;   // the knives: no bar, no Repair
                shot("W03_inventory_after");
                next(1105);
            }
            break;
        case 1105:
            if (since > 0.8) {
                shot("W04_inventory_knives");
                st.inv_sub = 0;
                // The room's own bars and Repair plates: the broken MP5 in the kit.
                if (const WeaponDef* mp5 = weapon_by_model("mp5")) {
                    auto lo = session_.profile.loadout;
                    lo[0] = mp5->id;
                    session_.set_loadout(session_.profile.force, lo);
                }
                RoomSettings rs;
                rs.title = "Autotest wear";
                rs.mode = Mode::TeamDeathmatch;
                rs.map = opts_.map.empty() ? "crossroad" : opts_.map;
                session_.create_room(rs);
                next(1106);
            }
            break;
        case 1106:
            if (screen_ == Screen::Room && since > 1.5) {
                shot("W05_room_wear");
                const WeaponDef* mp5 = weapon_by_model("mp5");
                if (mp5) session_.repair(mp5->id);
                next(1107);
            } else if (since > 10.0) {
                LOG_ERROR("autotest: wear: the room never opened  <-- FAILED");
                quit_ = true;
            }
            break;
        case 1107:
            if (since > 1.2) {
                const WeaponDef* mp5 = weapon_by_model("mp5");
                const u8 left = mp5 ? session_.durability_of(mp5->id) : u8(0);
                LOG_INFO("autotest: wear: the room's Repair: the MP5 is at %u%%%s", unsigned(left), left == kDurabilityFull ? "" : "  <-- FAILED");
                shot("W06_room_mended");
                // The ID card: your own, its three tabs, and a line written on it.
                session_.set_card_message("Autotest reporting for duty.");
                open_id_card(*this, session_.profile.code_name);
                next(1108);
            }
            break;
        case 1108: {
            static int step = 0;
            if (first) step = 0;
            const bool came = session_.id_card && session_.id_card->found;
            if (step == 0 && since > 1.5) {
                LOG_INFO("autotest: card: %s%s", came ? eng::str::format("%s: %u games, the line \"%s\"", session_.id_card->code_name.c_str(), session_.id_card->matches,
                                                                       session_.id_card->message.c_str()).c_str()
                                                     : "no card came", came && !session_.id_card->message.empty() ? "" : "  <-- FAILED");
                shot("W07_id_card_record");
                st.card_tab = 1, step = 1;
            } else if (step == 1 && since > 3.5) {
                shot("W08_id_card_equipment");
                st.card_tab = 2, step = 2;
            } else if (step == 2 && since > 4.3) {
                shot("W09_id_card_weapons");
                // The Item Shop's Horror items and its Supply Crate; a gift waiting in the Gift tab.
                st.card_open = false;
                if (session_.room) session_.leave_room();
                session_.buy_horror_item(HorrorItem::Rebirth, 2);
                session_.buy_horror_item(HorrorItem::SilverBullet, 1);
                next(1109);
            }
            break;
        }
        case 1109: {
            // Out of the room first (leaving it lands on the lobby), then into the Item Shop.
            static bool opened = false;
            if (first) opened = false;
            if (!opened && !session_.room && screen_ == Screen::Lobby && since > 1.0) {
                st.shop_tab = 3, st.item_tab = 2, st.item_sel = 0;
                st.shop_return = Screen::Lobby;
                go(Screen::Shop);
                opened = true;
                autotest_at_ = now_;
            } else if (opened && screen_ == Screen::Shop && since > 1.5) {
                LOG_INFO("autotest: shop: Horror items carried: %u Rebirth, %u Silver Bullet%s", unsigned(session_.horror_items[size_t(HorrorItem::Rebirth)]),
                         unsigned(session_.horror_items[size_t(HorrorItem::SilverBullet)]), session_.horror_items[size_t(HorrorItem::Rebirth)] >= 2 ? "" : "  <-- FAILED");
                shot("W10_item_shop_horror");
                st.item_tab = 3, st.item_sel = 7;
                next(1110);
            }
            break;
        }
        case 1110:
            if (since > 1.0) {
                shot("W11_item_shop_supply_crate");
                // A gun bought to see the dialog's gift switch.
                if (const WeaponDef* mp5 = weapon_by_model("mp5")) st.buy_kind = int(ShopKind::Weapon), st.buy_item = int(mp5->id), st.buy_offer = 0, st.buy_gift = true;
                next(1111);
            }
            break;
        case 1111:
            if (since > 1.0) {
                shot("W12_buy_as_gift");
                st.buy_item = -1;
                // The staff's Shop, its prices page: "If gifted: % discount".
                st.staff_open = true, st.staff_tab = 5, st.shop_sub = 1;
                ShopConfig c = session_.shop_config();
                c.gift_discount = 20;
                st.shop_cfg = c, st.shop_dirty = true;
                next(1112);
            }
            break;
        case 1112:
            if (since > 1.2) {
                shot("W13_staff_shop_gift_discount");
                st.shop_sub = 2;   // the Capsules page: what a turn costs, and what each holds
                next(1113);
            }
            break;
        case 1113:
            if (since > 0.8) {
                shot("W14_staff_shop_capsules");
                // A Duffle Bag of the Game Master's own making, on its page: the official server's
                // alone (TVAS sells them everywhere); any other's page shows TV's, to read.
                if (session_.on_official_server()) {
                    CapsuleDef bag;
                    bag.name = "Rifleman's Duffle Bag";
                    bag.price = 8000;
                    bag.picture = kBagPicture;
                    if (const WeaponDef* g36c = weapon_by_model("g36c")) bag.prizes.push_back({u8(CapsulePrizeKind::Weapon), g36c->id, 7, 0, 0, 10});
                    if (const WeaponDef* k2 = weapon_by_model("k2")) bag.prizes.push_back({u8(CapsulePrizeKind::Weapon), k2->id, 30, 0, 0, 4});
                    bag.prizes.push_back({u8(CapsulePrizeKind::Spray), 0, 7, 0, 0, 20});
                    bag.prizes.push_back({u8(CapsulePrizeKind::Sp), 0, 0, 1000, 3000, 30});
                    if (st.shop_cfg) st.shop_cfg->bags.push_back(bag), st.shop_dirty = true;
                }
                session_.refresh_tv_shop();
                st.shop_sub = 4, st.shop_bag = 0;
                next(1114);
            }
            break;
        case 1114:
            if (since > 0.8) {
                shot("W15_staff_shop_duffle_bags");
                // Saved, it is on sale in the Item Shop's Duffle Bag tab (once TVAS has it).
                g_tour_shop = session_.shop_config();
                if (st.shop_cfg && session_.on_official_server()) session_.staff_save_shop(*st.shop_cfg);
                st.shop_cfg.reset(), st.shop_dirty = false;
                st.staff_open = false;
                st.shop_tab = 3, st.item_tab = 4, st.item_sel = -1;
                next(1115);
            }
            break;
        case 1115:
            if (since > 1.5) {
                // The bags TVAS sells. Off the official server there may be none: only its Game
                // Masters put them up.
                const auto& bags = session_.bags_on_sale();
                const bool official = session_.on_official_server();
                LOG_INFO("autotest: shop: %zu Duffle Bag(s) on sale from TVAS%s", bags.size(),
                         bags.empty() ? (official ? "  <-- FAILED" : " (only the official server's Game Masters put them up)") : "");
                shot("W16_item_shop_duffle_bag");
                if (!bags.empty()) session_.buy_bag(bags.front().id, 2);
                st.shop_tab = 2, st.inv_tab = 3, st.gift_sel = -1;
                next(1116);
            }
            break;
        case 1116:
            if (since > 1.5) {
                LOG_INFO("autotest: shop: %zu kind(s) of Duffle Bag waiting in the Gift tab%s", session_.bags.size(),
                         session_.bags.empty() && !session_.bags_on_sale().empty() ? "  <-- FAILED" : "");
                shot("W17_gift_tab_duffle_bag");
                if (!session_.bags.empty()) session_.open_bag(session_.bags.front().id);
                next(1117);
            }
            break;
        case 1117:
            if (since > 1.5) {
                shot("W18_duffle_bag_opened");
                // The shop as it was before the tour.
                if (g_tour_shop) session_.staff_save_shop(*g_tour_shop);
                next(1118);
            }
            break;
        case 1118:
            if (since > 1.2) quit_ = true;
            break;
        // ── --games: the staff panel's Games tab (the server's Game Masters), Horror, Horror Mode 2,
        // the Pirate Ship and a map switched off and saved, then Make Room opened on the Pirate Ship:
        // it starts on a game type that is on, and its selector skips the ones off (pictures G*).
        case 1500:
            if (first) st.staff_open = true, st.staff_tab = 8, st.staff_games_loaded = false;
            if (since > 1.2) {
                LOG_INFO("autotest: games: the tab is %s", session_.games_editable ? "offered" : "not offered  <-- FAILED");
                shot("G1_games_tab");
                st.staff_games.modes_off = u16((1u << unsigned(Mode::Pirate)) | (1u << unsigned(Mode::Horror)) | (1u << unsigned(Mode::Horror2)));
                st.staff_games.maps_off = {"killhouse"};
                next(1501);
            }
            break;
        case 1501:
            if (since > 0.8) {
                shot("G2_games_changed");
                session_.games_edit(st.staff_games);
                next(1502);
            }
            break;
        case 1502:
            if (since > 1.5) {
                const bool held = session_.games.modes_off == st.staff_games.modes_off && !session_.games.takes_map("killhouse");
                LOG_INFO("autotest: games: the server plays %d game types, %zu map(s) off%s", std::popcount(unsigned(u16(~session_.games.modes_off) & ((1u << unsigned(Mode::Count)) - 1))),
                         session_.games.maps_off.size(), held ? "" : "  <-- FAILED");
                shot("G3_games_saved");
                st.staff_open = false;
                st.create = RoomSettings{};
                st.create.mode = Mode::Pirate, st.create.map = "pirate";
                st.create_open = true;
                next(1503);
            }
            break;
        case 1503:
            if (since > 1.2) {
                const auto maps = maps_for(st.create.mode);
                const bool killhouse = std::find(maps.begin(), maps.end(), "killhouse") != maps.end();
                LOG_INFO("autotest: games: Make Room opened on %s%s; the killhouse %s", mode_name(st.create.mode),
                         session_.games.takes_mode(st.create.mode) ? "" : "  <-- FAILED", killhouse ? "is listed  <-- FAILED" : "is not listed");
                shot("G4_make_room");
                RoomSettings turn;
                turn.mode = Mode::Occupy;
                step_game_type(turn, +1, session_.modes_allowed());
                LOG_INFO("autotest: games: after Occupy the selector goes to %s%s", mode_name(turn.mode), turn.mode == Mode::TeamBattle ? "" : "  <-- FAILED");
                st.create_open = false;
                // Everything back on.
                session_.games_edit(ServerGames{});
                next(1504);
            }
            break;
        case 1504:
            if (since > 1.5) {
                LOG_INFO("autotest: games: back to every game type and map%s", session_.games.modes_off == 0 && session_.games.maps_off.empty() ? "" : "  <-- FAILED");
                quit_ = true;
            }
            break;
        case 14:
            if (since > 0.5) {
                if (world_) world_->autopilot(false, false);
                float peak, rms;
                mixer_.take_levels(peak, rms);
                mixer_.stop_capture();
                LOG_INFO("autotest: match sound peak %.2f, rms %.3f (%d voices at the end)", double(peak), double(rms), mixer_.voices());
                quit_ = true;
            }
            break;
        default:
            break;
    }
    // Nothing should take this long; give up rather than hang a test run.
    if (since > 40.0) {
        LOG_ERROR("autotest: stuck at stage %d on %s", autotest_stage_, screen_name(screen_));
        shot("stuck");
        quit_ = true;
    }
}

}  // namespace lsf
