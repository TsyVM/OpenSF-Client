#include "Engine/Core/Xml.hpp"

#include <cstdlib>
#include <cstring>

namespace eng::xml {

namespace {

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        const size_t semi = s.find(';', i);
        if (semi == std::string_view::npos) {
            out.push_back('&');
            continue;
        }
        const std::string_view ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (!ent.empty() && ent[0] == '#') out.push_back(char(std::strtol(std::string(ent.substr(1)).c_str(), nullptr, 10)));
        else out.append(s.substr(i, semi - i + 1));
        i = semi;
    }
    return out;
}

struct Parser {
    std::string_view s;
    size_t p = 0;
    std::string error;

    bool at_end() const { return p >= s.size(); }
    void skip_ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
    }
    // Skips text, comments, <? ?> and <! > until the next element tag or closing tag.
    void skip_misc() {
        for (;;) {
            while (p < s.size() && s[p] != '<') ++p;
            if (p + 1 >= s.size()) return;
            if (s.compare(p, 4, "<!--") == 0) {
                const size_t e = s.find("-->", p + 4);
                p = e == std::string_view::npos ? s.size() : e + 3;
            } else if (s[p + 1] == '?' || s[p + 1] == '!') {
                const size_t e = s.find('>', p + 2);
                p = e == std::string_view::npos ? s.size() : e + 1;
            } else {
                return;
            }
        }
    }
    std::string name() {
        const size_t b = p;
        while (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\r' && s[p] != '\n' && s[p] != '>' && s[p] != '/' && s[p] != '=')
            ++p;
        return std::string(s.substr(b, p - b));
    }
    std::unique_ptr<Node> element() {
        // At '<'.
        ++p;
        auto node = std::make_unique<Node>();
        node->name = name();
        for (;;) {
            skip_ws();
            if (at_end()) {
                error = "unterminated tag <" + node->name + ">";
                return nullptr;
            }
            if (s[p] == '/') {
                p = s.find('>', p);
                if (p == std::string_view::npos) p = s.size();
                else ++p;
                return node;
            }
            if (s[p] == '>') {
                ++p;
                break;
            }
            std::string key = name();
            skip_ws();
            std::string value;
            if (p < s.size() && s[p] == '=') {
                ++p;
                skip_ws();
                if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
                    const char q = s[p++];
                    const size_t e = s.find(q, p);
                    if (e == std::string_view::npos) {
                        error = "unterminated attribute in <" + node->name + ">";
                        return nullptr;
                    }
                    value = decode(s.substr(p, e - p));
                    p = e + 1;
                }
            }
            if (key.empty()) {
                ++p;   // stray character
                continue;
            }
            node->attributes.emplace_back(std::move(key), std::move(value));
        }
        // Children until the matching close tag.
        for (;;) {
            skip_misc();
            if (at_end()) {
                error = "missing </" + node->name + ">";
                return nullptr;
            }
            if (s.compare(p, 2, "</") == 0) {
                const size_t e = s.find('>', p);
                p = e == std::string_view::npos ? s.size() : e + 1;
                return node;
            }
            auto child = element();
            if (!child) return nullptr;
            node->children.push_back(std::move(child));
        }
    }
};

}  // namespace

std::string_view Node::attr(std::string_view key, std::string_view fallback) const {
    for (const auto& [k, v] : attributes)
        if (iequal(k, key)) return v;
    return fallback;
}

float Node::attr_float(std::string_view key, float fallback) const {
    const std::string_view v = attr(key);
    return v.empty() ? fallback : float(std::atof(std::string(v).c_str()));
}

int Node::attr_int(std::string_view key, int fallback) const {
    const std::string_view v = attr(key);
    return v.empty() ? fallback : std::atoi(std::string(v).c_str());
}

const Node* Node::child(std::string_view n) const {
    for (const auto& c : children)
        if (iequal(c->name, n)) return c.get();
    return nullptr;
}

std::vector<const Node*> Node::all(std::string_view n) const {
    std::vector<const Node*> out;
    for (const auto& c : children)
        if (iequal(c->name, n)) out.push_back(c.get());
    return out;
}

std::unique_ptr<Node> parse(std::string_view text, std::string* error) {
    Parser ps{text};
    ps.skip_misc();
    if (ps.at_end()) {
        if (error) *error = "no root element";
        return nullptr;
    }
    auto root = ps.element();
    if (!root && error) *error = ps.error;
    return root;
}

}  // namespace eng::xml
