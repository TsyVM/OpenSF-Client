// SPDX-License-Identifier: MIT
#include "sf1/data/cfg.hpp"
#include <cctype>

namespace sf1::data::cfg {

Result<std::vector<Binding>> parse_controls(std::string_view text) noexcept {
    std::vector<Binding> out;
    std::string_view directive = "*setAcontrol\"";
    std::size_t i = 0;
    while (i < text.size()) {
        // Skip to next line or directive.
        auto pos = text.find(directive, i);
        if (pos == std::string_view::npos) break;
        auto key_start = pos + directive.size();
        auto key_end = text.find('"', key_start);
        if (key_end == std::string_view::npos) break;
        auto act_start = text.find_first_not_of(" \t", key_end + 1);
        if (act_start == std::string_view::npos) break;
        auto act_end = text.find_first_of(" \t\r\n", act_start);
        if (act_end == std::string_view::npos) act_end = text.size();
        out.push_back(Binding{
            std::string(text.substr(key_start, key_end - key_start)),
            std::string(text.substr(act_start, act_end - act_start)),
        });
        i = act_end;
    }
    return out;
}

}  // namespace sf1::data::cfg
