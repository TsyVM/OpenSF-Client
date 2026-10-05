// vangui_views.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Pillar 3: model/view adapters.
//
// Qt's model/view, expressed immediate-mode and virtualized through the existing
// VanGuiListClipper so a million-row model still renders O(visible rows) with
// zero retained row widgets. The model is a plain struct of function pointers —
// composition over inheritance; the data type implements nothing.
//
//   * Opt-in / zero-cost-when-off via VANGUI_ENABLE_VIEWS.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

// User-implemented list model. No inheritance required of the data type.
struct VanListModel
{
    int  (*RowCount)(void* ud);                       // number of rows
    void (*DrawRow)(void* ud, int row, bool selected);// draw one row's contents
    void*  UserData;
};

// User-implemented tree model. Nodes are addressed by an opaque int handle; -1 is
// the virtual root. The model exposes child counts and per-node child handles.
struct VanTreeModel
{
    int  (*ChildCount)(void* ud, int node);           // children of `node` (-1 = root)
    int  (*ChildAt)(void* ud, int node, int index);   // handle of index-th child
    void (*DrawNode)(void* ud, int node, bool selected); // node label/contents
    bool (*IsLeaf)(void* ud, int node);               // leaf => no expander
    void*  UserData;
};

#ifdef VANGUI_ENABLE_VIEWS

// Virtualized list. `*p_selected` is the selected row index (-1 = none); updated
// on click. Returns true if the selection changed this frame.
VANGUI_API bool BeginListView(const char* id, const VanListModel& model, int* p_selected,
                              const VanVec2& size = VanVec2(0, 0));
VANGUI_API void EndListView();

// Tree view over the model. `*p_selected` holds the selected node handle.
// Returns true if the selection changed this frame.
VANGUI_API bool BeginTreeView(const char* id, const VanTreeModel& model, int* p_selected,
                              const VanVec2& size = VanVec2(0, 0));
VANGUI_API void EndTreeView();

#else // ----------------------------- shims -----------------------------------

inline bool BeginListView(const char*, const VanListModel&, int*, const VanVec2& = VanVec2(0,0)) { return false; }
inline void EndListView() {}
inline bool BeginTreeView(const char*, const VanTreeModel&, int*, const VanVec2& = VanVec2(0,0)) { return false; }
inline void EndTreeView() {}

#endif // VANGUI_ENABLE_VIEWS

} // namespace VanGui
