// vangui_richtext.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — inline rich text.
//
// Render one line of text with mixed styling from a compact tag markup:
//   <c=RRGGBB>colored</c>  <b>bold</b>  <i>italic</i>  <u>underline</u>
//   <code>monospace-ish</code>
// Draw-list based, font-independent (bold is faux, italic is a tint). For
// headers, labels, legends and status lines. Opt-in via VANGUI_ENABLE_RICHTEXT.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_RICHTEXT

VANGUI_API void    RichText(const char* tagged);
VANGUI_API VanVec2 CalcRichTextSize(const char* tagged);   // approximate (tags stripped)

#else // ------------------------------- shims ---------------------------------

inline void    RichText(const char* tagged) { if (tagged) TextUnformatted(tagged); }
inline VanVec2 CalcRichTextSize(const char* tagged) { return CalcTextSize(tagged ? tagged : ""); }

#endif // VANGUI_ENABLE_RICHTEXT

} // namespace VanGui
