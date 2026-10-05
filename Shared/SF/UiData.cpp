#include "SF/UiData.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace sf {

namespace {

// A kit's rank among the lobby's atlases: the 2010 graphics update's "NewAlpha" sheets are the
// newest, the grey 2004 "source0N" sheets the oldest.
int generation_of(const std::string& stem) {
    if (stem.starts_with("sourcenewalpha")) return 100 + std::atoi(stem.c_str() + 14);
    if (stem.starts_with("sourcebandi")) return 90;
    if (stem.starts_with("sourcenew")) return 80 + std::atoi(stem.c_str() + 9);
    if (stem.starts_with("sourceold")) return 70 + std::atoi(stem.c_str() + 9);
    if (stem.starts_with("source")) return std::atoi(stem.c_str() + 6);
    return 50;
}

std::vector<std::string> split_ws(std::string_view line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
        size_t j = i;
        while (j < line.size() && !std::isspace((unsigned char)line[j])) ++j;
        if (j > i) out.emplace_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

bool is_int(const std::string& s) {
    if (s.empty()) return false;
    size_t i = s[0] == '-' ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i)
        if (!std::isdigit((unsigned char)s[i])) return false;
    return true;
}

std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

}  // namespace

// ── UiAtlas ────────────────────────────────────────────────────────────────────

bool UiAtlas::load(const Data& data) {
    sheets_.clear();
    sprites_.clear();
    by_name_.clear();
    for (const auto& [key, where] : data.keys(Pack::Lobby, "", ".txt")) {
        if (key.find('/') != std::string::npos) continue;
        const std::string stem = stem_of(key);
        // A sprite table has a picture of the same stem beside it.
        std::string image;
        for (const char* ext : {".tga", ".bmp", ".png", ".jpg"})
            if (data.resolve(Pack::Lobby, stem + ext)) {
                image = stem + ext;
                break;
            }
        if (image.empty()) continue;
        auto bytes = data.read(Pack::Lobby, where);
        if (!bytes) continue;
        const std::string text = cp949_to_utf8(*bytes);
        std::vector<Sprite> found;
        for (std::string_view line : eng::str::split(text, '\n')) {
            line = eng::str::trim(line);
            if (line.empty() || line.starts_with("//")) continue;
            auto tok = split_ws(line);
            if (tok.size() < 5 || !is_int(tok[1]) || !is_int(tok[2]) || !is_int(tok[3]) || !is_int(tok[4])) continue;
            Sprite s;
            s.name = tok[0];
            s.x0 = std::atoi(tok[1].c_str());
            s.y0 = std::atoi(tok[2].c_str());
            s.x1 = std::atoi(tok[3].c_str());
            s.y1 = std::atoi(tok[4].c_str());
            if (s.x1 <= s.x0 || s.y1 <= s.y0) continue;
            found.push_back(std::move(s));
        }
        if (found.empty()) continue;
        Sheet sheet;
        sheet.name = stem;
        sheet.text_key = key;
        sheet.image_key = image;
        sheet.generation = generation_of(stem);
        const int index = int(sheets_.size());
        sheets_.push_back(std::move(sheet));
        for (Sprite& s : found) {
            s.sheet = index;
            sprites_.push_back(std::move(s));
        }
    }
    for (int i = 0; i < int(sprites_.size()); ++i) by_name_[lower(sprites_[size_t(i)].name)].push_back(i);
    for (auto& [name, list] : by_name_)
        std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
            return sheets_[size_t(sprites_[size_t(a)].sheet)].generation > sheets_[size_t(sprites_[size_t(b)].sheet)].generation;
        });
    LOG_INFO("SF: lobby atlases: %zu sheets, %zu sprites", sheets_.size(), sprites_.size());
    return !sprites_.empty();
}

const Sprite* UiAtlas::find(std::string_view name, std::string_view sheet) const {
    auto it = by_name_.find(lower(name));
    if (it == by_name_.end()) return nullptr;
    if (sheet.empty()) return &sprites_[size_t(it->second.front())];
    const std::string want = lower(sheet);
    for (int i : it->second)
        if (sheets_[size_t(sprites_[size_t(i)].sheet)].name == want) return &sprites_[size_t(i)];
    return nullptr;
}

// ── Page scripts ───────────────────────────────────────────────────────────────

int PageNode::id() const { return number("ID", 0); }

bool PageNode::rect(int& x0, int& y0, int& x1, int& y1) const {
    auto it = props.find("RECT");
    if (it == props.end() || it->second.size() < 4) return false;
    x0 = std::atoi(it->second[0].c_str());
    y0 = std::atoi(it->second[1].c_str());
    x1 = std::atoi(it->second[2].c_str());
    y1 = std::atoi(it->second[3].c_str());
    return true;
}

