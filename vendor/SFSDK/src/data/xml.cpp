// SPDX-License-Identifier: MIT
#include "sf1/data/xml.hpp"

#include <algorithm>
#include <cctype>

namespace sf1::data::xml {

namespace {

bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool whitespace_only(std::string_view s) noexcept {
    return std::all_of(s.begin(), s.end(), is_space);
}

// The end of a start or end tag that begins at `open`, skipping '>' inside quotes.
std::size_t tag_end(std::string_view text, std::size_t open) noexcept {
    char quote = 0;
    for (std::size_t i = open + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return i;
        }
    }
    return std::string_view::npos;
}

// Splits "<name a="1" b='2'/>" into name, attributes and the self-closing flag.
bool parse_start_tag(std::string_view tag, Node& out) {
    std::string_view body = tag.substr(1, tag.size() - 2);   // drop '<' and '>'
    while (!body.empty() && is_space(body.back())) body.remove_suffix(1);
    if (!body.empty() && body.back() == '/') {
        out.self_closing = true;
        body.remove_suffix(1);
    }

    std::size_t i = 0;
    while (i < body.size() && !is_space(body[i])) ++i;
    out.name.assign(body.substr(0, i));
    if (out.name.empty()) return false;

    while (i < body.size()) {
        while (i < body.size() && is_space(body[i])) ++i;
        if (i >= body.size()) break;
        const std::size_t name_start = i;
        while (i < body.size() && body[i] != '=' && !is_space(body[i])) ++i;
        const std::string_view name = body.substr(name_start, i - name_start);
        while (i < body.size() && is_space(body[i])) ++i;
        if (i >= body.size() || body[i] != '=') return false;
        ++i;
        while (i < body.size() && is_space(body[i])) ++i;
        if (i >= body.size() || (body[i] != '"' && body[i] != '\'')) return false;
        const char quote = body[i++];
        const std::size_t value_start = i;
        while (i < body.size() && body[i] != quote) ++i;
        if (i >= body.size()) return false;
        out.attributes.push_back({std::string(name), std::string(body.substr(value_start, i - value_start))});
        ++i;
    }
    return true;
}

void emit(const Node& node, std::string& out) {
    switch (node.kind) {
        case NodeKind::Text:
        case NodeKind::Markup:
            out += node.text;
            return;
        case NodeKind::Element:
            break;
    }
    if (!node.name.empty()) {
        if (!node.open_tag.empty()) {
            out += node.open_tag;
        } else {
            out += '<';
            out += node.name;
            for (const Attribute& a : node.attributes) {
                out += ' ';
                out += a.name;
                out += "=\"";
                out += a.value;
                out += '"';
            }
            out += node.self_closing ? "/>" : ">";
        }
    }
    for (const Node& child : node.children) emit(child, out);
    if (!node.name.empty() && !node.self_closing) {
        if (!node.close_tag.empty()) {
            out += node.close_tag;
        } else {
            out += "</";
            out += node.name;
            out += '>';
        }
    }
}

// The indentation before the first element child, taken from the whitespace
// that precedes it ("\r\n\t\t" gives "\t\t"). Empty when there is none to copy.
std::string sibling_indent(const Node& parent) {
    for (std::size_t i = 0; i < parent.children.size(); ++i) {
        if (!parent.children[i].is_element()) continue;
        if (i == 0 || parent.children[i - 1].kind != NodeKind::Text) return {};
        const std::string& ws = parent.children[i - 1].text;
        const auto nl = ws.find_last_of('\n');
        return nl == std::string::npos ? std::string() : ws.substr(nl + 1);
    }
    return {};
}

std::string repeat(const std::string& unit, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) out += unit;
    return out;
}

}  // namespace

// ── Node ────────────────────────────────────────────────────────────────────

bool Node::is_element(std::string_view element_name) const noexcept {
    return kind == NodeKind::Element && iequals(name, element_name);
}

std::string_view Node::attribute(std::string_view attribute_name) const noexcept {
    for (const Attribute& a : attributes)
        if (iequals(a.name, attribute_name)) return a.value;
    return {};
}

bool Node::has_attribute(std::string_view attribute_name) const noexcept {
    return std::any_of(attributes.begin(), attributes.end(),
                       [&](const Attribute& a) { return iequals(a.name, attribute_name); });
}

