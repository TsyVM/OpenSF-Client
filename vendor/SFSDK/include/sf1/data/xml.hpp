// SPDX-License-Identifier: MIT
// sf1/data/xml.hpp — a lossless XML document for the client's script files.
//
// The map scripts ("ground/<map>/<map>.xml", <SFWorldScript>) are machine-written
// and inconsistent in small ways: tabs in most, two spaces in two, a stray double
// space between attributes, " />" in some rows and "/>" in others, and three
// declared encodings (utf-8, windows-949, windows-874). An editor that rewrote them
// from a parsed model would churn every line it did not touch.
//
// So this model keeps what it read. Every element remembers its start and end tag
// exactly as written, and every run of text between tags is a node of its own.
// Writing an untouched document reproduces it byte for byte (tests/test_roundtrip.cpp
// checks all 42 script documents in the client's area archives). Only an element whose attributes were changed
// has its start tag regenerated, and insertions borrow the indentation of their
// siblings, so an edit shows up as the rows it changed and nothing else.
//
// Values are kept as the raw bytes between the quotes — no entity decoding and no
// transcoding — because the files mix encodings and none of them use entities.
#pragma once

#include "../result.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf1::data::xml {

struct Attribute {
    std::string name;
    std::string value;   // raw bytes between the quotes
};

enum class NodeKind : std::uint8_t {
    Element,
    Text,     // characters between tags, whitespace included
    Markup,   // <?xml ...?>, <!-- ... -->, <!DOCTYPE ...> — kept verbatim
};

struct Node {
    NodeKind                kind = NodeKind::Text;
    std::string             text;          // Text and Markup: exactly as read
    std::string             name;          // Element
    std::vector<Attribute>  attributes;    // Element, in file order
    std::vector<Node>       children;      // Element
    bool                    self_closing = false;
    std::string             open_tag;      // Element: start tag as read; empty once regenerated
    std::string             close_tag;     // Element: end tag as read; empty when self-closing or regenerated

    [[nodiscard]] bool is_element() const noexcept { return kind == NodeKind::Element; }
    [[nodiscard]] bool is_element(std::string_view element_name) const noexcept;

    // The value of an attribute (case-insensitive name), or an empty view.
    [[nodiscard]] std::string_view attribute(std::string_view attribute_name) const noexcept;
    [[nodiscard]] bool has_attribute(std::string_view attribute_name) const noexcept;
    // Changes or appends an attribute. The start tag is regenerated on write.
    void set_attribute(std::string_view attribute_name, std::string_view value);

    // Element children, skipping text and markup.
    [[nodiscard]] std::size_t element_count() const noexcept;
    // The n-th element child, or nullptr.
    [[nodiscard]] Node* element_at(std::size_t n) noexcept;
    [[nodiscard]] const Node* element_at(std::size_t n) const noexcept;
    // First element child with this name (case-insensitive), or nullptr.
    [[nodiscard]] Node* find_child(std::string_view element_name) noexcept;
    [[nodiscard]] const Node* find_child(std::string_view element_name) const noexcept;
    // The text inside a leaf element such as <ObjectiveType>Destroy</ObjectiveType>, trimmed.
    [[nodiscard]] std::string_view inner_text() const noexcept;
    void set_inner_text(std::string_view value);
};

struct Document {
    Node        top;                    // unnamed container for the prolog, the root and what follows
    std::string newline = "\r\n";       // detected from the file
    std::string indent = "\t";          // one level, detected from the file

    // The first element at top level, or nullptr.
    [[nodiscard]] Node* root() noexcept;
    [[nodiscard]] const Node* root() const noexcept;
    // The encoding the declaration names ("utf-8", "windows-949", ...), or empty.
    [[nodiscard]] std::string declared_encoding() const;
};

[[nodiscard]] Result<Document> parse(std::string_view text) noexcept;
[[nodiscard]] Result<Document> parse(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] std::string write(const Document& document);

// A new self-closing element.
[[nodiscard]] Node make_element(std::string name, std::vector<Attribute> attributes = {});

// Inserts `element` as the `element_index`-th element child of `parent` (clamped to
// the end), laid out like its siblings. `parent_depth` is the parent's nesting depth
// (the root is 0) and is only used when the parent has no children to copy from.
// Returns the inserted node.
Node& insert_element(Document& document, Node& parent, std::size_t element_index, Node element, int parent_depth);

// Removes the `element_index`-th element child of `parent` together with the
// whitespace that introduced it. False when there is no such child.
bool remove_element(Node& parent, std::size_t element_index);

}  // namespace sf1::data::xml
