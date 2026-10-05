// A clan's own emblem (Rules.hpp ClanMark::layers), drawn: thirty shapes as outlines of points,
// filled or stroked by VanGUI's vector module, and thirty pictures drawn by VanGUI's icons; each
// placed, sized, turned, mirrored and coloured as its layer says (smooth-edged at any size, from
// a sixteen-pixel badge in a list to the emblem maker's canvas). Nothing here is a bitmap: an
// emblem is only ever its shapes' numbers.
#pragma once

#include "Game/Rules.hpp"

#include <vangui/vangui.h>

namespace lsf::ui {

// The emblem into the square a..b (pixels), nothing of it outside. `alpha` 0..1.
void draw_emblem(VanDrawList* dl, const ClanMark& mark, const VanVec2& a, const VanVec2& b, float alpha = 1.0f);
// One shape of it, the emblem's middle at `centre` and its half-width `half` (pixels).
void draw_emblem_layer(VanDrawList* dl, const EmblemLayer& layer, const VanVec2& centre, float half, float alpha = 1.0f);

// The emblem maker's own geometry, in the emblem's units (0..200 across and down).
// Where a point of the emblem lies in a layer's own frame: (-1..1, -1..1) inside its box.
VanVec2 emblem_to_layer(const EmblemLayer& layer, float ex, float ey);
// A point of a layer's frame (its box's corners are (+-1, +-1)) in the emblem.
VanVec2 emblem_from_layer(const EmblemLayer& layer, float u, float v);

}  // namespace lsf::ui