void Node::set_attribute(std::string_view attribute_name, std::string_view value) {
    for (Attribute& a : attributes) {
        if (!iequals(a.name, attribute_name)) continue;
        if (a.value == value) return;   // no change keeps the tag byte-identical
        a.value.assign(value);
        open_tag.clear();
        return;
    }
    attributes.push_back({std::string(attribute_name), std::string(value)});
    open_tag.clear();
}

std::size_t Node::element_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(children.begin(), children.end(), [](const Node& n) { return n.is_element(); }));
}

Node* Node::element_at(std::size_t n) noexcept {
    for (Node& child : children)
        if (child.is_element() && n-- == 0) return &child;
    return nullptr;
}

const Node* Node::element_at(std::size_t n) const noexcept {
    for (const Node& child : children)
        if (child.is_element() && n-- == 0) return &child;
    return nullptr;
}

Node* Node::find_child(std::string_view element_name) noexcept {
    for (Node& child : children)
        if (child.is_element(element_name)) return &child;
    return nullptr;
}

const Node* Node::find_child(std::string_view element_name) const noexcept {
    for (const Node& child : children)
        if (child.is_element(element_name)) return &child;
    return nullptr;
}

std::string_view Node::inner_text() const noexcept {
    for (const Node& child : children) {
        if (child.kind != NodeKind::Text) continue;
        std::string_view t = child.text;
        while (!t.empty() && is_space(t.front())) t.remove_prefix(1);
        while (!t.empty() && is_space(t.back())) t.remove_suffix(1);
        if (!t.empty()) return t;
    }
    return {};
}

void Node::set_inner_text(std::string_view value) {
    for (Node& child : children) {
        if (child.kind != NodeKind::Text || whitespace_only(child.text)) continue;
        if (child.text == value) return;
        child.text.assign(value);
        return;
    }
    Node inner;
    inner.kind = NodeKind::Text;
    inner.text.assign(value);
    if (self_closing) {
        self_closing = false;
        open_tag.clear();
    }
    children.push_back(std::move(inner));
}

// ── Document ────────────────────────────────────────────────────────────────

Node* Document::root() noexcept {
    for (Node& n : top.children)
        if (n.is_element()) return &n;
    return nullptr;
}

const Node* Document::root() const noexcept {
    for (const Node& n : top.children)
        if (n.is_element()) return &n;
    return nullptr;
}

std::string Document::declared_encoding() const {
    for (const Node& n : top.children) {
        if (n.kind != NodeKind::Markup || !n.text.starts_with("<?xml")) continue;
        const auto key = n.text.find("encoding");
        if (key == std::string::npos) return {};
        const auto open = n.text.find_first_of("\"'", key);
        if (open == std::string::npos) return {};
        const auto close = n.text.find(n.text[open], open + 1);
        if (close == std::string::npos) return {};
        return n.text.substr(open + 1, close - open - 1);
    }
    return {};
}

// ── Parse and write ─────────────────────────────────────────────────────────

Result<Document> parse(std::string_view text) noexcept {
    Document doc;
    doc.newline = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";

    // Nodes are appended only to the innermost open element, so a pointer to an
    // open element stays valid until that element is closed.
    std::vector<Node*> stack{&doc.top};
    std::size_t pos = 0;

    while (pos < text.size()) {
        Node& parent = *stack.back();
        if (text[pos] != '<') {
            const auto next = text.find('<', pos);
            const std::size_t end = next == std::string_view::npos ? text.size() : next;
            Node t;
            t.kind = NodeKind::Text;
            t.text.assign(text.substr(pos, end - pos));
            parent.children.push_back(std::move(t));
            pos = end;
            continue;
        }

        std::string_view rest = text.substr(pos);
        std::size_t end = std::string_view::npos;
        NodeKind kind = NodeKind::Markup;
        if (rest.starts_with("<!--")) {
            const auto close = text.find("-->", pos + 4);
            end = close == std::string_view::npos ? close : close + 2;
        } else if (rest.starts_with("<![CDATA[")) {
            const auto close = text.find("]]>", pos + 9);
            end = close == std::string_view::npos ? close : close + 2;
        } else if (rest.starts_with("<?")) {
            const auto close = text.find("?>", pos + 2);
            end = close == std::string_view::npos ? close : close + 1;
        } else if (rest.starts_with("<!")) {
            end = text.find('>', pos + 2);
        } else {
            kind = NodeKind::Element;
            end = tag_end(text, pos);
        }
        if (end == std::string_view::npos) return err(Error::Malformed);
        const std::string_view tag = text.substr(pos, end - pos + 1);
        pos = end + 1;

        if (kind == NodeKind::Markup) {
            Node m;
            m.kind = NodeKind::Markup;
            m.text.assign(tag);
            parent.children.push_back(std::move(m));
            continue;
        }

        if (tag.size() >= 3 && tag[1] == '/') {
            // End tag: it must close the innermost open element.
            std::string_view name = tag.substr(2, tag.size() - 3);
            while (!name.empty() && is_space(name.back())) name.remove_suffix(1);
            if (stack.size() < 2 || !iequals(stack.back()->name, name)) return err(Error::Malformed);
            stack.back()->close_tag.assign(tag);
            stack.pop_back();
            continue;
        }

        Node element;
        element.kind = NodeKind::Element;
        if (!parse_start_tag(tag, element)) return err(Error::Malformed);
        element.open_tag.assign(tag);
        parent.children.push_back(std::move(element));
        if (!parent.children.back().self_closing) stack.push_back(&parent.children.back());
    }
    if (stack.size() != 1) return err(Error::Malformed);

    // One level of indentation: the whitespace before the root's first element child.
    if (const Node* root = doc.root()) {
        const std::string first = sibling_indent(*root);
        if (!first.empty()) doc.indent = first;
    }
    return doc;
}

