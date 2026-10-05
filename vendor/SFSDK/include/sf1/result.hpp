// SPDX-License-Identifier: MIT
// sf1/result.hpp — Result<T> = std::expected<T, Error> with SF1SDK's error enum.
//
// Never throws. Errors are values. This is the only failure mechanism the SDK uses.
#pragma once

#include <expected>
#include <string_view>
#include <cstdint>

namespace sf1 {

enum class Error : std::uint32_t {
    Ok = 0,

    // Data layer
    ShortRead,            // fewer bytes than a required header
    BadMagic,             // magic/tag mismatch
    HeaderOutOfRange,     // header claims a size past EOF
    NotVerified,          // format shape is only Reasoned, not Verified — refuse to parse
    UnsupportedVersion,   // a version field the SDK has not verified
    CountOutOfRange,      // a record count past the format's sane ceiling
    EntryOutOfRange,      // an archive entry points past the end of the file
    EntryNotFound,        // no archive entry has that name
    AmbiguousEntry,       // more than one entry matches a name; refuse to pick
    IndexOutOfRange,      // a triangle index past its vertex buffer
    NonFinite,            // NaN or infinity where geometry must be finite
    Malformed,            // a structural rule of the format is broken
    TrailingBytes,        // bytes left over after a format that must end exactly
    IoError,              // the file could not be opened or read

    // Runtime layer
    PatternNotFound,      // pattern scanner returned no hits
    PatternAmbiguous,     // >1 hit; refuse to pick
    ImageNotLoaded,       // Image::current() called before init
    NotYetVerified,       // symbol/address declared but no confirmed value in classes.csv

    // Hook layer
    HookInstallFailed,
    HookAlreadyInstalled,

    // Net layer
    UnknownOpcode,
    TruncatedPacket,
    CryptoNotDerived,     // encryption seed hasn't been captured yet
};

[[nodiscard]] constexpr std::string_view describe(Error e) noexcept {
    switch (e) {
        case Error::Ok:                  return "ok";
        case Error::ShortRead:           return "short read";
        case Error::BadMagic:            return "bad magic";
        case Error::HeaderOutOfRange:    return "header out of range";
        case Error::NotVerified:         return "format not yet verified (reasoned only)";
        case Error::UnsupportedVersion:  return "unsupported format version";
        case Error::CountOutOfRange:     return "record count out of range";
        case Error::EntryOutOfRange:     return "archive entry outside the file";
        case Error::EntryNotFound:       return "archive entry not found";
        case Error::AmbiguousEntry:      return "archive entry name is ambiguous";
        case Error::IndexOutOfRange:     return "index outside vertex buffer";
        case Error::NonFinite:           return "non-finite geometry";
        case Error::Malformed:           return "malformed record";
        case Error::TrailingBytes:       return "unexpected trailing bytes";
        case Error::IoError:             return "file could not be read";
        case Error::PatternNotFound:     return "pattern not found";
        case Error::PatternAmbiguous:    return "pattern ambiguous (>1 hit)";
        case Error::ImageNotLoaded:      return "image not loaded";
        case Error::NotYetVerified:      return "symbol not yet verified";
        case Error::HookInstallFailed:   return "hook install failed";
        case Error::HookAlreadyInstalled:return "hook already installed";
        case Error::UnknownOpcode:       return "unknown opcode";
        case Error::TruncatedPacket:     return "truncated packet";
        case Error::CryptoNotDerived:    return "crypto seed not yet derived";
    }
    return "unknown";
}

template <class T>
using Result = std::expected<T, Error>;

inline auto err(Error e) noexcept { return std::unexpected<Error>{e}; }

}  // namespace sf1
