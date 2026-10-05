// vangui_reorder.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — drag-to-reorder helper (header-only).
//
// Turns the item you just submitted into a drag source + drop target so lists,
// tabs and rows can be reordered by dragging. Built on core drag-drop; no
// retained state, no macro gate — just include and use.
//
//   for (int i = 0; i < n; ++i) {
//       Selectable(items[i]);
//       int from, to;
//       if (VanGui::ReorderItem("MYLIST", i, &from, &to)) { /* move from -> to */ }
//   }
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

// Call immediately after submitting the item for `index`. Returns true when a
// reorder drop occurs this frame, writing the source/target indices.
inline bool ReorderItem(const char* payload_type, int index, int* out_from, int* out_to)
{
    bool moved = false;
    if (BeginDragDropSource(VanGuiDragDropFlags_None)) {
        (void)SetDragDropPayload(payload_type, &index, sizeof(int));
        TextUnformatted("...");
        EndDragDropSource();
    }
    if (BeginDragDropTarget()) {
        if (const VanGuiPayload* pl = AcceptDragDropPayload(payload_type)) {
            if (pl->Data && pl->DataSize == (int)sizeof(int)) {
                const int from = *static_cast<const int*>(pl->Data);
                if (from != index) {
                    if (out_from) *out_from = from;
                    if (out_to)   *out_to   = index;
                    moved = true;
                }
            }
        }
        EndDragDropTarget();
    }
    return moved;
}

// Live reorder: like ReorderItem, but the move is reported while dragging, as soon
// as the dragged item passes over another, so the list makes room as you drag.
// Apply each move at once, give rows a stable ID (PushID(item_id), not the index)
// so the drag stays with the item, and wrap each row in a glide (Anim::BeginGlide /
// van::glide) so the others slide out of the way instead of jumping:
//
//   for (int i = 0; i < n; ++i) {
//       PushID(items[i].id);
//       Anim::BeginGlide("row");  Selectable(items[i].name);
//       int from, to;
//       if (ReorderItemLive("MYLIST", i, &from, &to)) ReorderApply(items, n, from, to);
//       Anim::EndGlide();
//       PopID();
//   }
inline bool ReorderItemLive(const char* payload_type, int index, int* out_from, int* out_to)
{
    bool moved = false;
    if (BeginDragDropSource(VanGuiDragDropFlags_SourceNoPreviewTooltip)) {
        (void)SetDragDropPayload(payload_type, &index, sizeof(int));   // every frame: the index follows the item
        EndDragDropSource();
    }
    if (BeginDragDropTarget()) {
        if (const VanGuiPayload* pl = AcceptDragDropPayload(payload_type, VanGuiDragDropFlags_AcceptBeforeDelivery | VanGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (pl->Data && pl->DataSize == (int)sizeof(int)) {
                const int from = *static_cast<const int*>(pl->Data);
                if (from != index) {
                    if (out_from) *out_from = from;
                    if (out_to)   *out_to   = index;
                    // The dragged item now sits at `index`; rows later this frame must see that.
                    *static_cast<int*>(const_cast<void*>(pl->Data)) = index;
                    moved = true;
                }
            }
        }
        EndDragDropTarget();
    }
    return moved;
}

// Apply a move produced by ReorderItem to a contiguous array (element type T).
template <class T>
inline void ReorderApply(T* arr, int count, int from, int to)
{
    if (from < 0 || to < 0 || from >= count || to >= count || from == to) return;
    T tmp = arr[from];
    if (from < to) for (int i = from; i < to; ++i) arr[i] = arr[i + 1];
    else           for (int i = from; i > to; --i) arr[i] = arr[i - 1];
    arr[to] = tmp;
}

} // namespace VanGui