std::string PageNode::text(std::string_view key) const {
    auto it = props.find(std::string(key));
    if (it == props.end() || it->second.empty()) return {};
    return unquote(it->second.front());
}

int PageNode::number(std::string_view key, int fallback) const {
    auto it = props.find(std::string(key));
    if (it == props.end() || it->second.empty()) return fallback;
    return std::atoi(it->second.front().c_str());
}

const PageNode* PageNode::find_id(int want, std::string_view k) const {
    if (id() == want && (k.empty() || kind == k)) return this;
    for (const PageNode& c : children)
        if (const PageNode* hit = c.find_id(want, k)) return hit;
    return nullptr;
}

void PageNode::collect(std::string_view k, std::vector<const PageNode*>& out) const {
    if (kind == k) out.push_back(this);
    for (const PageNode& c : children) c.collect(k, out);
}

namespace {

// Tokens of a page script: "*KEY", "{", "}", a quoted string, or a bare word; comments ride
// along so the element they annotate can keep them.
struct PageLexer {
    std::string_view s;
    size_t at = 0;
    std::string pending_comment;

    enum class Kind { End, Key, Open, Close, Word, Newline };
    Kind kind = Kind::End;
    std::string value;

    void next() {
        for (;;) {
            if (at >= s.size()) {
                kind = Kind::End;
                return;
            }
            const char c = s[at];
            if (c == '\n') {
                ++at;
                kind = Kind::Newline;
                return;
            }
            if (std::isspace((unsigned char)c)) {
                ++at;
                continue;
            }
            if (c == '/' && at + 1 < s.size() && s[at + 1] == '/') {
                size_t e = s.find('\n', at);
                if (e == std::string_view::npos) e = s.size();
                pending_comment = std::string(eng::str::trim(s.substr(at + 2, e - at - 2)));
                at = e;
                continue;
            }
            if (c == '{') {
                ++at;
                kind = Kind::Open;
                return;
            }
            if (c == '}') {
                ++at;
                kind = Kind::Close;
                return;
            }
            if (c == '"') {
                size_t e = s.find('"', at + 1);
                if (e == std::string_view::npos) e = s.size();
                value = std::string(s.substr(at, std::min(e + 1, s.size()) - at));
                at = std::min(e + 1, s.size());
                kind = Kind::Word;
                return;
            }
            size_t e = at;
            while (e < s.size() && !std::isspace((unsigned char)s[e]) && s[e] != '{' && s[e] != '}') {
                if (s[e] == '/' && e + 1 < s.size() && s[e + 1] == '/') break;
                ++e;
            }
            value = std::string(s.substr(at, e - at));
            at = e;
            kind = value.starts_with('*') ? Kind::Key : Kind::Word;
            if (kind == Kind::Key) value.erase(0, 1);
            return;
        }
    }
};

// Parses the body of a block (after its '{') into `node` until the matching '}'.
// COMMON's properties are hoisted into the element; CHILD's elements become its children.
void parse_block(PageLexer& lx, PageNode& node, int depth) {
    if (depth > 64) return;
    lx.next();
    while (lx.kind != PageLexer::Kind::End && lx.kind != PageLexer::Kind::Close) {
        if (lx.kind != PageLexer::Kind::Key) {
            lx.next();
            continue;
        }
        std::string key = lx.value;
        std::vector<std::string> values;
        lx.pending_comment.clear();
        lx.next();
        while (lx.kind == PageLexer::Kind::Word) {
            values.push_back(lx.value);
            lx.next();
        }
        while (lx.kind == PageLexer::Kind::Newline) lx.next();
        if (lx.kind == PageLexer::Kind::Open) {
            if (key == "COMMON") {
                PageNode common;
                parse_block(lx, common, depth + 1);
                for (auto& [k, v] : common.props) node.props[k] = v;
            } else if (key == "CHILD") {
                PageNode holder;
                parse_block(lx, holder, depth + 1);
                for (auto& c : holder.children) node.children.push_back(std::move(c));
            } else {
                PageNode child;
                child.kind = key;
                child.comment = lx.pending_comment;
                parse_block(lx, child, depth + 1);
                node.children.push_back(std::move(child));
            }
            lx.next();
            continue;
        }
        if (!values.empty() || !node.props.contains(key)) node.props[key] = std::move(values);
        // A property's own line may have ended the value list; carry on from here.
        while (lx.kind == PageLexer::Kind::Newline) lx.next();
    }
}

}  // namespace

