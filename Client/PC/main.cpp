// legacysf.exe: Soldier Front Legacy for Windows. There is no launcher: this is the game.
//
//   legacysf.exe [--data <SF client data folder>] [--d3d12 | --d3d11 | --opengl] [--d3ddebug] [--warp]
//                [--size WxH] [--shot out.png] [--autotest <folder>] [--map <id>] [--user U --pass P]
//                [--server host[:port]] [--pose <character clip>] [--settings <file>]
//                [--picture WxH [--bars]] [--pad-test] [--quick] [--spot N [--spot-yaw D | --spot-sun] [--spot-wall CM]]
//                [--modes | --movement | --social | --killmarks | --shop]
//
// --settings names a settings.cfg of the run's own (a test's: the player's is never written by a
// test). --picture draws the match as it would be with the whole screen at that size, inside the
// window, pulled over it or (--bars) kept in shape. --pad-test has the autotest play on a controller.
// --social has it tour friends, messages, chat and the clan on a staged server
// (`socialcheck --stage`, then `--server 127.0.0.1:27301`). --shop tours the shops on this PC's own
// server: rentals, sprays, the capsule machine, the staff panel's Shop, a match with a spray, and
// the match's recording kept.
//
// The renderer is the player's (Options, `renderer` in settings.cfg): DirectX 12 by default,
// DirectX 11 or OpenGL; a flag overrides it for one run. One that cannot start on the card hands
// over to the next (12, 11, OpenGL) and the game says so once it is up.
//
// The data is the player's own Soldier Front client: its data\ folder (area, weapon, force, lobby,
// menu, ...), read in place. legacysf.exe finds it beside itself (drop it in the client's folder),
// in settings.cfg (`data = ...`), with --data, or asks the first time.
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/Embedded.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"
#include "Game/App.hpp"
#include "SF/Data.hpp"

#include <filesystem>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace {

namespace stdfs = std::filesystem;

std::optional<stdfs::path> data_in(const stdfs::path& dir) {
    if (sf::Data::is_client_data(dir)) return dir;
    if (sf::Data::is_client_data(dir / "data")) return dir / "data";
    if (sf::Data::is_client_data(dir / "Client" / "data")) return dir / "Client" / "data";
    return std::nullopt;
}

stdfs::path find_data(const lsf::Settings& settings) {
    const auto exe = eng::fs::executable_directory();
    if (!settings.data_dir.empty())
        if (auto d = data_in(eng::str::widen(settings.data_dir))) return *d;
    for (const stdfs::path& c : {exe, exe.parent_path(), exe.parent_path().parent_path() / "Soldier Front" / "SFClient"})
        if (auto d = data_in(c)) return *d;
    return {};
}

