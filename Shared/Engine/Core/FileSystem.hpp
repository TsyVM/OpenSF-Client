// Plain files, and a small virtual file system over loose folders (the rewrite's own content).
// Paths are looked up normalised ("UI\Brand\Mark.png" -> "ui/brand/mark.png"); the most
// recently mounted folder wins. Soldier Front's own archives are read by SF/Data, not here.
#pragma once

#include "Engine/Core/Types.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eng::fs {

// ── Plain OS files ─────────────────────────────────────────────────────────────
std::optional<std::vector<u8>> read_file(const std::filesystem::path& path);
std::optional<std::string> read_text_file(const std::filesystem::path& path);
bool write_file(const std::filesystem::path& path, const void* data, size_t size);
bool write_text_file(const std::filesystem::path& path, std::string_view text);
// Where the program's own files live: the exe's folder on Windows; on Android the app's files
// folder, given by the platform layer at start (set_executable_directory).
std::filesystem::path executable_directory();
void set_executable_directory(const std::filesystem::path& dir);

// An OS file open for reading at any offset. Thread-safe reads.
class NativeFile {
public:
    NativeFile() = default;
    ~NativeFile() { close(); }
    NativeFile(const NativeFile&) = delete;
    NativeFile& operator=(const NativeFile&) = delete;
    bool open(const std::filesystem::path& path);
    void close();
    bool is_open() const { return handle_ != kNone; }
    u64 size() const { return size_; }
    size_t read_at(u64 offset, void* out, size_t bytes);

private:
    static constexpr intptr_t kNone = -1;
    intptr_t handle_ = kNone;   // a HANDLE on Windows, a file descriptor elsewhere
    u64 size_ = 0;
    std::mutex mutex_;          // Windows: the file pointer is shared
};

// A read-only window onto one file, loose or archived, for callers that stream a large
// member (music, dialogue, movies) rather than loading it whole. Thread-safe reads.
class Stream {
public:
    virtual ~Stream() = default;
    virtual u64 size() const = 0;
    // Reads up to `bytes` at `offset` within the member. Returns the count read.
    virtual size_t read_at(u64 offset, void* out, size_t bytes) = 0;
};

// ── Virtual content ────────────────────────────────────────────────────────────
bool mount_directory(const std::filesystem::path& directory);
void unmount_all();

std::optional<std::vector<u8>> read(std::string_view virtual_path);
std::optional<std::string> read_text(std::string_view virtual_path);
std::unique_ptr<Stream> open(std::string_view virtual_path);
bool exists(std::string_view virtual_path);
// Normalised paths starting with `prefix` and ending with `suffix`, sorted, deduplicated.
std::vector<std::string> list(std::string_view prefix, std::string_view suffix = {});

// "Art\\Cars\\X.P3D" -> "art/cars/x.p3d": the key every mount is indexed by.
std::string normalize(std::string_view path);

}  // namespace eng::fs