std::optional<PageNode> parse_page(std::string_view text) {
    const std::string utf8 = cp949_to_utf8(text);
    PageLexer lx{utf8};
    PageNode root;
    root.kind = "PAGE";
    // Top level: a sequence of "*KIND { ... }" blocks.
    lx.next();
    while (lx.kind != PageLexer::Kind::End) {
        if (lx.kind != PageLexer::Kind::Key) {
            lx.next();
            continue;
        }
        std::string key = lx.value;
        std::string note = lx.pending_comment;
        lx.next();
        while (lx.kind == PageLexer::Kind::Word || lx.kind == PageLexer::Kind::Newline) lx.next();
        if (lx.kind != PageLexer::Kind::Open) continue;
        PageNode node;
        node.kind = key;
        node.comment = note;
        parse_block(lx, node, 1);
        root.children.push_back(std::move(node));
        lx.next();
    }
    if (root.children.empty()) return std::nullopt;
    if (root.children.size() == 1) return std::move(root.children.front());
    return root;
}

std::optional<PageNode> load_page(const Data& data, std::string_view name) {
    std::string key = lower(name);
    if (!key.ends_with(".txt")) key += ".txt";
    auto bytes = data.read(Pack::Lobby, key);
    if (!bytes) return std::nullopt;
    return parse_page(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

// ── Tables ─────────────────────────────────────────────────────────────────────

std::optional<std::string> load_lobby_text(const Data& data, std::string_view name) {
    auto bytes = data.read(Pack::Lobby, lower(name));
    if (!bytes) return std::nullopt;
    return cp949_to_utf8(*bytes);
}

std::vector<MapInfo> load_map_names(const Data& data) {
    std::vector<MapInfo> out;
    auto text = load_lobby_text(data, "MapName.txt");
    if (!text) return out;
    // Tokens: quoted strings and bare words, in order.
    std::vector<std::pair<bool, std::string>> tok;   // (quoted, text)
    const std::string& s = *text;
    for (size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (std::isspace((unsigned char)c)) {
            ++i;
        } else if (c == '"') {
            size_t e = s.find('"', i + 1);
            if (e == std::string::npos) e = s.size();
            tok.emplace_back(true, s.substr(i + 1, e - i - 1));
            i = e + 1;
        } else {
            size_t e = i;
            // A picture name can hold a space ("SF_M_predator b.jpg"), so a bare word runs to
            // the next tab, newline or quote.
            while (e < s.size() && s[e] != '\t' && s[e] != '\n' && s[e] != '\r' && s[e] != '"') ++e;
            const std::string w(eng::str::trim(std::string_view(s).substr(i, e - i)));
            // Crossroad's line has spaces, not a tab, after its number ("13      SF_M_Crossroad.jpg"):
            // a number at the front of a word is a token of its own.
            size_t digits = 0;
            while (digits < w.size() && std::isdigit((unsigned char)w[digits])) ++digits;
            if (digits > 0 && digits < w.size() && std::isspace((unsigned char)w[digits])) {
                tok.emplace_back(false, w.substr(0, digits));
                tok.emplace_back(false, std::string(eng::str::trim(std::string_view(w).substr(digits))));
            } else {
                tok.emplace_back(false, w);
            }
            i = e;
        }
    }
    auto fix = [](std::string t) {
        for (char& c : t)
            if (c == '|') c = '\n';
        return std::string(eng::str::trim(t));
    };
    for (size_t i = 0; i + 6 < tok.size(); ++i) {
        if (tok[i].first || !is_int(tok[i].second) || tok[i + 1].first) continue;
        bool five = true;
        for (size_t k = 2; k < 7; ++k) five &= tok[i + k].first;
        if (!five) continue;
        MapInfo m;
        m.index = std::atoi(tok[i].second.c_str());
        m.picture = tok[i + 1].second;
        m.name = tok[i + 2].second;
        m.mission = tok[i + 3].second;
        m.attack_text = fix(tok[i + 4].second);
        m.defence_text = fix(tok[i + 5].second);
        m.training = lower(tok[i + 6].second) == "training";
        out.push_back(std::move(m));
        i += 6;
    }
    return out;
}

std::vector<RankInfo> load_ranks(const Data& data) {
    std::vector<RankInfo> out;
    auto text = load_lobby_text(data, "SF_ClassPoint.txt");
    if (!text) return out;
    RankInfo cur;
    bool open = false;
    for (std::string_view line : eng::str::split(*text, '\n')) {
        line = eng::str::trim(line);
        if (line.starts_with("[CLASS-")) {
            if (open) out.push_back(cur);
            cur = RankInfo{};
            open = true;
            continue;
        }
        const size_t eq = line.find('=');
        if (!open || eq == std::string_view::npos) continue;
        const std::string k(eng::str::trim(line.substr(0, eq)));
        const std::string v(eng::str::trim(line.substr(eq + 1)));
        if (k == "NAME") cur.name = v;
        else if (k == "CLASSID") cur.id = std::atoi(v.c_str());
        else if (k == "MAXPOINT") cur.max_point = std::atoi(v.c_str());
    }
    if (open) out.push_back(cur);
    std::sort(out.begin(), out.end(), [](const RankInfo& a, const RankInfo& b) { return a.id < b.id; });
    return out;
}

}  // namespace sf
