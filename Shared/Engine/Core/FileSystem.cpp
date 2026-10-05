#include "Engine/Core/FileSystem.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>

#include <climits>
#include <cstdlib>
#endif

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
#include <unordered_map>

namespace eng::fs {

namespace stdfs = std::filesystem;

std::string normalize(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') c = '/';
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        out.push_back(c);
    }
    for (;;) {
        if (out.starts_with("/")) out.erase(0, 1);
        else if (out.starts_with("./")) out.erase(0, 2);
        else break;
    }
    return out;
}

// -- NativeFile --

#ifdef _WIN32
bool NativeFile::open(const stdfs::path& path) {
    close();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER s{};
    GetFileSizeEx(h, &s);
    size_ = u64(s.QuadPart);
    handle_ = intptr_t(h);
    return true;
}

void NativeFile::close() {
    if (handle_ != kNone) CloseHandle(HANDLE(handle_));
    handle_ = kNone;
    size_ = 0;
}

size_t NativeFile::read_at(u64 offset, void* out, size_t bytes) {
    std::lock_guard lock(mutex_);
    if (handle_ == kNone || offset >= size_) return 0;
    bytes = size_t(std::min<u64>(bytes, size_ - offset));
    LARGE_INTEGER pos;
    pos.QuadPart = LONGLONG(offset);
    if (!SetFilePointerEx(HANDLE(handle_), pos, nullptr, FILE_BEGIN)) return 0;
    size_t total = 0;
    u8* p = static_cast<u8*>(out);
    while (total < bytes) {
        DWORD chunk = DWORD(std::min<size_t>(bytes - total, 64u << 20));
        DWORD got = 0;
        if (!ReadFile(HANDLE(handle_), p + total, chunk, &got, nullptr) || got == 0) break;
        total += got;
    }
    return total;
}
#else
bool NativeFile::open(const stdfs::path& path) {
    close();
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    fstat(fd, &st);
    size_ = u64(st.st_size);
    handle_ = fd;
    return true;
}

void NativeFile::close() {
    if (handle_ != kNone) ::close(int(handle_));
    handle_ = kNone;
    size_ = 0;
}

size_t NativeFile::read_at(u64 offset, void* out, size_t bytes) {
    if (handle_ == kNone || offset >= size_) return 0;
    bytes = size_t(std::min<u64>(bytes, size_ - offset));
    size_t total = 0;
    u8* p = static_cast<u8*>(out);
    while (total < bytes) {
        const ssize_t got = pread(int(handle_), p + total, bytes - total, off_t(offset + total));
        if (got <= 0) break;
        total += size_t(got);
    }
    return total;
}
#endif

std::optional<std::vector<u8>> read_file(const stdfs::path& path) {
#ifndef _WIN32
    NativeFile f;
    if (!f.open(path)) return std::nullopt;
    std::vector<u8> data(size_t(f.size()));
    if (f.read_at(0, data.data(), data.size()) != data.size()) return std::nullopt;
    return data;
#else
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::vector<u8> data(size_t(size.QuadPart));
    size_t at = 0;
    bool ok = true;
    while (at < data.size()) {
        DWORD chunk = DWORD(std::min<size_t>(data.size() - at, 64 * 1024 * 1024));
        DWORD got = 0;
        if (!ReadFile(h, data.data() + at, chunk, &got, nullptr) || got == 0) {
            ok = false;
            break;
        }
        at += got;
    }
    CloseHandle(h);
    if (!ok) return std::nullopt;
    return data;
#endif
}

