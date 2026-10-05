// packres <out.pack> <file>...: the files, each compressed (Windows' LZMS), into one pack the
// game carries as a resource and unpacks at run time (Engine/Core/Embedded).
//
// Layout: "SHRPACK1", u32 count, then per file u32 name length, the name (UTF-8, no path),
// u64 size, u64 packed size, u64 FNV-1a of the file; then the packed files in that order.
#include <windows.h>
#include <compressapi.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "Cabinet.lib")

namespace {

uint64_t fnv1a(const std::vector<unsigned char>& d) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : d) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

template <class T>
void put(std::vector<unsigned char>& out, T v) {
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: packres <out.pack> <file>...\n");
        return 2;
    }
    COMPRESSOR_HANDLE c = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &c)) {
        std::fprintf(stderr, "packres: no LZMS compressor (%lu)\n", GetLastError());
        return 1;
    }
    std::vector<unsigned char> head, body;
    head.insert(head.end(), {'S', 'H', 'R', 'P', 'A', 'C', 'K', '1'});
    put<uint32_t>(head, uint32_t(argc - 2));
    size_t raw_total = 0;
    for (int i = 2; i < argc; ++i) {
        const std::filesystem::path path = std::filesystem::path(argv[i]);
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "packres: cannot read %s\n", argv[i]);
            return 1;
        }
        std::vector<unsigned char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        SIZE_T need = 0;
        Compress(c, raw.data(), raw.size(), nullptr, 0, &need);
        std::vector<unsigned char> packed(need);
        SIZE_T got = 0;
        if (!Compress(c, raw.data(), raw.size(), packed.data(), packed.size(), &got)) {
            std::fprintf(stderr, "packres: compressing %s failed (%lu)\n", argv[i], GetLastError());
            return 1;
        }
        packed.resize(got);
        const std::u8string u8 = path.filename().u8string();
        const std::string name(u8.begin(), u8.end());
        put<uint32_t>(head, uint32_t(name.size()));
        head.insert(head.end(), name.begin(), name.end());
        put<uint64_t>(head, uint64_t(raw.size()));
        put<uint64_t>(head, uint64_t(packed.size()));
        put<uint64_t>(head, fnv1a(raw));
        body.insert(body.end(), packed.begin(), packed.end());
        raw_total += raw.size();
        std::printf("packres: %s %zu -> %zu bytes\n", name.c_str(), raw.size(), packed.size());
    }
    CloseCompressor(c);
    std::ofstream out(std::filesystem::path(argv[1]), std::ios::binary);
    out.write(reinterpret_cast<const char*>(head.data()), std::streamsize(head.size()));
    out.write(reinterpret_cast<const char*>(body.data()), std::streamsize(body.size()));
    if (!out) {
        std::fprintf(stderr, "packres: cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("packres: %d files, %zu -> %zu bytes\n", argc - 2, raw_total, head.size() + body.size());
    return 0;
}
