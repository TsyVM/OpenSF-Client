// Match recordings: so staff reading a report can watch what happened rather than take
// somebody's word for it (TacticalFPS's Game/Replay, Gameplan 04 §5.4). A recording is only ever
// watched; nothing in it is playable.
//
// A recording is the server's own match messages -- every snapshot and event the players are
// sent -- with the moment each went out, written from a seat that sees everyone. Two
// consequences:
//
//   * Watching one is the ordinary match world fed from a file instead of the session's queue
//     (GameWorld::handle_messages), so nothing about drawing a match is written twice.
//   * It is made on the server: the one copy a cheat cannot edit, and the one that exists
//     whether or not anybody thought to press record.
//
// Layout: the header (with the match's MatchLoad as `you = 0`, so the watcher knows the map, the
// mode and everyone in it), then blocks of frames, each compressed on its own (zlib, stb), so a
// match is written as it is played and a server that stops mid-match leaves every finished block
// readable. A frame is `u32 milliseconds since the start, u32 length, one protocol message`.
// A block is `u32 raw size, u32 packed size, packed bytes`; a raw size of 0 ends the file.
#pragma once

#include "Engine/Core/Types.hpp"

#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace lsf::replay {

inline constexpr eng::u32 kMagic = 0x5246534C;   // "LSFR"
inline constexpr eng::u32 kVersion = 2;       // 2: the server's packs named (MT-5); 1 is still read
inline constexpr const char* kExtension = ".replay";

struct Header {
    eng::u32 game_version = 0;   // the server's kProtocolVersion
    eng::u32 match = 0;
    eng::u64 started = 0;        // unix seconds
    std::string server;
    std::vector<eng::u8> load;   // proto::MatchLoad (you = 0), as encoded
    // MT-5: the packs the match was played with (Docs/UniversalServerDeploy.md §11): the manifest's
    // hash (empty: none) and each pack as "id:sha256". Watching needs exactly these.
    std::string manifest;
    std::vector<std::string> packs;
};

struct Frame {
    eng::u32 ms = 0;
    std::vector<eng::u8> bytes;   // one protocol message, as proto::encode made it
};

class Writer {
public:
    ~Writer() { close(); }
    bool open(const std::filesystem::path& file, const Header& header);
    void add(eng::u32 ms, std::span<const eng::u8> message);
    // Ends the block being built and writes it. The match does this every round and every half
    // minute, so a stopped server costs at most that much.
    void flush();
    // Writes what is buffered and the end marker. Safe to call twice.
    void close();
    bool is_open() const { return file_ != nullptr; }
    size_t bytes_written() const { return written_; }

private:
    std::FILE* file_ = nullptr;
    std::vector<eng::u8> block_;
    size_t written_ = 0;
};

// The header alone (the recordings list reads every file's).
bool read_header(const std::filesystem::path& file, Header& header);
// A whole recording. One cut short (a match still being played, a server that stopped) gives
// every complete block and `complete` false, rather than an error.
bool read(std::span<const eng::u8> file, Header& header, std::vector<Frame>& frames, bool& complete, std::string* error = nullptr);

}  // namespace lsf::replay
