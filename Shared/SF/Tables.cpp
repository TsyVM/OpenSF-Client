#include "SF/Tables.hpp"

#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace sf {

namespace {

constexpr u8 kKstKey[8] = {0x58, 0xc8, 0xb8, 0x68, 0xd8, 0x48, 0x38, 0xe8};
constexpr u8 kOle[8] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};

u16 rd16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
u32 rd32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }

bool fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}

// The named stream of an OLE2 compound file (regular sectors only: a workbook is never small).
bool ole_stream(std::span<const u8> f, std::u16string_view want, std::vector<u8>& out, std::string* error) {
    if (f.size() < 512 || std::memcmp(f.data(), kOle, 8) != 0) return fail(error, "not an OLE2 compound file");
    const u32 shift = rd16(&f[30]);
    if (shift < 9 || shift > 12) return fail(error, "unexpected sector size");
    const size_t ssz = size_t(1) << shift;
    const u32 n_fat = rd32(&f[44]), dir_start = rd32(&f[48]), mini_cutoff = rd32(&f[56]);
    u32 difat_next = rd32(&f[68]);
    const u32 n_difat = rd32(&f[72]);
    auto sector = [&](u32 s) -> const u8* {
        const size_t off = (size_t(s) + 1) * ssz;
        return off + ssz <= f.size() ? &f[off] : nullptr;
    };
    std::vector<u32> fat_sectors;
    for (int i = 0; i < 109; ++i) fat_sectors.push_back(rd32(&f[76 + i * 4]));
    for (u32 k = 0; k < n_difat && difat_next < 0xFFFFFFFA; ++k) {
        const u8* p = sector(difat_next);
        if (!p) return fail(error, "DIFAT out of the file");
        for (size_t i = 0; i + 1 < ssz / 4; ++i) fat_sectors.push_back(rd32(p + i * 4));
        difat_next = rd32(p + ssz - 4);
    }
    std::vector<u32> fat;
    for (u32 i = 0; i < n_fat && i < fat_sectors.size(); ++i) {
        const u8* p = sector(fat_sectors[i]);
        if (!p) return fail(error, "FAT out of the file");
        for (size_t k = 0; k < ssz / 4; ++k) fat.push_back(rd32(p + k * 4));
    }
    auto chain = [&](u32 start, std::vector<u8>& into) {
        into.clear();
        for (u32 s = start, n = 0; s < 0xFFFFFFFA && n <= fat.size(); ++n) {
            const u8* p = sector(s);
            if (!p || s >= fat.size()) return false;
            into.insert(into.end(), p, p + ssz);
            s = fat[s];
        }
        return true;
    };
    std::vector<u8> dir;
    if (!chain(dir_start, dir)) return fail(error, "bad directory chain");
    for (size_t e = 0; e + 128 <= dir.size(); e += 128) {
        const u16 name_bytes = rd16(&dir[e + 64]);
        std::u16string name;
        for (size_t i = 0; i + 2 <= name_bytes && i < 64; i += 2) {
            const char16_t c = char16_t(rd16(&dir[e + i]));
            if (c) name += c;
        }
        if (name != want) continue;
        const u32 start = rd32(&dir[e + 116]), size = rd32(&dir[e + 120]);
        if (size < mini_cutoff) return fail(error, "the stream sits in the mini stream");
        if (!chain(start, out) || out.size() < size) return fail(error, "bad stream chain");
        out.resize(size);
        return true;
    }
    return fail(error, "no such stream");
}

