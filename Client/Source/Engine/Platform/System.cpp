#include "Engine/Platform/System.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>

#include <cstdio>
#endif
#if !defined(_WIN32) && !defined(__ANDROID__)
#include <spawn.h>
#include <sys/wait.h>

#include <cstdlib>
#include <initializer_list>
#include <utility>
#include <vector>

extern char** environ;
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace eng::platform {

namespace {
std::string g_country;
}

void set_country(const std::string& country) { g_country = str::upper(country); }

#ifdef _WIN32

size_t process_memory() {
    PROCESS_MEMORY_COUNTERS_EX pm{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof(pm));
    return size_t(pm.PrivateUsage);
}

void fatal_message(const char* title, const char* text, void* window) {
    MessageBoxW(HWND(window), str::widen(text).c_str(), str::widen(title).c_str(), MB_ICONERROR);
}

// Windows' "measurement system": 1 is U.S., 0 metric.
bool uses_mph() {
    DWORD measure = 0;
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&measure),
                        sizeof(measure) / sizeof(wchar_t)) == 0)
        return false;
    return measure == 1;
}

// A keyboard at hand: the game's own text fields take the typing.
bool text_input_native() { return false; }
void start_typing(const std::string&, int, bool) {}
void stop_typing() {}
bool typed_text(std::string&) { return false; }
bool text_answer(std::string&, bool&) { return false; }

#else

size_t process_memory() {
#ifdef __APPLE__
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return size_t(info.resident_size);
#else
    long pages = 0, resident = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        std::fclose(f);
    }
    return size_t(resident) * size_t(sysconf(_SC_PAGESIZE));
#endif
}

#ifdef __ANDROID__
void fatal_message(const char* title, const char* text, void*) { LOG_ERROR("%s: %s", title, text); }
#else

namespace {

// Runs a dialog program and waits for it; false when there is no such program.
bool show_with(std::initializer_list<std::string> args) {
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return false;
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return false;
    // 127 and 255: the program is there but could not show anything (no display, an unknown option).
    return WEXITSTATUS(status) != 127 && WEXITSTATUS(status) != 255;
}

std::string escaped(const char* text, std::initializer_list<std::pair<char, const char*>> table) {
    std::string out;
    for (const char* p = text; *p; ++p) {
        const char* sub = nullptr;
        for (const auto& [c, s] : table)
            if (*p == c) sub = s;
        if (sub) out += sub;
        else out += *p;
    }
    return out;
}

}  // namespace

// A player who double-clicked the game has no terminal: the message is also shown in a window.
void fatal_message(const char* title, const char* text, void*) {
    LOG_ERROR("%s: %s", title, text);
    std::fprintf(stderr, "%s: %s\n", title, text);
#ifdef __APPLE__
    const std::string script = "display alert \"" + escaped(title, {{'\\', "\\\\"}, {'"', "\\\""}}) + "\" message \"" +
                               escaped(text, {{'\\', "\\\\"}, {'"', "\\\""}}) + "\" as critical";
    show_with({"osascript", "-e", script});
#else
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) return;
    const std::string markup = escaped(text, {{'&', "&amp;"}, {'<', "&lt;"}, {'>', "&gt;"}});
    if (show_with({"zenity", "--error", "--title", title, "--text", markup})) return;
    if (show_with({"kdialog", "--title", title, "--error", text})) return;
    show_with({"xmessage", "-center", std::string(title) + "\n\n" + text});
#endif
}

#endif

// Road signs in miles: the U.S., Liberia and Myanmar (and the U.K.'s roads, but its phones say GB
// for metric everything else, as Windows does).
bool uses_mph() { return g_country == "US" || g_country == "LR" || g_country == "MM"; }

#endif

}  // namespace eng::platform
