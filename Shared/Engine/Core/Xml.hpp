// A small XML reader for the data files the game ships (Scrooby projects, screens and pages):
// elements, attributes and nesting. Text content, comments, processing instructions and DOCTYPEs
// are skipped; entities &amp; &lt; &gt; &quot; &apos; are decoded in attribute values.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eng::xml {

struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<std::unique_ptr<Node>> children;

    // Attribute value, or `fallback` when absent (names compare case-insensitively).
    std::string_view attr(std::string_view key, std::string_view fallback = {}) const;
    float attr_float(std::string_view key, float fallback = 0) const;
    int attr_int(std::string_view key, int fallback = 0) const;
    // First child element with this name, or null.
    const Node* child(std::string_view name) const;
    // Every child element with this name.
    std::vector<const Node*> all(std::string_view name) const;
};

// Parses `text` and returns the root element, or null (with `error`) when malformed.
std::unique_ptr<Node> parse(std::string_view text, std::string* error = nullptr);

}  // namespace eng::xml