std::string number_text(double v) {
    if (v == std::floor(v) && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(v));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

double rk_value(u32 rk) {
    double v;
    if (rk & 2) {
        v = double(static_cast<std::int32_t>(rk) >> 2);
    } else {
        const u64 bits = u64(rk & 0xFFFFFFFCu) << 32;
        std::memcpy(&v, &bits, 8);
    }
    return (rk & 1) ? v / 100.0 : v;
}

// The shared string table, which CONTINUE records split wherever they like (a string's
// characters restate their width after each split).
std::vector<std::string> read_sst(const std::vector<std::span<const u8>>& parts) {
    std::vector<std::string> out;
    if (parts.empty() || parts[0].size() < 8) return out;
    size_t pi = 0, pos = 8;
    auto cur = [&]() -> std::span<const u8> { return parts[pi]; };
    auto next_part = [&]() {
        if (pi + 1 >= parts.size()) return false;
        ++pi;
        pos = 0;
        return true;
    };
    auto byte = [&](u8& b) {
        while (pos >= cur().size())
            if (!next_part()) return false;
        b = cur()[pos++];
        return true;
    };
    auto u16v = [&](u16& v) {
        u8 a, b;
        if (!byte(a) || !byte(b)) return false;
        v = u16(a | (b << 8));
        return true;
    };
    auto u32v = [&](u32& v) {
        u16 a, b;
        if (!u16v(a) || !u16v(b)) return false;
        v = u32(a) | (u32(b) << 16);
        return true;
    };
    const u32 unique = rd32(parts[0].data() + 4);
    for (u32 n = 0; n < unique; ++n) {
        u16 chars = 0;
        u8 flags = 0;
        if (!u16v(chars) || !byte(flags)) break;
        u16 runs = 0;
        u32 ext = 0;
        if ((flags & 0x08) && !u16v(runs)) break;
        if ((flags & 0x04) && !u32v(ext)) break;
        bool wide = flags & 1;
        std::u16string text;
        for (u32 left = chars; left > 0;) {
            if (pos >= cur().size()) {
                if (!next_part()) break;
                wide = cur()[0] & 1;
                pos = 1;
            }
            const size_t each = wide ? 2 : 1;
            const size_t take = std::min<size_t>(left, (cur().size() - pos) / each);
            if (take == 0) {
                pos = cur().size();
                continue;
            }
            for (size_t i = 0; i < take; ++i) text += wide ? char16_t(rd16(&cur()[pos + i * 2])) : char16_t(cur()[pos + i]);
            pos += take * each;
            left -= u32(take);
        }
        for (size_t skip = size_t(runs) * 4 + ext; skip > 0;) {
            if (pos >= cur().size() && !next_part()) break;
            const size_t t = std::min(skip, cur().size() - pos);
            pos += t;
            skip -= t;
        }
        // UTF-16 to UTF-8 (the tables are Latin text plus the odd Korean line).
        std::string utf8;
        for (size_t i = 0; i < text.size(); ++i) {
            u32 c = text[i];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size()) c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
            if (c < 0x80) utf8 += char(c);
            else if (c < 0x800) utf8 += char(0xC0 | (c >> 6)), utf8 += char(0x80 | (c & 0x3F));
            else if (c < 0x10000) utf8 += char(0xE0 | (c >> 12)), utf8 += char(0x80 | ((c >> 6) & 0x3F)), utf8 += char(0x80 | (c & 0x3F));
            else utf8 += char(0xF0 | (c >> 18)), utf8 += char(0x80 | ((c >> 12) & 0x3F)), utf8 += char(0x80 | ((c >> 6) & 0x3F)), utf8 += char(0x80 | (c & 0x3F));
        }
        out.push_back(std::move(utf8));
    }
    return out;
}

}  // namespace

int Sheet::column(std::string_view name) const {
    for (size_t i = 0; i < header.size(); ++i)
        if (header[i] == name) return int(i);
    const std::string want = lower(name);
    for (size_t i = 0; i < header.size(); ++i)
        if (lower(header[i]) == want) return int(i);
    return -1;
}

const std::string& Sheet::cell(size_t row, int column) const {
    static const std::string kEmpty;
    if (row >= rows.size() || column < 0 || size_t(column) >= rows[row].size()) return kEmpty;
    return rows[row][size_t(column)];
}

float Sheet::number(size_t row, int column, float fallback) const {
    const std::string& s = cell(row, column);
    if (s.empty()) return fallback;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return end && *end == '\0' ? v : fallback;
}

