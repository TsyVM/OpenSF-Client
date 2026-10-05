#pragma once

#include <functional>
#include <string>

namespace eng::log {

enum class Level { Debug, Info, Warn, Error };

// Opens `file_path` for appending (truncated first) and optionally mirrors to the console.
void init(const std::string& file_path, bool console);
void shutdown();

void write(Level level, const char* fmt, ...);

// Receives every line after it is written; used by the in-game console.
void set_listener(std::function<void(Level, const std::string&)> listener);

}  // namespace eng::log

#define LOG_DEBUG(...) ::eng::log::write(::eng::log::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::eng::log::write(::eng::log::Level::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::eng::log::write(::eng::log::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::eng::log::write(::eng::log::Level::Error, __VA_ARGS__)
