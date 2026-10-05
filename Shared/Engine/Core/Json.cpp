#include "Engine/Core/Json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace eng::json {

namespace {
const Value& null_value() {
    static const Value v;
    return v;
}
}  // namespace

bool Value::as_bool(bool fallback) const {
    if (type_ == Type::Bool) return b_;
    if (type_ == Type::Number) return whole_ ? int_ != 0 : num_ != 0;
    return fallback;
}

i64 Value::as_int(i64 fallback) const {
    if (type_ != Type::Number) return fallback;
    if (whole_) return int_;
    if (!std::isfinite(num_) || num_ > 9.2e18 || num_ < -9.2e18) return fallback;
    return i64(num_);
}

u64 Value::as_uint(u64 fallback) const {
    const i64 v = as_int(-1);
    return v < 0 ? fallback : u64(v);
}

double Value::as_double(double fallback) const { return type_ == Type::Number ? (whole_ ? double(int_) : num_) : fallback; }

std::string Value::str(std::string_view fallback) const { return type_ == Type::String ? str_ : std::string(fallback); }

bool Value::has(std::string_view key) const {
    if (type_ != Type::Object) return false;
    for (const auto& [k, v] : obj_)
        if (k == key) return true;
    return false;
}

const Value& Value::operator[](std::string_view key) const {
    if (type_ == Type::Object)
        for (const auto& [k, v] : obj_)
            if (k == key) return v;
    return null_value();
}

Value& Value::operator[](std::string_view key) {
    if (type_ == Type::Null) type_ = Type::Object;
    for (auto& [k, v] : obj_)
        if (k == key) return v;
    obj_.emplace_back(std::string(key), Value());
    return obj_.back().second;
}

void Value::erase(std::string_view key) {
    for (auto it = obj_.begin(); it != obj_.end(); ++it)
        if (it->first == key) {
            obj_.erase(it);
            return;
        }
}

const Value& Value::operator[](size_t i) const { return type_ == Type::Array && i < arr_.size() ? arr_[i] : null_value(); }

Value& Value::push(Value v) {
    if (type_ == Type::Null) type_ = Type::Array;
    arr_.push_back(std::move(v));
    return arr_.back();
}

namespace {
void quote_to(std::string& out, std::string_view s) {
    out.push_back('"');
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(c));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}
}  // namespace

std::string quote(std::string_view s) {
    std::string out;
    quote_to(out, s);
    return out;
}

void Value::dump_to(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[40];
            if (whole_) {
                auto [p, ec] = std::to_chars(buf, buf + sizeof buf, int_);
                out.append(buf, p);
            } else if (!std::isfinite(num_)) {
                out += "null";
            } else {
                auto [p, ec] = std::to_chars(buf, buf + sizeof buf, num_);
                out.append(buf, p);
            }
            break;
        }
        case Type::String: quote_to(out, str_); break;
        case Type::Array: {
            out.push_back('[');
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out.push_back(',');
                arr_[i].dump_to(out);
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) out.push_back(',');
                quote_to(out, obj_[i].first);
                out.push_back(':');
                obj_[i].second.dump_to(out);
            }
            out.push_back('}');
            break;
        }
    }
}

std::string Value::dump() const {
    std::string out;
    dump_to(out);
    return out;
}

