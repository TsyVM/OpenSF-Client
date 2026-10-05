// vangui_data_grid.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — data grid.
//
// A sortable, filterable, virtualized grid over a callback model, built on core
// tables. A per-id cache holds the filtered+sorted row index so steady-state
// drawing is O(visible rows). Opt-in / zero-cost via VANGUI_ENABLE_DATA_GRID.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

struct VanDataGridColumn { const char* name; float init_width = 0.0f; bool sortable = true; };

struct VanDataGridModel
{
    int  (*RowCount)(void* ud);
    void (*GetCell)(void* ud, int row, int col, char* buf, int buf_size);
    void* UserData = nullptr;
};

#ifdef VANGUI_ENABLE_DATA_GRID

// Draw the grid. Returns the row index activated this frame (double/again click),
// or -1. `selected_row` (optional) tracks the current selection.
VANGUI_API int DataGrid(const char* id, const VanDataGridColumn* cols, int col_count,
                        const VanDataGridModel& model, VanVec2 size = VanVec2(0, 0),
                        int* selected_row = nullptr);

#else // ------------------------------- shim ----------------------------------

inline int DataGrid(const char*, const VanDataGridColumn*, int,
                    const VanDataGridModel&, VanVec2 = VanVec2(0, 0), int* = nullptr) { return -1; }

#endif // VANGUI_ENABLE_DATA_GRID

} // namespace VanGui
