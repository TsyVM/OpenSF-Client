// JSON, for what the game and the server say to Team Vanilla's account service (TVAS) and what it
// answers (Docs/UniversalServerDeploy.md §4.1). A small value tree: objects keep their keys' order,
// numbers keep whether they were whole. Reading never trusts its input: depth and size are bounded,
// and a missing key or a wrong type reads as the fallback rather than failing.
#pragma once

#include "Engine/Core/Types.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eng::json {

class Value {
public:
    enum class Type : u8 { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), b_(b) {}
    Value(int v) : type_(Type::Number), int_(v), num_(double(v)), whole_(true) {}
    Value(unsigned v) : type_(Type::Number), int_(i64(v)), num_(double(v)), whole_(true) {}
    Value(i64 v) : type_(Type::Number), int_(v), num_(double(v)), whole_(true) {}
    Value(u64 v) : type_(Type::Number), int_(i64(v)), num_(double(v)), whole_(true) {}
    Value(double v) : type_(Type::Number), int_(i64(v)), num_(v), whole_(false) {}
    Value(const char* s) : type_(Type::String), str_(s ? s : "") {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(std::string_view s) : type_(Type::String), str_(s) {}
    static Value array() {
        Value v;
        v.type_ = Type::Array;
        return v;
    }
    static Value object() {
        Value v;
        v.type_ = Type::Object;
        return v;
    }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool fallback = false) const;
    i64 as_int(i64 fallback = 0) const;
    u64 as_uint(u64 fallback = 0) const;   // a negative number reads as the fallback
    double as_double(double fallback = 0) const;
    // The string, or the fallback when this is not one.
    std::string str(std::string_view fallback = {}) const;
    const std::string& string_ref() const { return str_; }

    // Objects. A missing key reads as null; writing one makes it (a null becomes an object).
    bool has(std::string_view key) const;
    const Value& operator[](std::string_view key) const;
    Value& operator[](std::string_view key);
    const Value& operator[](const char* key) const { return (*this)[std::string_view(key)]; }
    Value& operator[](const char* key) { return (*this)[std::string_view(key)]; }
    void erase(std::string_view key);
    const std::vector<std::pair<std::string, Value>>& items() const { return obj_; }

    // Arrays. An index past the end reads as null; push makes a null into an array.
    size_t size() const { return type_ == Type::Array ? arr_.size() : type_ == Type::Object ? obj_.size() : 0; }
    // Every form of an index (const or not, int or size_t), so a literal 0 is never read as a key.
    const Value& operator[](size_t i) const;
    const Value& operator[](size_t i) { return std::as_const(*this)[i]; }
    const Value& operator[](int i) const { return (*this)[size_t(i < 0 ? ~size_t(0) : size_t(i))]; }
    const Value& operator[](int i) { return std::as_const(*this)[i]; }
    const Value& at(size_t i) const { return (*this)[i]; }
    Value& push(Value v);
    const std::vector<Value>& elements() const { return arr_; }
    std::vector<Value>& elements_mut() { return arr_; }

    // Compact text, UTF-8, with only what must be escaped escaped.
    std::string dump() const;
    void dump_to(std::string& out) const;

private:
    Type type_ = Type::Null;
    bool b_ = false;
    i64 int_ = 0;
    double num_ = 0;
    bool whole_ = false;
    std::string str_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
    friend class Parser;
};

// False (and `error`) on malformed text, nesting deeper than `max_depth`, or text past the value.
bool parse(std::string_view text, Value& out, std::string* error = nullptr, int max_depth = 64);
// A string as a JSON string literal, quotes included.
std::string quote(std::string_view s);

}  // namespace eng::json