Result<Document> parse(std::span<const std::byte> bytes) noexcept {
    return parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::string write(const Document& document) {
    std::string out;
    for (const Node& node : document.top.children) emit(node, out);
    return out;
}

Node make_element(std::string name, std::vector<Attribute> attributes) {
    Node n;
    n.kind = NodeKind::Element;
    n.name = std::move(name);
    n.attributes = std::move(attributes);
    n.self_closing = true;
    return n;
}

Node& insert_element(Document& document, Node& parent, std::size_t element_index, Node element, int parent_depth) {
    auto whitespace = [](std::string text) {
        Node t;
        t.kind = NodeKind::Text;
        t.text = std::move(text);
        return t;
    };

    std::string indent = sibling_indent(parent);
    if (indent.empty()) indent = repeat(document.indent, parent_depth + 1);
    const std::string lead = document.newline + indent;

    if (parent.element_count() == 0) {
        // An empty or self-closing parent: open it up around the new child.
        if (parent.self_closing) {
            parent.self_closing = false;
            parent.open_tag.clear();
            parent.close_tag.clear();
        }
        std::string closing_indent = repeat(document.indent, parent_depth);
        if (indent.size() >= document.indent.size())
            closing_indent = indent.substr(0, indent.size() - document.indent.size());
        parent.children.clear();
        parent.children.push_back(whitespace(lead));
        parent.children.push_back(std::move(element));
        parent.children.push_back(whitespace(document.newline + closing_indent));
        return parent.children[1];
    }

    // Child-vector position of the element currently at element_index, or of the
    // node just past the last element when inserting at the end.
    std::size_t seen = 0;
    std::size_t last_element = 0;
    for (std::size_t i = 0; i < parent.children.size(); ++i) {
        if (!parent.children[i].is_element()) continue;
        if (seen == element_index) {
            // Before an existing element: [ws] NEW [lead] EXISTING
            parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(i), whitespace(lead));
            parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(i), std::move(element));
            return parent.children[i];
        }
        last_element = i;
        ++seen;
    }
    // After the last element: LAST [lead] NEW [the trailing whitespace already there]
    const auto at = parent.children.begin() + static_cast<std::ptrdiff_t>(last_element + 1);
    const auto ws = parent.children.insert(at, whitespace(lead));
    const auto inserted = parent.children.insert(ws + 1, std::move(element));
    return *inserted;
}

bool remove_element(Node& parent, std::size_t element_index) {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < parent.children.size(); ++i) {
        if (!parent.children[i].is_element()) continue;
        if (seen++ != element_index) continue;
        std::size_t first = i;
        if (i > 0 && parent.children[i - 1].kind == NodeKind::Text && whitespace_only(parent.children[i - 1].text))
            first = i - 1;
        parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(first),
                              parent.children.begin() + static_cast<std::ptrdiff_t>(i + 1));
        return true;
    }
    return false;
}

}  // namespace sf1::data::xml
