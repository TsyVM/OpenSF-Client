// The crosshair as the player shaped it (Settings.hpp Crosshair), drawn the same way in the match
// and in the options' preview.
#pragma once

#include "Game/Settings.hpp"

#include <vangui/vangui.h>

namespace lsf::ui {

// `unit` is a screen unit in pixels (a 900-high screen's pixel: ui::px(1) on the match's stage).
// `open` is how far the cone has pushed the ticks out, in screen units (0: at rest); a crosshair
// that stands still ignores it. `wide` is how much wider than drawn the picture is across (a 4:3
// picture pulled over a wide screen): the side ticks stand out by as much, as the cone does.
void draw_crosshair(VanDrawList* dl, const VanVec2& centre, float unit, const Crosshair& c, float open = 0.0f, float wide = 1.0f);

// A screen unit in pixels as the match's own stage has it, for a preview drawn outside a match.
float crosshair_unit();

}  // namespace lsf::ui
