// legacysf: Soldier Front Legacy for a Linux PC and for macOS (Apple Silicon). There is no
// launcher: this is the game. The same options as the Windows game's (Client/PC/main.cpp) but the
// renderer's: these draw with OpenGL ES (the system's on Linux, ANGLE over Metal on a Mac).
//
//   legacysf [--data <SF client data folder>] [--size WxH] [--shot out.png] [--autotest <folder>] [--map <id>]
//            [--user U --pass P] [--server host[:port]] [--tvas <address>] [--settings <file>] [--quick] ...
//
// The game data is found beside the game (Distro/Game/Linux/data; on a Mac, beside the .app), in
// the player's own folder (its data/), in settings.cfg (`data = ...`), or with --data. The player's
// own files are in the system's places (1.1): ~/.config/legacysf (Linux), ~/Library/Application
// Support/Soldier Front Legacy (macOS).
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Platform/System.hpp"
#include "Game/App.hpp"
#include "SF/Data.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#endif

namespace stdfs = std::filesystem;

namespace lsf {
std::filesystem::path user_directory();   // Game/App.cpp
}

namespace {

std::optional<stdfs::path> data_in(const stdfs::path& dir) {
    if (sf::Data::is_client_data(dir)) return dir;
    if (sf::Data::is_client_data(dir / "data")) return dir / "data";
    if (sf::Data::is_client_data(dir / "Client" / "data")) return dir / "Client" / "data";
    return std::nullopt;
}

#ifdef __APPLE__
// An app downloaded and opened where it was unzipped runs from a hidden copy (App Translocation),
// where nothing is beside it. The system knows where it really is (Security's
// SecTranslocateCreateOriginalPathForURL, found at run time): the folder the .app is in.
std::optional<stdfs::path> untranslocated_folder(const stdfs::path& exe) {
    const stdfs::path app = exe.parent_path().parent_path();   // .../X.app (exe: X.app/Contents/MacOS)
    if (app.string().find("/AppTranslocation/") == std::string::npos) return std::nullopt;
    void* security = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY | RTLD_LOCAL);
    if (!security) return std::nullopt;
    using Original = CFURLRef (*)(CFURLRef, CFErrorRef*);
    const auto original = reinterpret_cast<Original>(dlsym(security, "SecTranslocateCreateOriginalPathForURL"));
    if (!original) return std::nullopt;
    const std::string s = app.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(s.data()), CFIndex(s.size()), true);
    if (!url) return std::nullopt;
    std::optional<stdfs::path> out;
    if (CFURLRef real = original(url, nullptr)) {
        char buffer[4096];
        if (CFURLGetFileSystemRepresentation(real, true, reinterpret_cast<UInt8*>(buffer), sizeof(buffer))) out = stdfs::path(buffer).parent_path();
        CFRelease(real);
    }
    CFRelease(url);
    return out;
}
#endif

stdfs::path find_data(const lsf::Settings& settings) {
    const auto exe = eng::fs::executable_directory();
    if (!settings.data_dir.empty())
        if (auto d = data_in(settings.data_dir)) return *d;
    // Beside the game; on a Mac the program is three folders down in its .app, and the data is
    // beside the .app (DI-3: nothing is written into the app).
    for (const stdfs::path& c : {exe, exe.parent_path(), exe.parent_path().parent_path().parent_path(),
                                 exe.parent_path().parent_path().parent_path().parent_path()})
        if (auto d = data_in(c)) return *d;
#ifdef __APPLE__
    if (const auto folder = untranslocated_folder(exe)) {
        LOG_INFO("The game runs from a copy macOS made (App Translocation); its own folder is %s", folder->string().c_str());
        if (auto d = data_in(*folder)) return *d;
    }
#endif
    // The player's own folder: where the data goes when the game itself is moved (to Applications).
    if (auto d = data_in(lsf::user_directory())) return *d;
    return {};
}

stdfs::path find_content() {
    const auto exe = eng::fs::executable_directory();
    for (const stdfs::path& c : {exe / "Content", exe.parent_path() / "Resources" / "Content", exe.parent_path() / "Client" / "Content"})
        if (stdfs::exists(c / "fidelity")) return c;
    return exe / "Content";
}

}  // namespace

int main(int argc, char** argv) {
    lsf::LaunchOptions opts;
    bool console = false;
    std::error_code ec;
    const stdfs::path home = lsf::user_directory();
    stdfs::create_directories(home, ec);
    lsf::Settings::set_file(home / "settings.cfg");
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--data") opts.data_dir = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &opts.width, &opts.height);
        else if (a == "--shot") opts.shot = next();
        else if (a == "--shot-frames") opts.shot_frames = std::atoi(next().c_str());
        else if (a == "--autotest") opts.autotest = next();
        else if (a == "--map") opts.map = next();
        else if (a == "--daytime") opts.daytime = next();
        else if (a == "--join") opts.join = true;
        else if (a == "--watch") opts.watch = opts.join = true;
        else if (a == "--expect") opts.expect = std::max(1, std::atoi(next().c_str()));
        else if (a == "--user") opts.user = next();
        else if (a == "--pass") opts.pass = next();
        else if (a == "--server") opts.server = next();
        else if (a == "--tvas") opts.tvas = next();
        else if (a == "--pose") opts.pose = next();
        else if (a == "--weapon") opts.weapon = next();
        else if (a == "--settings") lsf::Settings::set_file(next()), opts.own_settings = true;
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
        else if (a == "--console") console = true;
        else if (a == "--help" || a == "-h") {
            std::printf("legacysf [--data <Soldier Front data folder>] [--size WxH] [--settings <file>] [--console]\n");
            return 0;
        }
    }
    // The log beside the player's settings (an automated run's, with its pictures).
    const stdfs::path log_dir = opts.autotest.empty() ? home : stdfs::path(opts.autotest);
    stdfs::create_directories(log_dir, ec);
    eng::log::init((log_dir / "game.log").string(), console);
    eng::install_crash_handler();
    eng::time::TimerResolution timer;
    LOG_INFO("Soldier Front Legacy, build %s", eng::build_id().c_str());

    lsf::Settings prefs;
    prefs.load();
    if (opts.data_dir.empty()) opts.data_dir = find_data(prefs);
    if (!sf::Data::is_client_data(opts.data_dir)) {
        eng::platform::fatal_message("Soldier Front Legacy",
                                     "The Soldier Front game data was not found (a data folder holding area, lobby, weapon, ...).\n"
                                     "Put the game's folder inside your Soldier Front folder, copy Soldier Front's data folder beside the game,\n"
                                     "or start it with --data <folder>.");
        eng::log::shutdown();
        return 1;
    }
    opts.content_dir = find_content();
    LOG_INFO("Soldier Front data: %s", opts.data_dir.string().c_str());
    LOG_INFO("Content: %s", opts.content_dir.string().c_str());
    opts.api = eng::Api::GLES;
    int rc = 1;
    {
        const auto app = std::make_unique<lsf::App>(eng::Api::GLES);
        rc = app->run(opts);
    }
    if (rc == lsf::kRunDeviceFailed) {
        eng::platform::fatal_message("Soldier Front Legacy", "OpenGL ES 3 could not start on this graphics card (see game.log).");
        rc = 1;
    }
    eng::log::shutdown();
    return rc;
}
