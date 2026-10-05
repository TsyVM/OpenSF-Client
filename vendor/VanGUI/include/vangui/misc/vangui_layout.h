// vangui_layout.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Pillar 3: constraint/flow layout helpers.
//
// Pure layout math over the immediate-mode cursor API. There is NO retained
// layout tree: each helper is a Begin/End scope that pushes spacing and tracks a
// little state in a context-side stack, exactly like the core's own Group stack.
//
// CONTRACT
//   * VBox stacks children vertically (the default flow) with uniform spacing.
//   * HBox places children on one line. Adjacent widgets follow the normal IM
//     idiom (the box manages spacing); use Stretch() as a flexible spacer to
//     justify — e.g. push the last button to the right edge of the row.
//   * Grid arranges children into `columns` columns, wrapping every `columns`
//     cells. Call NextCell() between cells (thin wrapper over SameLine/newline).
//   * Stretch(weight) inserts a flexible spacer that consumes the remaining space
//     along the active box's axis (single-spacer justify is the common case).
//   * EndBox() closes the most recent Begin*; a RAII guard is provided.
//   * Opt-in / zero-cost-when-off via VANGUI_ENABLE_LAYOUT.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_LAYOUT

// spacing < 0 means "use the current style ItemSpacing".
VANGUI_API void BeginHBox(const char* id, float spacing = -1.0f);
VANGUI_API void BeginVBox(const char* id, float spacing = -1.0f);
VANGUI_API void BeginGrid(const char* id, int columns, float spacing = -1.0f);
VANGUI_API void EndBox();

// Advance to the next grid cell (no-op outside a grid). Wraps the row every
// `columns` cells. Harmless to call in an HBox (acts as a spacing separator).
VANGUI_API void NextCell();

// Flexible spacer: consumes remaining space along the active box axis.
VANGUI_API void Stretch(float weight = 1.0f);

// RAII guard: VanBox b(BeginVBox, ...) style. Use the macro-free form:
//   { VanBoxScope _box([]{ VanGui::BeginVBox("id"); }); ... }  // verbose
// Simpler: call EndBox() yourself, or use VAN_VBOX/VAN_HBOX below.
struct VanBoxScope
{
    VanBoxScope() = default;
    ~VanBoxScope() { EndBox(); }
    VanBoxScope(const VanBoxScope&) = delete;
    VanBoxScope& operator=(const VanBoxScope&) = delete;
};

#else // ----------------------------- shims -----------------------------------

inline void BeginHBox(const char*, float = -1.0f) {}
inline void BeginVBox(const char*, float = -1.0f) {}
inline void BeginGrid(const char*, int, float = -1.0f) {}
inline void EndBox() {}
inline void NextCell() {}
inline void Stretch(float = 1.0f) {}
struct VanBoxScope { ~VanBoxScope() {} };

#endif // VANGUI_ENABLE_LAYOUT

} // namespace VanGui
