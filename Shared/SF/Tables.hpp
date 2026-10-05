// The client's script tables (data/scr/scr_001.sff: Weapon.kst, ItemsListInfo.kst, force.kst, ...).
// A .kst is an Excel 97 workbook (an OLE2 compound file holding a BIFF8 "Workbook" stream),
// XORed with the repeating key 58 c8 b8 68 d8 48 38 e8 and then reversed end to end. Row 0 of
// its first sheet names the columns (Weapon.kst: CODE, NAME, BHW, ATTACK_SOUND_NAME, ...).
#pragma once

#include "SF/Data.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct Sheet {
    std::vector<std::string> header;             // row 0
    std::vector<std::vector<std::string>> rows;  // the rest; numbers written as the shortest decimal
    // The column titled `name` (exactly, then ignoring case), or -1.
    int column(std::string_view name) const;
    const std::string& cell(size_t row, int column) const;   // "" out of range
    float number(size_t row, int column, float fallback = 0) const;
};

// A .kst's bytes as the workbook they hide.
std::vector<u8> decode_kst(std::span<const std::byte> bytes);
// The first worksheet of an .xls workbook.
std::optional<Sheet> read_xls(std::span<const u8> xls, std::string* error = nullptr);
// A .kst from the script archives by key ("weapon.kst"), decoded and read.
std::optional<Sheet> load_kst(const Data& data, std::string_view key, std::string* error = nullptr);

}  // namespace sf
