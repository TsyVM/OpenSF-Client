#include "Engine/Core/Log.hpp"

#ifdef _WIN32
#include <windows.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#endif

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace eng::log {

namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
bool g_console = false;
std::function<void(Level, const std::string&)> g_listener;

const char* tag(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info: return "info ";
        case Level::Warn: return "warn ";
        case Level::Error: return "error";
    }
    return "?";
}

}  // namespace

void init(const std::string& file_path, bool console) {
    std::lock_guard lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = file_path.empty() ? nullptr : std::fopen(file_path.c_str(), "w");
    g_console = console;
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
    g_listener = nullptr;
}

void set_listener(std::function<void(Level, const std::string&)> listener) {
    std::lock_guard lock(g_mutex);
    g_listener = std::move(listener);
}

void write(Level level, const char* fmt, ...) {
    char message[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char line[2200];
    std::snprintf(line, sizeof(line), "%02d:%02d:%02d [%s] %s\n", local.tm_hour, local.tm_min, local.tm_sec,
                  tag(level), message);

    std::function<void(Level, const std::string&)> listener;
    {
        std::lock_guard lock(g_mutex);
        if (g_file) {
            std::fputs(line, g_file);
            std::fflush(g_file);
        }
#ifdef _WIN32
        if (g_console) {
            HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
            WORD colour = level == Level::Error  ? (FOREGROUND_RED | FOREGROUND_INTENSITY)
                          : level == Level::Warn ? (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
                          : level == Level::Debug ? (FOREGROUND_BLUE | FOREGROUND_GREEN)
                                                  : (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            SetConsoleTextAttribute(out, colour);
            std::fputs(line, stdout);
            SetConsoleTextAttribute(out, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }
        OutputDebugStringA(line);
#elif defined(__ANDROID__)
        // Android's log (adb logcat -s SHAR).
        const int prio = level == Level::Error  ? ANDROID_LOG_ERROR
                         : level == Level::Warn ? ANDROID_LOG_WARN
                         : level == Level::Debug ? ANDROID_LOG_DEBUG
                                                 : ANDROID_LOG_INFO;
        __android_log_write(prio, "SHAR", message);
#endif
        listener = g_listener;
    }
    if (listener) listener(level, message);
}

}  // namespace eng::log
