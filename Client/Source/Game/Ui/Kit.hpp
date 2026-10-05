// The 2010 kit, redrawn. Soldier Front's lobby art is bitmaps made for a 1024 x 768 screen: a
// plate 73 pixels wide with its word painted on. Pulled over a screen three times that size the
// word goes soft. So the pieces of the kit that are a plate, a frame and a word (the nav row, the
// buttons, the tabs, the small Buy / Sell / Use plates, the arrows, the checks and the dials) are
// drawn here instead, at the screen's own size: the same plate and frame colours measured off the
// kit's sheet (sourceNewAlpha03), the same words, the same lit and pressed states, the lettering
// set in the nearest face Windows has to the kit's own (Malgun Gothic Bold, tracked and outlined
// as the kit's is), and the little pictures beside some words drawn as VanGUI icons.
//
// Pictures stay pictures: the backdrop, the mark, portraits, weapons, emblems, the team bars.
// The options have a switch (Game > Sharp interface) that puts the kit's own bitmaps back.
#pragma once

#include <vangui/vangui.h>

#include <string_view>

namespace lsf::ui {

bool kit_sharp();
void set_kit_sharp(bool on);

// `sprite` in the state asked for (`state` of `states`, as the kit stacks them), into the pixel
// rect a..b. `unit` is a page unit's height in pixels (line weights and lettering follow it);
// `tint` fades it, as it fades the bitmap. False when the kit's own bitmap should be drawn: a
// piece with no drawing here, or the switch is off.
bool kit_draw(VanDrawList* dl, std::string_view sprite, int state, int states, const VanVec2& a, const VanVec2& b, VanU32 tint, float unit);

// The kit's lettering anywhere, centred on a point: tracked, with the kit's dark outline. A '\n'
// stacks two lines. Shrinks to `fit_px` across when it would be wider (0: no limit).
void kit_label(VanDrawList* dl, std::string_view text, const VanVec2& centre, float size_px, VanU32 ink, float fit_px = 0);
// The same from a left edge, on one line, its middle at `left.y`. Returns the width drawn.
float kit_label_left(VanDrawList* dl, std::string_view text, const VanVec2& left, float size_px, VanU32 ink);
// A plate with a word on it in the kit's colours: a tab (lit while `chosen`) or a button.
void kit_plate(VanDrawList* dl, const VanVec2& a, const VanVec2& b, float unit, std::string_view label, bool chosen, bool hovered, bool pressed = false);

}  // namespace lsf::ui
