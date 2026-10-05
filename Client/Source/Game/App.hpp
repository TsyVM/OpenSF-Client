// legacysf: the game. One App runs the window, the renderer (DirectX 12, 11 or OpenGL; OpenGL ES
// on a phone), the interface, the sound, the connection to a server (or a server of its own,
// for playing on this machine), the front end's screens and the match.
//
// The screens follow the original's flow (Docs/Research.md §2): Boot -> Servers (sign in) ->
// Code name (first time) -> Channels -> Lobby -> Room -> Loading -> Match -> Results -> Room.
#pragma once

#include "Engine/Audio/Mixer.hpp"
#include "Engine/Platform/Gamepad.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Render/Device.hpp"
#include "Engine/UI/UiLayer.hpp"
#include "Game/Audio/Sounds.hpp"
#include "Game/Modes.hpp"
#include "Game/Net/Session.hpp"
#include "Game/PackMount.hpp"
#include "Game/Render/FramePipe.hpp"
#include "Game/Settings.hpp"
#include "Game/Ui/Atlas.hpp"
#include "SF/Data.hpp"
#include "SF/Level.hpp"
#include "SF/UiData.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lsfs {
class Server;
}

namespace lsf {

class GameWorld;
class ModelStage;
struct ScreenState;

enum class Screen { Boot, Servers, CodeName, Channels, Lobby, Room, Shop, Loading, Match, Result, Replay, Recordings };
const char* screen_name(Screen s);

struct LaunchOptions {
    std::filesystem::path data_dir;      // the Soldier Front client's data folder
    std::filesystem::path content_dir;   // Content: fidelity/ (the Fidelity look's own art)
    eng::Api api = eng::Api::Default;
    bool d3d_debug = false, warp = false;
    std::string angle_backend = "opengl";
    int width = 0, height = 0;           // 0: the settings'
    std::string renderer_note;           // "DirectX 12 could not start" -> a toast once up
    bool restarted = false;              // the game started itself again (a new renderer): say what draws now
    bool own_settings = false;           // --settings: this run keeps its own settings.cfg, and may write it
    // Tests: the picture as it would be with the whole screen, inside the test's window: its own
    // size ("1024x768"), and bars instead of the stretch.
    int picture_width = 0, picture_height = 0;
    bool picture_bars = false;
    bool pad_test = false;               // the autotest plays a stretch of the match on a controller (fed, not plugged in)
    bool quick = false;                  // the autotest stops after the match's first pictures
    bool modes = false;                  // the autotest plays a room of every game type with bots instead (pictures M*)
    bool movement = false;               // the autotest climbs a ladder up and down and runs a flight of stairs instead (pictures V*)
    bool killmarks = false;              // the autotest shows every kill mark in a match of its own instead (pictures K*)
    bool social = false;                 // the autotest tours friends, messages, chat and the clan on a staged server instead (pictures S*)
    bool shop = false;                   // the autotest tours the shops, sprays, capsules, the staff's Shop and your recordings instead (pictures P*)
    bool packs = false;       // the autotest plays the joined server's own map, gun and character (--server, §11)
    bool wear = false;                   // the autotest tours the guns' wear instead: the inventory's bars, Repair, Sell, the room's (pictures W*)
    bool knife = false;                  // the autotest swings the melee weapon in hand, seen from the eye and from outside (pictures K*)
    bool throws = false;                 // the autotest carries three throwables and takes each out with the fourth key, the switch bar photographed (pictures T*)
    bool games = false;                  // the autotest switches game types and a map off in the staff panel's Games tab, then opens Make Room (pictures G*)
    bool cannon = false;                 // the autotest mans and fires one of the Pirate Ship's cannons instead (pictures C*)
    // The autotest's first match picture from a fixed place (your side's spawn of this number,
    // facing along `spot_yaw`), so runs can be laid over each other: renderer against renderer,
    // one setting against another. -1: wherever the server put you.
    int spot = -1;
    bool stand = false;                  // --stand x,y,z,yaw,pitch: the match's pictures from this place, looking this way
    float stand_at[5] = {0, 0, 0, 0, 0};
    float spot_yaw = 0;
    float spot_back = 0;                 // stood this far (cm) back from the wall ahead instead
    bool spot_sun = false;               // facing the map's sun instead of `spot_yaw` (Fidelity's light shafts)
    bool spot_shadow = false;            // back to the sun, from behind and above: your soldier and his shadow
    bool spot_water = false;             // at the spawn that sees the map's water best, facing it
    // Tests and tools.
    std::string shot;                    // a PNG of the first screen that settles, then quit
    std::string autotest;                // a folder: walk every screen with a local server, photograph each
    std::string map;                     // straight into a practice match on this map (offline)
    bool join = false;                   // the autotest joins someone else's room instead of making one
    bool watch = false;                  // ...and in the match stands and photographs the others
    bool view_glow = false;              // the autotest's match photographs the map's glows (--map tunnel)
    int expect = 1;                      // the autotest's host starts once this many are in its room
    std::string daytime;                 // the autotest's room hour: "day", "night" (default: the map's bake)
    std::string screen;                  // open on this screen (servers, channels, ...) with a local server
    std::string user, pass;              // sign in as (tests)
    std::string server;                  // host[:port] to join (tests)
    std::string tvas;                    // Team Vanilla's account service, when not the settings' (tests: one on this machine)
    std::string weapon;                  // the autotest carries this gun into the match (item code: WRDC01)
    std::string pose;                    // the clip the menus' soldiers stand in (trying one out)
    int shot_frames = 90;
};

inline constexpr int kRunDeviceFailed = 3;

class App {
public:
    explicit App(eng::Api api);
    ~App();
    int run(const LaunchOptions& opts);

