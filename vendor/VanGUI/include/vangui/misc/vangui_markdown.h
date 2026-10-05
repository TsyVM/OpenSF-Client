// vangui_markdown.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Markdown mini-renderer.
//
// A small, font-independent Markdown subset for tooltips, About boxes and help
// panels: headings, bold/italic, inline code + fenced code, unordered/ordered
// lists, blockquotes, horizontal rules and [text](url) links. Draw-list based —
// no font atlas requirements. Opt-in / zero-cost via VANGUI_ENABLE_MARKDOWN.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

typedef void (*VanMarkdownLinkFn)(const char* url, void* user_data);

#ifdef VANGUI_ENABLE_MARKDOWN

VANGUI_API void Markdown(const char* text, VanMarkdownLinkFn on_link = nullptr, void* user_data = nullptr);

#else // ------------------------------- shim ----------------------------------

inline void Markdown(const char* text, VanMarkdownLinkFn = nullptr, void* = nullptr)
{
    if (text) TextUnformatted(text);   // graceful fallback: raw text
}

#endif // VANGUI_ENABLE_MARKDOWN

} // namespace VanGui