class Parser {
public:
    Parser(std::string_view t, int depth) : t_(t), max_depth_(depth) {}
    bool run(Value& out, std::string* error) {
        ws();
        if (!value(out, 0)) {
            if (error) *error = err_.empty() ? "malformed JSON" : err_;
            return false;
        }
        ws();
        if (i_ != t_.size()) {
            if (error) *error = "text after the value";
            return false;
        }
        return true;
    }

private:
    void ws() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\t' || t_[i_] == '\n' || t_[i_] == '\r')) ++i_;
    }
    bool fail(const char* why) {
        if (err_.empty()) err_ = why;
        return false;
    }
    bool literal(std::string_view word) {
        if (t_.substr(i_, word.size()) != word) return fail("bad literal");
        i_ += word.size();
        return true;
    }
    static void utf8(std::string& out, u32 cp) {
        if (cp < 0x80) out.push_back(char(cp));
        else if (cp < 0x800) out.push_back(char(0xC0 | (cp >> 6))), out.push_back(char(0x80 | (cp & 0x3F)));
        else if (cp < 0x10000) out.push_back(char(0xE0 | (cp >> 12))), out.push_back(char(0x80 | ((cp >> 6) & 0x3F))), out.push_back(char(0x80 | (cp & 0x3F)));
        else
            out.push_back(char(0xF0 | (cp >> 18))), out.push_back(char(0x80 | ((cp >> 12) & 0x3F))), out.push_back(char(0x80 | ((cp >> 6) & 0x3F))),
                out.push_back(char(0x80 | (cp & 0x3F)));
    }
    bool hex4(u32& v) {
        if (i_ + 4 > t_.size()) return fail("short \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= u32(c - '0');
            else if (c >= 'a' && c <= 'f') v |= u32(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= u32(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& out) {
        ++i_;   // the opening quote
        while (i_ < t_.size()) {
            const char c = t_[i_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in a string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= t_.size()) break;
            const char e = t_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    u32 cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < t_.size() && t_[i_] == '\\' && t_[i_ + 1] == 'u') {
                        i_ += 2;
                        u32 lo;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else cp = 0xFFFD;
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    utf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool number(Value& out) {
        const size_t start = i_;
        bool whole = true;
        if (i_ < t_.size() && t_[i_] == '-') ++i_;
        while (i_ < t_.size()) {
            const char c = t_[i_];
            if (c >= '0' && c <= '9') ++i_;
            else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') whole = false, ++i_;
            else break;
        }
        const std::string_view digits = t_.substr(start, i_ - start);
        if (digits.empty() || digits == "-") return fail("bad number");
        out.type_ = Value::Type::Number;
        if (whole) {
            i64 v = 0;
            auto [p, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), v);
            if (ec == std::errc() && p == digits.data() + digits.size()) {
                out.int_ = v, out.num_ = double(v), out.whole_ = true;
                return true;
            }
        }
        double d = 0;
        auto [p, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), d);
        if (ec != std::errc() || p != digits.data() + digits.size() || !std::isfinite(d)) return fail("bad number");
        out.num_ = d, out.int_ = i64(std::clamp(d, -9.2e18, 9.2e18)), out.whole_ = false;
        return true;
    }
    bool value(Value& out, int depth) {
        if (depth > max_depth_) return fail("nested too deeply");
        if (i_ >= t_.size()) return fail("unexpected end");
        const char c = t_[i_];
        if (c == '{') {
            ++i_;
            out = Value::object();
            ws();
            if (i_ < t_.size() && t_[i_] == '}') return ++i_, true;
            for (;;) {
                ws();
                if (i_ >= t_.size() || t_[i_] != '"') return fail("expected a key");
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i_ >= t_.size() || t_[i_] != ':') return fail("expected ':'");
                ++i_;
                ws();
                Value v;
                if (!value(v, depth + 1)) return false;
                out.obj_.emplace_back(std::move(key), std::move(v));
                ws();
                if (i_ < t_.size() && t_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < t_.size() && t_[i_] == '}') return ++i_, true;
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i_;
            out = Value::array();
            ws();
            if (i_ < t_.size() && t_[i_] == ']') return ++i_, true;
            for (;;) {
                ws();
                Value v;
                if (!value(v, depth + 1)) return false;
                out.arr_.push_back(std::move(v));
                ws();
                if (i_ < t_.size() && t_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < t_.size() && t_[i_] == ']') return ++i_, true;
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            out = Value(std::string());
            return string(out.str_);
        }
        if (c == 't') return literal("true") && (out = Value(true), true);
        if (c == 'f') return literal("false") && (out = Value(false), true);
        if (c == 'n') return literal("null") && (out = Value(), true);
        return number(out);
    }

    std::string_view t_;
    size_t i_ = 0;
    int max_depth_;
    std::string err_;
};

bool parse(std::string_view text, Value& out, std::string* error, int max_depth) {
    out = Value();
    Parser p(text, max_depth);
    return p.run(out, error);
}

}  // namespace eng::json