    // ── Services the screens use ────────────────────────────────────────────────
    eng::Device& device() { return device_; }
    eng::Window& window() { return window_; }
    eng::UiLayer& ui() { return ui_; }
    eng::audio::Mixer& mixer() { return mixer_; }
    Sounds& sounds() { return sounds_; }
    Settings& settings() { return settings_; }
    // Writes settings.cfg. A test run leaves the player's file alone (it writes only one of its own).
    void save_settings();
    // The window made to match the settings (its mode, its size, vertical sync).
    void apply_display();
    // Starts the game again and closes this one: a renderer is chosen when the game starts.
    void restart();
    // The sizes the options offer: for a window (those that fit on the screen) or for the whole
    // screen (its modes and the usual ones, up to its own size), of one shape or of any.
    std::vector<eng::ScreenSize> resolutions(bool whole_screen, int aspect) const;
    // The picture: its size in its own pixels, its shape, and where it sits in the window.
    int picture_width() const { return picture_w_; }
    int picture_height() const { return picture_h_; }
    float picture_aspect() const { return picture_h_ > 0 ? float(picture_w_) / float(picture_h_) : 16.0f / 9.0f; }
    FramePipe& frame_pipe() { return pipe_; }
    // The controller, whether or not it is being played with (the options show what is plugged in).
    eng::Gamepad& pad() { return pad_; }
    // The controller in hand: plugged in, switched on in the options, and this window in front.
    const eng::Gamepad* pad_in_hand() const { return pad_live_ ? &pad_ : nullptr; }
    // A shake in the hands (nothing without a pad, or with vibration off): strength 0..1, seconds.
    void rumble(float strength, float seconds);
    Session& session() { return session_; }
    sf::Data& data() { return data_; }
    ui::Atlas& atlas() { return atlas_; }
    const sf::ClanMarks& clan_marks() const { return clan_marks_; }
    // The forces' own 3D models, photographed for the menus.
    ModelStage& stage() { return *stage_; }
    // Steps each time a server's packs come or go: what was read from the data before is stale (MT-4).
    u32 content_epoch() const { return content_epoch_; }
    GameWorld* world() { return world_.get(); }
    ScreenState& state() { return *state_; }
    const LaunchOptions& options() const { return opts_; }
    double now() const { return now_; }
    float dt() const { return dt_; }
    float fps() const { return fps_; }