stdfs::path ask_for_data() {
    MessageBoxW(nullptr,
                L"Soldier Front Legacy plays from your own Soldier Front client.\n\n"
                L"Choose the folder Soldier Front is installed in (the one holding the data folder).",
                L"Soldier Front Legacy", MB_ICONINFORMATION);
    stdfs::path chosen;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    for (;;) {
        IFileOpenDialog* dlg = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) break;
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dlg->SetTitle(L"Where is Soldier Front installed?");
        stdfs::path pick;
        if (SUCCEEDED(dlg->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    pick = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
        if (pick.empty()) break;
        if (auto d = data_in(pick)) {
            chosen = *d;
            break;
        }
        MessageBoxW(nullptr, L"That folder does not hold Soldier Front's data (no area and lobby archives in it).", L"Soldier Front Legacy",
                    MB_ICONWARNING);
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return chosen;
}

// OpenGL goes through ANGLE's two DLLs (delay-loaded): beside the exe in angle/, the workspace's
// vendor copy, or the pair the exe carries, unpacked.
bool find_angle() {
    static int found = -1;
    if (found >= 0) return found == 1;
    const auto exe = eng::fs::executable_directory();
    for (const stdfs::path& dir : {exe / "angle", exe.parent_path() / "vendor" / "ANGLE" / "bin"})
        if (stdfs::exists(dir / "libGLESv2.dll") && stdfs::exists(dir / "libEGL.dll")) {
            SetDllDirectoryW(dir.c_str());
            return (found = 1) == 1;
        }
    if (const auto dir = eng::embedded::unpack(L"ANGLE_PACK", L"angle"); !dir.empty()) {
        SetDllDirectoryW(dir.c_str());
        return (found = 1) == 1;
    }
    found = 0;
    return false;
}

stdfs::path find_content() {
    const auto exe = eng::fs::executable_directory();
    for (const stdfs::path& c : {exe / "Content", exe.parent_path() / "Client" / "Content"})
        if (stdfs::exists(c / "fidelity")) return c;
    return exe / "Content";
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Real pixels on every screen, before anything asks Windows how big one is.
    eng::Window::use_real_pixels();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    lsf::LaunchOptions opts;
    std::optional<eng::Api> forced;
    bool console = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = eng::str::narrow(argv[i]);
        auto next = [&]() { return i + 1 < argc ? eng::str::narrow(argv[++i]) : std::string(); };
        if (a == "--data") opts.data_dir = eng::str::widen(next());
        else if (a == "--d3d12") forced = eng::Api::D3D12;
        else if (a == "--d3d11") forced = eng::Api::D3D11;
        else if (a == "--opengl" || a == "--gles") forced = eng::Api::GLES;
        else if (a == "--angle") opts.angle_backend = next();
        else if (a == "--d3ddebug") opts.d3d_debug = true;
        else if (a == "--warp") opts.warp = true;
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &opts.width, &opts.height);
        else if (a == "--shot") opts.shot = next();
        else if (a == "--shot-frames") opts.shot_frames = std::atoi(next().c_str());
        else if (a == "--autotest") opts.autotest = next();
        else if (a == "--map") opts.map = next();
        else if (a == "--daytime") opts.daytime = next();
        else if (a == "--join") opts.join = true;
        else if (a == "--watch") opts.watch = opts.join = true;
        else if (a == "--view-glow") opts.view_glow = true;
        else if (a == "--expect") opts.expect = std::max(1, std::atoi(next().c_str()));
        else if (a == "--user") opts.user = next();
        else if (a == "--pass") opts.pass = next();
        else if (a == "--server") opts.server = next();
        else if (a == "--tvas") opts.tvas = next();
        else if (a == "--pose") opts.pose = next();
        else if (a == "--weapon") opts.weapon = next();
        else if (a == "--settings") lsf::Settings::set_file(eng::str::widen(next())), opts.own_settings = true;
        else if (a == "--picture") std::sscanf(next().c_str(), "%dx%d", &opts.picture_width, &opts.picture_height);
        else if (a == "--bars") opts.picture_bars = true;
        else if (a == "--pad-test") opts.pad_test = true;
        else if (a == "--quick") opts.quick = true;
        else if (a == "--modes") opts.modes = true;
        else if (a == "--movement") opts.movement = true;
        else if (a == "--social") opts.social = true;
        else if (a == "--killmarks") opts.killmarks = true;
        else if (a == "--shop") opts.shop = true;
        else if (a == "--packs") opts.packs = true;
        else if (a == "--wear") opts.wear = true;
        else if (a == "--cannon") opts.cannon = true;
        else if (a == "--knife") opts.knife = true;
        else if (a == "--throws") opts.throws = true;
        else if (a == "--games") opts.games = true;
        else if (a == "--spot") opts.spot = std::atoi(next().c_str());
        else if (a == "--stand") opts.stand = std::sscanf(next().c_str(), "%f,%f,%f,%f,%f", &opts.stand_at[0], &opts.stand_at[1], &opts.stand_at[2], &opts.stand_at[3], &opts.stand_at[4]) >= 3;
        else if (a == "--spot-yaw") opts.spot_yaw = float(std::atof(next().c_str()));
        else if (a == "--spot-wall") opts.spot_back = float(std::atof(next().c_str()));
        else if (a == "--spot-sun") opts.spot_sun = true;
        else if (a == "--spot-shadow") opts.spot_shadow = true;
        else if (a == "--spot-water") opts.spot_water = true;
        else if (a == "--restarted") opts.restarted = true;
        else if (a == "--console") console = true;
    }
    LocalFree(argv);
    if (console && AllocConsole()) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
    }
    // An automated run keeps its log with its pictures, so two clients tested side by side do not share one.
    const std::filesystem::path log_dir = opts.autotest.empty() ? eng::fs::executable_directory() : std::filesystem::path(opts.autotest);
    if (!opts.autotest.empty()) std::filesystem::create_directories(log_dir);
    eng::log::init(eng::str::narrow((log_dir / "game.log").wstring()), console);
    eng::install_crash_handler();
    eng::time::TimerResolution timer;
    LOG_INFO("Soldier Front Legacy, build %s", eng::build_id().c_str());

    lsf::Settings prefs;
    prefs.load();
    const bool automated = !opts.shot.empty() || !opts.autotest.empty();
    if (opts.data_dir.empty()) opts.data_dir = find_data(prefs);
    if (!sf::Data::is_client_data(opts.data_dir)) {
        if (automated) return 1;
        opts.data_dir = ask_for_data();
        if (opts.data_dir.empty()) return 1;
        prefs.data_dir = eng::str::narrow(opts.data_dir.wstring());
        prefs.save();
    }
    opts.content_dir = find_content();
    LOG_INFO("Soldier Front data: %s", eng::str::narrow(opts.data_dir.wstring()).c_str());
    LOG_INFO("Content: %s", eng::str::narrow(opts.content_dir.wstring()).c_str());

    using R = lsf::Settings::Renderer;
    opts.api = prefs.renderer == R::D3D12 ? eng::Api::D3D12 : prefs.renderer == R::D3D11 ? eng::Api::D3D11 : prefs.renderer == R::OpenGL ? eng::Api::GLES : eng::Api::Default;
    if (forced) opts.api = *forced;
    std::vector<eng::Api> order = {opts.api};
    for (const eng::Api a : {eng::Api::D3D12, eng::Api::D3D11, eng::Api::GLES})
        if (a != opts.api && !(opts.api == eng::Api::Default && a == eng::Api::D3D12)) order.push_back(a);
    int rc = lsf::kRunDeviceFailed;
    for (const eng::Api api : order) {
        const char* name = eng::api_name(api == eng::Api::Default ? eng::Api::D3D12 : api);
        if (api == eng::Api::GLES && !find_angle()) {
            LOG_WARN("OpenGL: ANGLE's DLLs are not here");
            continue;
        }
        opts.api = api;
        const auto app = std::make_unique<lsf::App>(api);
        rc = app->run(opts);
        if (rc != lsf::kRunDeviceFailed) break;
        LOG_WARN("%s could not start on this card", name);
        if (opts.renderer_note.empty()) opts.renderer_note = std::string(name) + " could not start on this graphics card";
    }
    if (rc == lsf::kRunDeviceFailed) {
        if (!automated)
            MessageBoxW(nullptr, L"Soldier Front Legacy could not start DirectX 12, DirectX 11 or OpenGL on this graphics card.\nSee game.log for details.",
                        L"Soldier Front Legacy", MB_ICONERROR);
        rc = 1;
    }
    eng::log::shutdown();
    return rc;
}