std::optional<std::string> read_text_file(const stdfs::path& path) {
    auto bytes = read_file(path);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

// Written whole beside the file, onto the disk, and only then put in its place: a crash, a power
// cut or a full disk part-way leaves the last good copy instead of half of one (written in place,
// accounts.cfg -- every player's account -- was cut short by any of them).
bool write_file(const stdfs::path& path, const void* data, size_t size) {
    std::error_code ec;
    if (path.has_parent_path()) stdfs::create_directories(path.parent_path(), ec);
    stdfs::path tmp = path;
    tmp += ".tmp";
#ifndef _WIN32
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = size == 0 || std::fwrite(data, 1, size, f) == size;
    ok = std::fflush(f) == 0 && fsync(fileno(f)) == 0 && ok;
    ok = std::fclose(f) == 0 && ok;
    if (ok) ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) std::remove(tmp.c_str());
    return ok;
#else
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const u8* p = static_cast<const u8*>(data);
    bool ok = true;
    while (size > 0) {
        DWORD chunk = DWORD(std::min<size_t>(size, 64 * 1024 * 1024));
        DWORD wrote = 0;
        if (!WriteFile(h, p, chunk, &wrote, nullptr) || wrote != chunk) {
            ok = false;
            break;
        }
        p += chunk;
        size -= chunk;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
#endif
}

bool write_text_file(const stdfs::path& path, std::string_view text) {
    return write_file(path, text.data(), text.size());
}

namespace {
stdfs::path g_exe_dir;
}

void set_executable_directory(const stdfs::path& dir) { g_exe_dir = dir; }

stdfs::path executable_directory() {
    if (!g_exe_dir.empty()) return g_exe_dir;
#ifdef _WIN32
    wchar_t buffer[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return stdfs::path(std::wstring(buffer, n)).parent_path();
#elif defined(__APPLE__)
    // No /proc on a Mac: dyld names the program (Contents/MacOS inside the .app), with links resolved.
    char buffer[PATH_MAX * 2];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        char real[PATH_MAX];
        if (realpath(buffer, real)) return stdfs::path(real).parent_path();
        return stdfs::path(buffer).parent_path();
    }
    return stdfs::current_path();
#else
    // DS-3: the binary's real folder, so a service started from / never writes its files in /.
    std::error_code ec;
    const stdfs::path self = stdfs::read_symlink("/proc/self/exe", ec);
    if (!ec && !self.empty()) return self.parent_path();
    return stdfs::current_path();
#endif
}

namespace {

// A loose folder is indexed once at mount: the retail tree is a few thousand files, and an
// index both makes lookups case-insensitive (the scripts spell paths in any case) and avoids a
// failed CreateFile per miss.
struct Mount {
    stdfs::path directory;
    std::unordered_map<std::string, stdfs::path> loose;
};

std::mutex g_mutex;
std::vector<Mount> g_mounts;

class LooseStream final : public Stream {
public:
    u64 size() const override { return file.size(); }
    size_t read_at(u64 offset, void* out, size_t bytes) override { return file.read_at(offset, out, bytes); }
    NativeFile file;
};

}  // namespace

bool mount_directory(const stdfs::path& directory) {
    std::error_code ec;
    if (!stdfs::is_directory(directory, ec)) return false;
    Mount m;
    m.directory = stdfs::absolute(directory, ec);
    for (auto it = stdfs::recursive_directory_iterator(m.directory, ec); !ec && it != stdfs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        std::string rel = normalize(str::narrow(stdfs::relative(it->path(), m.directory, ec).wstring()));
        m.loose.emplace(std::move(rel), it->path());
    }
    LOG_INFO("Mounted folder %s (%zu files)", str::narrow(directory.wstring()).c_str(), m.loose.size());
    std::lock_guard lock(g_mutex);
    g_mounts.push_back(std::move(m));
    return true;
}

void unmount_all() {
    std::lock_guard lock(g_mutex);
    g_mounts.clear();
}

std::optional<std::vector<u8>> read(std::string_view virtual_path) {
    std::string key = normalize(virtual_path);
    stdfs::path loose;
    {
        std::lock_guard lock(g_mutex);
        for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it)
            if (auto f = it->loose.find(key); f != it->loose.end()) {
                loose = f->second;
                break;
            }
    }
    if (!loose.empty()) return read_file(loose);
    return std::nullopt;
}

std::optional<std::string> read_text(std::string_view virtual_path) {
    auto bytes = read(virtual_path);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

std::unique_ptr<Stream> open(std::string_view virtual_path) {
    std::string key = normalize(virtual_path);
    std::lock_guard lock(g_mutex);
    for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it)
        if (auto f = it->loose.find(key); f != it->loose.end()) {
            auto s = std::make_unique<LooseStream>();
            if (s->file.open(f->second)) return s;
        }
    return nullptr;
}

bool exists(std::string_view virtual_path) {
    std::string key = normalize(virtual_path);
    std::lock_guard lock(g_mutex);
    for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it)
        if (it->loose.contains(key)) return true;
    return false;
}

std::vector<std::string> list(std::string_view prefix, std::string_view suffix) {
    std::string pre = normalize(prefix);
    std::string suf = str::lower(suffix);
    std::set<std::string> found;
    std::lock_guard lock(g_mutex);
    for (const Mount& m : g_mounts) {
        for (const auto& [rel, _] : m.loose)
            if (rel.starts_with(pre) && rel.ends_with(suf)) found.insert(rel);
    }
    return {found.begin(), found.end()};
}

}  // namespace eng::fs