    // ── Flow ────────────────────────────────────────────────────────────────────
    Screen screen() const { return screen_; }
    void go(Screen s);
    double screen_time() const { return now_ - screen_since_; }
    void quit() { quit_ = true; }

    // ── Boot data ───────────────────────────────────────────────────────────────
    bool data_ready() const { return boot_done_; }
    float boot_progress() const { return boot_progress_; }
    std::string boot_status() const;
    const std::vector<sf::MapInfo>& map_infos() const { return map_infos_; }
    const std::vector<sf::LevelListing>& levels() const { return levels_; }
    const sf::MapInfo* map_info(std::string_view level_id) const;
    std::string map_title(std::string_view level_id) const;
    // The maps a game type is played on (Game/Modes.hpp mode_offers), by the names the lobby gives
    // them, the lobby's All Random and Hot Random first (MapName.txt 0 and 1). Each map's world
    // script is read once, the first time a list is asked for. The joined server's switched-off
    // maps are left out unless `switched_off_too`.
    std::vector<std::string> maps_for(Mode mode, bool switched_off_too = false);
    // Every map some game type is played on, by title (the Games tab's list).
    std::vector<std::string> every_map();
    // A map's Team Battle mission (its world script's objective and pieces), Elimination when it has none.
    Mission map_mission(std::string_view level_id);
    std::string notice_text() const { return notice_; }

    // ── Servers ─────────────────────────────────────────────────────────────────
    // The list (Docs/UniversalServerDeploy.md 8.4): "This PC" where the game can host (SL-9: not on
    // macOS or Android, PF-1), Team Vanilla's official servers, the community's, then the player's
    // own by address (SL-8).
    using ServerRow = ListedServer;
    std::vector<ServerRow> servers() const;
#if LSF_WITH_SERVER
    bool hosting() const { return local_server_ != nullptr; }
#else
    bool hosting() const { return false; }
#endif
    // Joins a server (5.1). "This PC": the server inside the game is started first, with the id
    // Team Vanilla issues this account for it.
    void connect(const ServerRow& row, const std::string& password = {});
    const ServerRow& server_row() const { return server_row_; }
    // The server about to be joined (its password is asked first).
    void pick_server(const ServerRow& row) { server_row_ = row; }
    // FR-2: this server left cleanly, the other joined like any other.
    void switch_server(const ServerRow& row);
    void add_direct_server(const ServerEntry& e);
    // Signs in to Team Vanilla (ID-11) from the sign-in window.
    void sign_in(bool create);

    // ── The match ───────────────────────────────────────────────────────────────
    void begin_match_load();       // from Session::match_load
    void leave_match();
    // A match recording (staff, Screens/Staff.cpp): watched on the Replay screen until left. A
    // match the client is called into ends it.
    bool watch_replay(std::vector<u8> file);
    void leave_replay();
    struct Watching {
        std::vector<std::vector<u8>> frames;
        std::vector<u32> times;      // each frame's milliseconds
        size_t next = 0;
        double ms = 0;
        float speed = 1;
        bool paused = false;
        bool complete = false;
        u32 match = 0;
        u64 started = 0;
        std::string map;
        Screen back = Screen::Lobby;
        bool mounted = false;        // its server's packs were mounted from the cache to watch it (MT-5)
    };
    Watching* watching() { return watching_ ? &*watching_ : nullptr; }
    u32 replay_length_ms() const { return watching_ && !watching_->times.empty() ? watching_->times.back() : 0; }
    void replay_seek(double ms);

