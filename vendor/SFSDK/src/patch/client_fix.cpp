// SPDX-License-Identifier: MIT
#include "sf1/patch/client_fix.hpp"

namespace sf1::patch {

// The actual byte address lives in data/patterns.csv (build @3c7699). The
// concrete patch is applied by the extractor-generated table; this stub keeps
// the API green until patterns.csv is populated.
Result<runtime::Patch> unlock_framerate(std::uint32_t /*target*/) noexcept {
    return err(Error::NotYetVerified);
}
Result<runtime::Patch> widescreen(std::uint32_t, std::uint32_t) noexcept {
    return err(Error::NotYetVerified);
}
Result<runtime::Patch> fix_ping_display() noexcept {
    return err(Error::NotYetVerified);
}

}  // namespace sf1::patch
