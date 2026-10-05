#include "Game/Ui/Crosshair.hpp"

#include "Game/Ui/Ui.hpp"

#include <algorithm>
#include <cmath>

namespace lsf::ui {

float crosshair_unit() { return std::max(0.5f, view().h / kStageH); }

void draw_crosshair(VanDrawList* dl, const VanVec2& centre, float unit, const Crosshair& c, float open, float wide) {
    const VanU32 ink = VAN_COL32(c.r, c.g, c.b, c.a);
    const VanU32 edge = VAN_COL32(0, 0, 0, c.a * 170 / 255);
    // Whole pixels, about a centre on a pixel's corner: a tick is as sharp as the screen is.
    const VanVec2 o{std::floor(centre.x + 0.5f), std::floor(centre.y + 0.5f)};
    const float t = std::max(1.0f, std::floor(c.thickness * unit + 0.5f));
    const float h0 = std::floor(t * 0.5f), h1 = t - h0;
    const float gap = (c.gap + (c.moves ? open : 0.0f)) * unit;
    if (c.lines) {
        const float len = std::max(1.0f, std::floor(c.length * unit + 0.5f));
        const float gy = std::floor(gap + 0.5f), gx = std::floor(gap * wide + 0.5f);
        struct Tick {
            VanVec2 a, b;
        };
        const Tick ticks[4] = {
            {{o.x + gx, o.y - h0}, {o.x + gx + len, o.y + h1}},      // right
            {{o.x - gx - len, o.y - h0}, {o.x - gx, o.y + h1}},      // left
            {{o.x - h0, o.y + gy}, {o.x + h1, o.y + gy + len}},      // lower
            {{o.x - h0, o.y - gy - len}, {o.x + h1, o.y - gy}},      // upper
        };
        const int n = c.top ? 4 : 3;
        if (c.outline)
            for (int i = 0; i < n; ++i) dl->AddRectFilled({ticks[i].a.x - 1, ticks[i].a.y - 1}, {ticks[i].b.x + 1, ticks[i].b.y + 1}, edge);
        for (int i = 0; i < n; ++i) dl->AddRectFilled(ticks[i].a, ticks[i].b, ink);
    }
    if (c.ring) {
        const float r = std::max(1.5f, gap * (1.0f + wide) * 0.5f + c.ring_size * unit);
        const int segments = std::clamp(int(r * 0.9f) + 16, 24, 96);
        if (c.outline) dl->AddCircle(o, r, edge, segments, t + 2.0f);
        dl->AddCircle(o, r, ink, segments, t);
    }
    if (c.dot) {
        const float r = std::max(1.0f, c.dot_size * unit);
        if (c.outline) dl->AddCircleFilled(o, r + 1.0f, edge, 20);
        dl->AddCircleFilled(o, r, ink, 20);
    }
}

}  // namespace lsf::ui
