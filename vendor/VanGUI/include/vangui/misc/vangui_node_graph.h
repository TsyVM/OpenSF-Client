#pragma once

//-----------------------------------------------------------------------------
// vangui_node_graph.h  --  Node graph / node editor widget for VanGUI
//
// Usage:
//   1. Create a context once per graph instance:
//        VanGui::VanNodeGraphContext* ctx = VanGui::CreateNodeGraphContext();
//
//   2. Each frame, inside a VanGui window:
//        if (VanGui::BeginNodeGraph("##mygraph", ctx))
//        {
//            // Existing links
//            VanGui::NodeLink(0, 0, 1, 0);
//
//            // Node 0
//            VanGui::BeginNode(0, &node0_pos);
//            VanGui::NodeTitle("My Node");
//            VanGui::NodePin(0, false, "Input");
//            VanGui::NodePin(1, true,  "Output");
//            VanGui::EndNode();
//
//            VanGui::EndNodeGraph();
//        }
//
//   3. Query events after EndNodeGraph():
//        int fn, fp, tn, tp;
//        if (VanGui::IsLinkCreated(&fn, &fp, &tn, &tp)) { ... }
//        if (VanGui::IsLinkDeleted(&fn, &fp, &tn, &tp)) { ... }
//        if (VanGui::IsNodeSelected(0)) { ... }
//
//   4. Destroy when done:
//        VanGui::DestroyNodeGraphContext(ctx);
//
// Notes:
//   - NodePin() must be called between BeginNode/EndNode.
//   - NodeLink() must be called between BeginNodeGraph/EndNodeGraph, outside
//     any Begin/EndNode pair.
//   - Pan: middle mouse drag, or Alt + left mouse drag.
//   - Zoom: mouse wheel.
//   - Delete hovered link: hover it and press Delete.
//-----------------------------------------------------------------------------

#include <vangui/vangui.h>

namespace VanGui
{
    // Opaque per-graph context. One per node graph canvas.
    struct VanNodeGraphContext;

    // Lifecycle
    VANGUI_API VanNodeGraphContext* CreateNodeGraphContext();
    VANGUI_API void                DestroyNodeGraphContext(VanNodeGraphContext* ctx);

    // Canvas
    // Returns true if the canvas is visible and interactive.
    // Pass size = (0,0) to fill the remaining content region.
    VANGUI_API bool BeginNodeGraph(const char* str_id, VanNodeGraphContext* ctx,
                                   const VanVec2& size = VanVec2(0, 0));
    VANGUI_API void EndNodeGraph();

    // Node scope -- call between BeginNodeGraph / EndNodeGraph
    // inout_pos: graph-space position, read on first call and updated as the
    //            node is dragged.
    VANGUI_API void BeginNode(int id, VanVec2* inout_pos);
    VANGUI_API void EndNode();

    // Renders a title bar stripe inside the current node.
    // Call once after BeginNode, before any NodePin calls.
    VANGUI_API void NodeTitle(const char* title);

    // Renders a pin circle on the left (input) or right (output) edge.
    // Returns true the frame a connection is completed or broken on this pin.
    VANGUI_API bool NodePin(int pin_id, bool is_output, const char* label,
                            VanVec4 color = VanVec4(1, 1, 1, 1));

    // Draw a bezier link. Call between BeginNodeGraph/EndNodeGraph, outside
    // any BeginNode/EndNode pair.
    VANGUI_API void NodeLink(int from_node_id, int from_pin_id,
                             int to_node_id,   int to_pin_id,
                             VanVec4 color = VanVec4(1, 1, 1, 1));

    // Result queries -- valid after EndNodeGraph() until the next BeginNodeGraph().
    VANGUI_API bool IsLinkCreated(int* from_node_id, int* from_pin_id,
                                  int* to_node_id,   int* to_pin_id);
    VANGUI_API bool IsLinkDeleted(int* from_node_id, int* from_pin_id,
                                  int* to_node_id,   int* to_pin_id);
    VANGUI_API bool IsNodeSelected(int node_id);

} // namespace VanGui