    // Tests: a PNG of this frame, named.
    void shot(const std::string& name) { pending_shot_ = name; }

private:
    bool init(const LaunchOptions& opts);
    void frame();
    void layout();
    void pad_frame();
    void boot_thread();
    void handle_session();
    void start_local();
    void tour_shop_roll(int roll);
    // The session's packs mounted at a safe point (MT-2), or what the last server added forgotten (MT-3).
    bool mount_packs(const std::vector<std::filesystem::path>& packs, const eng::json::Value& manifest, const std::string& manifest_hash, std::string* why);
    void unmount_packs();
    void refresh_levels();
    void draw_screen();
    void autotest_step();
    // Tests: the fed controller's stick pushed toward a point on the page grid (true once the
    // pointer is on it), and its state handed to the pad.
    bool pad_point(float page_x, float page_y);
    void pad_feed();
    eng::Gamepad::Feed test_pad_;
    void capture(const std::string& path);

    std::unique_ptr<eng::Device> device_owner_;
    eng::Device& device_;
    eng::Window window_;
    eng::UiLayer ui_;
    eng::audio::Mixer mixer_;
    eng::Gamepad pad_;
    bool pad_live_ = false;
    // The pad working the menus: what it holds down for the interface, and where its pointer is
    // when there is no real one to move (a test).
    bool pad_click_ = false, pad_context_ = false, pad_escape_ = false, pad_start_ = false;
    float pad_x_ = -1, pad_y_ = -1;
    double rumble_until_ = -1;
    float rumble_ = 0, rumble_sent_ = 0;
    FramePipe pipe_;
    bool pipe_ok_ = false;
    int picture_w_ = 1600, picture_h_ = 900;
    bool automated_ = false;
    bool restarting_ = false;   // the next game is already starting: this one leaves settings.cfg to it
    Sounds sounds_;
    Settings settings_;
    Session session_;
    sf::Data data_;
    sf::ClanMarks clan_marks_;
    ui::Atlas atlas_;
    std::unique_ptr<ModelStage> stage_;
    std::unique_ptr<GameWorld> world_;
    std::unique_ptr<ScreenState> state_;
#if LSF_WITH_SERVER
    // PF-1: only where the game may host (Windows, Linux). The macOS and Android games hold no
    // server code at all.
    std::unique_ptr<lsfs::Server> local_server_;
#endif
    unsigned long long local_id_ = 0;   // this PC's own server, as Team Vanilla numbered it
    bool local_pending_ = false;
    LaunchOptions opts_;
    bool device_failed_ = false;

    Screen screen_ = Screen::Boot;
    double screen_since_ = 0;
    double now_ = 0, last_ = 0;
    float dt_ = 0, fps_ = 0;
    int frame_ = 0;
    bool quit_ = false;

    std::thread boot_;
    std::atomic<bool> boot_done_{false};
    std::atomic<float> boot_progress_{0};
    mutable std::mutex boot_mutex_;
    std::string boot_status_ = "Starting";
    bool atlas_uploaded_ = false;
    std::vector<sf::MapInfo> map_infos_;
    std::vector<MapRules> map_rules_;   // maps_for's, read once (a server's own maps' when it is joined)
    bool map_rules_stale_ = false;
    std::vector<sf::LevelListing> levels_;
    // The joined server's packs (Game/PackMount.hpp): where their files lie as archives for this
    // session, the client's own file names they are checked against, and a leaving still to be done
    // at the frame's safe point.
    std::filesystem::path mounted_;
    pack::BaseIndex base_names_;
    bool unmount_pending_ = false;
    std::atomic<u32> content_epoch_{0};
    std::string notice_;

    bool leave_pending_ = false;
    std::optional<Watching> watching_;
    void replay_tick();
    ServerRow server_row_;
    std::string pending_shot_;
    int autotest_stage_ = 0;
    double autotest_at_ = 0;
    bool autotest_fresh_ = true;   // the stage has not run a frame yet
    bool autotest_named_ = false, autotest_promoted_ = false;
    int shot_countdown_ = -1;
};

}  // namespace lsf