std::vector<u8> decode_kst(std::span<const std::byte> bytes) {
    std::vector<u8> out(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i) out[bytes.size() - 1 - i] = u8(std::to_integer<u8>(bytes[i]) ^ kKstKey[i % 8]);
    return out;
}

std::optional<Sheet> read_xls(std::span<const u8> xls, std::string* error) {
    std::vector<u8> book;
    if (!ole_stream(xls, u"Workbook", book, error) && !ole_stream(xls, u"Book", book, error)) return std::nullopt;
    struct Record {
        u16 id;
        std::span<const u8> body;
    };
    std::vector<Record> records;
    for (size_t pos = 0; pos + 4 <= book.size();) {
        const u16 id = rd16(&book[pos]), len = rd16(&book[pos + 2]);
        if (pos + 4 + len > book.size()) break;
        records.push_back({id, std::span<const u8>(book.data() + pos + 4, len)});
        pos += 4 + size_t(len);
    }
    std::vector<std::string> sst;
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].id == 0x00FC) {
            std::vector<std::span<const u8>> parts{records[i].body};
            for (size_t j = i + 1; j < records.size() && records[j].id == 0x003C; ++j) parts.push_back(records[j].body);
            sst = read_sst(parts);
            break;
        }
    // The first worksheet's cells.
    std::map<u32, std::map<u32, std::string>> cells;
    bool in_sheet = false, done = false;
    for (const Record& r : records) {
        if (done) break;
        const u8* b = r.body.data();
        const size_t n = r.body.size();
        switch (r.id) {
            case 0x0809:   // BOF: 0x10 is a worksheet
                if (n >= 4 && rd16(b + 2) == 0x0010) in_sheet = true;
                break;
            case 0x000A:   // EOF
                if (in_sheet) done = true;
                break;
            case 0x00FD:   // LABELSST
                if (in_sheet && n >= 10) {
                    const u32 idx = rd32(b + 6);
                    cells[rd16(b)][rd16(b + 2)] = idx < sst.size() ? sst[idx] : std::string();
                }
                break;
            case 0x0203:   // NUMBER
                if (in_sheet && n >= 14) {
                    double v;
                    std::memcpy(&v, b + 6, 8);
                    cells[rd16(b)][rd16(b + 2)] = number_text(v);
                }
                break;
            case 0x027E:   // RK
                if (in_sheet && n >= 10) cells[rd16(b)][rd16(b + 2)] = number_text(rk_value(rd32(b + 6)));
                break;
            case 0x00BD:   // MULRK
                if (in_sheet && n >= 6) {
                    const u16 row = rd16(b), first = rd16(b + 2);
                    for (size_t k = 0; 4 + k * 6 + 6 <= n - 2; ++k) cells[row][u32(first + k)] = number_text(rk_value(rd32(b + 4 + k * 6 + 2)));
                }
                break;
            case 0x0204:   // LABEL (old files)
                if (in_sheet && n >= 8) {
                    const u16 len = rd16(b + 6);
                    cells[rd16(b)][rd16(b + 2)] = std::string(reinterpret_cast<const char*>(b + 8), std::min<size_t>(len, n - 8));
                }
                break;
            default:
                break;
        }
    }
    if (cells.empty()) {
        if (error) *error = "no cells";
        return std::nullopt;
    }
    u32 width = 0;
    for (const auto& [r, row] : cells)
        if (!row.empty()) width = std::max(width, row.rbegin()->first + 1);
    Sheet sheet;
    bool first = true;
    for (const auto& [r, row] : cells) {
        std::vector<std::string> line(width);
        for (const auto& [c, v] : row) line[c] = v;
        if (first) sheet.header = std::move(line);
        else sheet.rows.push_back(std::move(line));
        first = false;
    }
    return sheet;
}

std::optional<Sheet> load_kst(const Data& data, std::string_view key, std::string* error) {
    auto bytes = data.read(Pack::Scr, key);
    if (!bytes) {
        if (error) *error = "not in the script archives";
        return std::nullopt;
    }
    const std::vector<u8> xls = decode_kst(*bytes);
    return read_xls(xls, error);
}

}  // namespace sf
