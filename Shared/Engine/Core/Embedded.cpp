#include "Engine/Core/Embedded.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <windows.h>
#include <compressapi.h>
#include <shlobj.h>

#include <cstring>
#include <fstream>
#include <vector>

#pragma comment(lib, "Cabinet.lib")

namespace eng::embedded {

namespace {

struct Entry {
    std::string name;
    uint64_t size = 0, packed = 0, hash = 0;
    const unsigned char* data = nullptr;
};

bool parse(const unsigned char* p, size_t n, std::vector<Entry>& out) {
    if (n < 12 || std::memcmp(p, "SHRPACK1", 8) != 0) return false;
    size_t at = 8;
    auto take = [&](void* dst, size_t bytes) {
        if (at + bytes > n) return false;
        std::memcpy(dst, p + at, bytes);
        at += bytes;
        return true;
    };
    uint32_t count = 0;
    if (!take(&count, 4)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        Entry e;
        uint32_t len = 0;
        if (!take(&len, 4) || at + len > n) return false;
        e.name.assign(reinterpret_cast<const char*>(p + at), len);
        at += len;
        if (!take(&e.size, 8) || !take(&e.packed, 8) || !take(&e.hash, 8)) return false;
        out.push_back(std::move(e));
    }
    for (Entry& e : out) {
        if (at + e.packed > n) return false;
        e.data = p + at;
        at += size_t(e.packed);
    }
    return true;
}

// Writes the pack's files into `dir`; true when every one is there at its size afterwards.
bool write_all(const std::vector<Entry>& entries, const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    DECOMPRESSOR_HANDLE d = nullptr;
    bool ok = true;
    for (const Entry& e : entries) {
        const auto file = dir / str::widen(e.name);
        if (std::filesystem::file_size(file, ec) == e.size && !ec) continue;
        if (!d && !CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &d)) return false;
        std::vector<unsigned char> raw(size_t(e.size));
        SIZE_T got = 0;
        if (!Decompress(d, e.data, SIZE_T(e.packed), raw.data(), raw.size(), &got) || got != e.size) {
            ok = false;
            break;
        }
        // Written beside, then moved into place: a half-written DLL is never loaded.
        const auto tmp = file.wstring() + L".part";
        {
            std::ofstream out(tmp, std::ios::binary);
            out.write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size()));
            if (!out) {
                ok = false;
                break;
            }
        }
        if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmp.c_str());
            ok = false;
            break;
        }
    }
    if (d) CloseDecompressor(d);
    return ok;
}

}  // namespace

std::filesystem::path unpack(const wchar_t* resource_name, const std::wstring& prefix) {
    HRSRC res = FindResourceW(nullptr, resource_name, MAKEINTRESOURCEW(10));   // RT_RCDATA
    if (!res) return {};
    HGLOBAL mem = LoadResource(nullptr, res);
    const auto* p = mem ? static_cast<const unsigned char*>(LockResource(mem)) : nullptr;
    const size_t n = SizeofResource(nullptr, res);
    std::vector<Entry> entries;
    if (!p || !parse(p, n, entries)) {
        LOG_WARN("Embedded: the %s pack is damaged", str::narrow(resource_name).c_str());
        return {};
    }
    // The folder's name from its files' contents.
    uint64_t h = 1469598103934665603ull;
    for (const Entry& e : entries) h = (h ^ e.hash) * 1099511628211ull;
    const std::wstring leaf = prefix + L"-" + str::widen(str::format("%08llx", (unsigned long long)(h & 0xFFFFFFFFull)));

    std::vector<std::filesystem::path> roots = {fs::executable_directory() / "runtime"};
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        roots.push_back(std::filesystem::path(local) / "TeamVanilla" / "LegacySF" / "runtime");
        CoTaskMemFree(local);
    }
    for (const auto& root : roots) {
        const auto dir = root / leaf;
        if (!write_all(entries, dir)) continue;
        // Folders an older exe unpacked: gone (skipped while something still holds them).
        std::error_code ec;
        for (const auto& old : std::filesystem::directory_iterator(root, ec)) {
            const std::wstring name = old.path().filename().wstring();
            if (old.is_directory() && name != leaf && name.rfind(prefix + L"-", 0) == 0) std::filesystem::remove_all(old.path(), ec);
        }
        LOG_INFO("Embedded: %s ready in %s (%zu files)", str::narrow(resource_name).c_str(), str::narrow(dir.wstring()).c_str(), entries.size());
        return dir;
    }
    LOG_WARN("Embedded: could not unpack %s anywhere", str::narrow(resource_name).c_str());
    return {};
}

}  // namespace eng::embedded
