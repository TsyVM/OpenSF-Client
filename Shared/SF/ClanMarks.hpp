// The clan mark builder's pieces: data/clan/clanmark_bg.dfz, clanmark_frame.dfz and
// clanmark_symbol.dfz. A .dfz is a plain zip of 128x128 32-bit TGAs named by number ("1.tga",
// "45.TGA", "1001.tga"); a clan's mark is one of each laid over the other (background, frame,
// symbol). The three clanmark1..3.dfz beside them are clans' own uploaded marks and are not read.
#pragma once

#include "Engine/Asset/ImageDecode.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sf {

enum class MarkLayer : eng::u8 { Background, Frame, Symbol, Count };

class ClanMarks {
public:
    // `client_data` is the client's data/ folder. False when none of the three is there.
    bool open(const std::filesystem::path& client_data);
    // How many pieces a layer offers (numbered 1..count; 0 is "none" for frame and symbol).
    int count(MarkLayer layer) const;
    // The piece `index` (1-based) of a layer, decoded: 128x128 RGBA.
    std::optional<eng::Image> image(MarkLayer layer, int index) const;

private:
    struct Entry {
        std::string name;
        int number = 0;
        eng::u32 method = 0, packed = 0, size = 0, offset = 0;
    };
    struct Archive {
        std::vector<eng::u8> bytes;
        std::vector<Entry> entries;   // by number
    };
    std::array<Archive, size_t(MarkLayer::Count)> layers_;
    mutable std::mutex mutex_;
};

// A zip's entries, stored or deflated, into memory. Null when the entry cannot be read.
std::optional<std::vector<eng::u8>> unzip_entry(const std::vector<eng::u8>& zip, eng::u32 method, eng::u32 offset, eng::u32 packed, eng::u32 size);

}  // namespace sf
