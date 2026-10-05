// Soldier Front Legacy for Android: the app's native side (GameActivity, a NativeActivity).
//
// The app's own folder (Android/data/org.teamvanilla.legacysf/files) holds what the game's folder
// does on Windows: settings.cfg, game.log, replays/, and data/, the Soldier Front game data the
// setup screen (SetupActivity) copied out of the app's package on the first start. The launch
// intent's "args" extra takes some of the PC game's options (tests: adb shell am start ... --es
// args "--autotest autotest --quick"); relative paths start in the app's folder.
//
// The phone holds no server of any kind (PF-1): this library is built without LSF_WITH_SERVER.
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Net/Https.hpp"
#include "Engine/Platform/Android.hpp"
#include "Engine/Platform/System.hpp"
#include "Game/App.hpp"
#include "Game/Settings.hpp"
#include "SF/Data.hpp"

#include <android/configuration.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace {

// The launch intent's "args" extra (empty when started from the launcher).
std::string intent_args(android_app* app) {
    JNIEnv* env = eng::android::jni();
    if (!env) return {};
    std::string out;
    jobject activity = app->activity->clazz;
    jclass ac = env->GetObjectClass(activity);
    jmethodID get_intent = env->GetMethodID(ac, "getIntent", "()Landroid/content/Intent;");
    jobject intent = get_intent ? env->CallObjectMethod(activity, get_intent) : nullptr;
    if (intent) {
        jclass ic = env->GetObjectClass(intent);
        jmethodID get_extra = env->GetMethodID(ic, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");
        jstring key = env->NewStringUTF("args");
        auto value = get_extra ? static_cast<jstring>(env->CallObjectMethod(intent, get_extra, key)) : nullptr;
        if (value) {
            const char* c = env->GetStringUTFChars(value, nullptr);
            out = c;
            env->ReleaseStringUTFChars(value, c);
            env->DeleteLocalRef(value);
        }
        env->DeleteLocalRef(key);
        env->DeleteLocalRef(ic);
        env->DeleteLocalRef(intent);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(ac);
    return out;
}

// The options a phone can take: the tests' and the data's.
void parse(const std::string& args, lsf::LaunchOptions& opts) {
    std::istringstream in(args);
    std::vector<std::string> v;
    for (std::string s; in >> s;) v.push_back(s);
    for (size_t i = 0; i < v.size(); ++i) {
        const std::string& a = v[i];
        auto next = [&]() { return i + 1 < v.size() ? v[++i] : std::string(); };
        if (a == "--data") opts.data_dir = next();
        else if (a == "--shot") opts.shot = next();
        else if (a == "--autotest") opts.autotest = next();
        else if (a == "--quick") opts.quick = true;
        else if (a == "--map") opts.map = next();
        else if (a == "--server") opts.server = next();
        else if (a == "--tvas") opts.tvas = next();
        else if (a == "--user") opts.user = next();
        else if (a == "--pass") opts.pass = next();
        else if (a == "--pad-test") opts.pad_test = true;
        else if (a == "--settings") lsf::Settings::set_file(next()), opts.own_settings = true;
        // The desktop game's tours too (Client/Desktop/main.cpp), on the phone's screen.
        else if (a == "--modes") opts.modes = true;
        else if (a == "--movement") opts.movement = true;
        else if (a == "--social") opts.social = true;
        else if (a == "--killmarks") opts.killmarks = true;
        else if (a == "--shop") opts.shop = true;
        else if (a == "--packs") opts.packs = true;
        else if (a == "--wear") opts.wear = true;
        else if (a == "--join") opts.join = true;
        else if (a == "--watch") opts.watch = opts.join = true;
        else if (a == "--weapon") opts.weapon = next();
        else if (a == "--daytime") opts.daytime = next();
        else if (a == "--shot-frames") opts.shot_frames = std::atoi(next().c_str());
    }
}

}  // namespace

extern "C" void android_main(android_app* app) {
    eng::android::set_app(app);
    const char* where = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
    const std::filesystem::path files = where ? where : "/sdcard";
    std::error_code ec;
    std::filesystem::create_directories(files, ec);
    chdir(files.c_str());
    eng::fs::set_executable_directory(files);

    lsf::LaunchOptions opts;
    opts.data_dir = files / "data";
    opts.content_dir = files / "Content";
    opts.api = eng::Api::GLES;
    parse(intent_args(app), opts);

    const std::filesystem::path log_dir = opts.autotest.empty() ? files : files / opts.autotest;
    std::filesystem::create_directories(log_dir, ec);
    eng::log::init((log_dir / "game.log").string(), false);
    eng::install_crash_handler();
    LOG_INFO("Soldier Front Legacy (Android), build %s", eng::build_id().c_str());

    char country[3] = {};
    AConfiguration_getCountry(app->config, country);
    eng::platform::set_country(country);
    // Team Vanilla's account service is reached through the activity's Java (the phone's HTTPS).
    if (eng::android::jni()) eng::net::android_web_init(app->activity->vm, app->activity->clazz);

    LOG_INFO("Soldier Front data: %s", opts.data_dir.string().c_str());
    if (!sf::Data::is_client_data(opts.data_dir)) {
        // Nothing to play yet: back to the setup screen, which copies the data out of the package.
        LOG_WARN("No Soldier Front data in %s: back to setup", opts.data_dir.string().c_str());
        eng::android::call_activity("returnToSetup");
    } else {
        const auto game = std::make_unique<lsf::App>(eng::Api::GLES);
        const int rc = game->run(opts);
        LOG_INFO("The game ended (%d)", rc);
    }
    eng::log::shutdown();
    // Out of the activity; the app glue wants its events handled until the system destroys it.
    ANativeActivity_finish(app->activity);
    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0 && source) source->process(app, source);
    }
    // A NativeActivity's process lives on; the next start must begin clean.
    std::exit(0);
}
