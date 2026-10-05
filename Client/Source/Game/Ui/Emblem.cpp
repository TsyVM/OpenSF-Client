#include "Game/Ui/Emblem.hpp"

#include <vangui/misc/vangui_icons.h>
#include <vangui/misc/vangui_vector.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lsf::ui {

namespace {

constexpr float kPi = 3.14159265f;

// A shape in its own frame: x right and y down, each -1..1. Its contours are closed and filled
// together (one inside another is a hole); its lines are open and stroked (the snowflake).
struct Geo {
    std::vector<std::vector<VanVec2>> contours;
    std::vector<std::vector<VanVec2>> lines;
};

std::vector<VanVec2> ngon(int sides, float radius, float start_deg = -90.0f) {
    std::vector<VanVec2> p;
    for (int k = 0; k < sides; ++k) {
        const float a = (start_deg + 360.0f * float(k) / float(sides)) * kPi / 180.0f;
        p.push_back({std::cos(a) * radius, std::sin(a) * radius});
    }
    return p;
}

// Points round a star: outer and inner radii by turns, the first point up.
std::vector<VanVec2> star(int points, float inner) {
    std::vector<VanVec2> p;
    for (int k = 0; k < points * 2; ++k) {
        const float a = (-90.0f + 180.0f * float(k) / float(points)) * kPi / 180.0f;
        const float r = k % 2 == 0 ? 1.0f : inner;
        p.push_back({std::cos(a) * r, std::sin(a) * r});
    }
    return p;
}

void arc(std::vector<VanVec2>& out, float cx, float cy, float r, float from_deg, float to_deg, int steps) {
    for (int k = 0; k <= steps; ++k) {
        const float a = (from_deg + (to_deg - from_deg) * float(k) / float(steps)) * kPi / 180.0f;
        out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
}

// A shape stretched to fill its frame, whatever its own outline's extent was.
void fit(Geo& g) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const auto* set : {&g.contours, &g.lines})
        for (const auto& c : *set)
            for (const VanVec2& p : c) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
    if (x1 - x0 < 1e-4f || y1 - y0 < 1e-4f) return;
    for (auto* set : {&g.contours, &g.lines})
        for (auto& c : *set)
            for (VanVec2& p : c) p = {(p.x - x0) / (x1 - x0) * 2 - 1, (p.y - y0) / (y1 - y0) * 2 - 1};
}

Geo make(EmblemShape shape) {
    Geo g;
    auto one = [&](std::vector<VanVec2> c) { g.contours.push_back(std::move(c)); };
    switch (shape) {
        case EmblemShape::Circle: one(ngon(48, 1)); break;
        case EmblemShape::Ring:
            one(ngon(48, 1));
            one(ngon(40, 0.64f));
            break;
        case EmblemShape::HalfCircle: {
            std::vector<VanVec2> c;
            arc(c, 0, 0, 1, 180, 360, 28);
            one(c);
            fit(g);
            break;
        }
        case EmblemShape::Quarter: {
            std::vector<VanVec2> c{{-1, 1}};
            arc(c, -1, 1, 2, 270, 360, 20);
            one(c);
            break;
        }
        case EmblemShape::Square: one({{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}); break;
        case EmblemShape::RoundedSquare: {
            std::vector<VanVec2> c;
            const float r = 0.38f, k = 1 - r;
            arc(c, k, -k, r, 270, 360, 7);
            arc(c, k, k, r, 0, 90, 7);
            arc(c, -k, k, r, 90, 180, 7);
            arc(c, -k, -k, r, 180, 270, 7);
            one(c);
            break;
        }
        case EmblemShape::Frame:
            one({{-1, -1}, {1, -1}, {1, 1}, {-1, 1}});
            one({{-0.66f, -0.66f}, {0.66f, -0.66f}, {0.66f, 0.66f}, {-0.66f, 0.66f}});
            break;
        case EmblemShape::Triangle: one({{0, -1}, {1, 1}, {-1, 1}}); break;
        case EmblemShape::RightTriangle: one({{-1, -1}, {1, 1}, {-1, 1}}); break;
        case EmblemShape::Trapezoid: one({{-0.55f, -1}, {0.55f, -1}, {1, 1}, {-1, 1}}); break;
        case EmblemShape::Parallelogram: one({{-0.45f, -1}, {1, -1}, {0.45f, 1}, {-1, 1}}); break;
        case EmblemShape::Diamond: one({{0, -1}, {1, 0}, {0, 1}, {-1, 0}}); break;
        case EmblemShape::Pentagon:
            one(ngon(5, 1));
            fit(g);
            break;
        case EmblemShape::Hexagon:
            one(ngon(6, 1));
            fit(g);
            break;
        case EmblemShape::Octagon:
            one(ngon(8, 1, -67.5f));
            fit(g);
            break;
        case EmblemShape::Star:
            one(star(5, 0.42f));
            fit(g);
            break;
        case EmblemShape::Star4: one(star(4, 0.30f)); break;
        case EmblemShape::Burst: one(star(12, 0.72f)); break;
        case EmblemShape::Cross: {
            const float t = 0.34f;
            one({{-t, -1}, {t, -1}, {t, -t}, {1, -t}, {1, t}, {t, t}, {t, 1}, {-t, 1}, {-t, t}, {-1, t}, {-1, -t}, {-t, -t}});
            break;
        }
        case EmblemShape::Line: one({{-1, -0.16f}, {1, -0.16f}, {1, 0.16f}, {-1, 0.16f}}); break;
        case EmblemShape::Chevron: one({{-1, 0.25f}, {0, -1}, {1, 0.25f}, {1, 1}, {0, -0.25f}, {-1, 1}}); break;
        case EmblemShape::Arrow: one({{0, -1}, {1, 0}, {0.4f, 0}, {0.4f, 1}, {-0.4f, 1}, {-0.4f, 0}, {-1, 0}}); break;
        case EmblemShape::Crescent: {
            // A disc with a smaller one, set to its right, taken out of it.
            std::vector<VanVec2> c;
            arc(c, 0, 0, 1, 51.6f, 308.4f, 34);
            arc(c, 0.38f, 0, 0.82f, 287.1f, 72.9f, 26);
            one(c);
            fit(g);
            break;
        }
        case EmblemShape::Shield: {
            std::vector<VanVec2> c{{-1, -1}, {1, -1}, {1, 0.05f}};
            // Each side curves in to the point at the foot (a quadratic, sampled).
            for (int k = 1; k <= 12; ++k) {
                const float t = float(k) / 12.0f, u = 1 - t;
                c.push_back({u * u * 1 + 2 * u * t * 0.95f + t * t * 0, u * u * 0.05f + 2 * u * t * 0.78f + t * t * 1});
            }
            for (int k = 1; k <= 11; ++k) {
                const float t = float(k) / 12.0f, u = 1 - t;
                c.push_back({-(t * t * 1 + 2 * u * t * 0.95f), t * t * 0.05f + 2 * u * t * 0.78f + u * u * 1});
            }
            c.push_back({-1, 0.05f});
            one(c);
            break;
        }
        case EmblemShape::Snowflake:
            // Six arms, each with two pairs of barbs swept out toward its tip.
            for (int arm = 0; arm < 6; ++arm) {
                const float a = (-90.0f + 60.0f * float(arm)) * kPi / 180.0f;
                const VanVec2 d{std::cos(a), std::sin(a)};
                g.lines.push_back({{0, 0}, {d.x * 0.97f, d.y * 0.97f}});
                for (const auto& [at, len] : {std::pair{0.40f, 0.30f}, std::pair{0.66f, 0.22f}})
                    for (int side : {-1, 1}) {
                        const float b = a + float(side) * 42.0f * kPi / 180.0f;
                        g.lines.push_back({{d.x * at, d.y * at}, {d.x * at + std::cos(b) * len, d.y * at + std::sin(b) * len}});
                    }
            }
            break;
        case EmblemShape::Bolt: one({{0.3f, -1}, {-0.75f, 0.12f}, {-0.08f, 0.12f}, {-0.35f, 1}, {0.75f, -0.22f}, {0.06f, -0.22f}}); break;
        case EmblemShape::Heart: {
            std::vector<VanVec2> c;
            for (int k = 0; k < 48; ++k) {
                const float t = 2 * kPi * float(k) / 48.0f, s = std::sin(t);
                c.push_back({16 * s * s * s, -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t))});
            }
            one(c);
            fit(g);
            break;
        }
        case EmblemShape::Crown: one({{-1, 1}, {-1, -0.55f}, {-0.5f, 0.1f}, {0, -1}, {0.5f, 0.1f}, {1, -0.55f}, {1, 1}}); break;
        case EmblemShape::Gear: {
            // Eight flat-topped teeth round a wheel with a hole in it.
            std::vector<VanVec2> c;
            for (int k = 0; k < 8; ++k) {
                const float base = 45.0f * float(k) - 90.0f;
                for (const auto& [off, r] : {std::pair{-11.0f, 0.76f}, std::pair{-7.5f, 1.0f}, std::pair{7.5f, 1.0f}, std::pair{11.0f, 0.76f}}) {
                    const float a = (base + off) * kPi / 180.0f;
                    c.push_back({std::cos(a) * r, std::sin(a) * r});
                }
            }
            one(c);
            one(ngon(24, 0.34f));
            break;
        }
        case EmblemShape::Drop: {
            // A point above a round body.
            std::vector<VanVec2> c{{0, -1}};
            arc(c, 0, 0.38f, 0.62f, -32, 212, 30);
            one(c);
            fit(g);
            break;
        }
        default: one(ngon(48, 1)); break;
    }
    return g;
}

const Geo& geo(u8 shape) {
    static std::vector<Geo> all;
    if (all.empty())
        for (int k = 0; k < kEmblemFirstPicture; ++k) all.push_back(make(EmblemShape(k)));
    return all[shape < all.size() ? shape : 0];
}

// The pictures, in EmblemShape's order from Skull: VanGUI's own icons.
using namespace VanGui;
constexpr VanIconID kPictures[] = {
    VanIcon_Skull,     VanIcon_Fire,  VanIcon_Bomb,  VanIcon_Knife,  VanIcon_Rifle,  VanIcon_Sniper,   VanIcon_Pistol, VanIcon_Shotgun,
    VanIcon_MachineGun, VanIcon_GrenadeItem, VanIcon_C4, VanIcon_Crosshair, VanIcon_Scope, VanIcon_Explosion, VanIcon_Helmet, VanIcon_Vest,
    VanIcon_Medal,     VanIcon_Trophy, VanIcon_Flag, VanIcon_Rocket, VanIcon_Globe,  VanIcon_Sun,      VanIcon_Moon,   VanIcon_Cloud,
    VanIcon_Tree,      VanIcon_Gem,   VanIcon_Key,   VanIcon_Eye,    VanIcon_Mask,   VanIcon_Compass};
static_assert(std::size(kPictures) == size_t(EmblemShape::Count) - kEmblemFirstPicture);

struct Frame {
    float sx, sy, cs, sn, tx, ty;   // a layer's frame into the emblem's half-units (-1..1)
};
Frame frame_of(const EmblemLayer& l) {
    const float a = float(l.turn) * kPi / 180.0f;
    return {float(l.w) / 200.0f * (l.mirrored() ? -1.0f : 1.0f), float(l.h) / 200.0f, std::cos(a), std::sin(a), (float(l.x) - 100.0f) / 100.0f,
            (float(l.y) - 100.0f) / 100.0f};
}
VanVec2 place(const Frame& f, const VanVec2& p) {
    const float x = p.x * f.sx, y = p.y * f.sy;
    return {x * f.cs - y * f.sn + f.tx, x * f.sn + y * f.cs + f.ty};
}

}  // namespace

VanVec2 emblem_from_layer(const EmblemLayer& layer, float u, float v) {
    const VanVec2 p = place(frame_of(layer), {u, v});
    return {p.x * 100.0f + 100.0f, p.y * 100.0f + 100.0f};
}

VanVec2 emblem_to_layer(const EmblemLayer& layer, float ex, float ey) {
    const Frame f = frame_of(layer);
    const float x = (ex - 100.0f) / 100.0f - f.tx, y = (ey - 100.0f) / 100.0f - f.ty;
    return {(x * f.cs + y * f.sn) / f.sx, (-x * f.sn + y * f.cs) / f.sy};
}

void draw_emblem_layer(VanDrawList* dl, const EmblemLayer& layer, const VanVec2& centre, float half, float alpha) {
    const Frame f = frame_of(layer);
    const float opacity = std::clamp(alpha, 0.0f, 1.0f) * float(layer.a) / 255.0f;
    const VanU32 col = VAN_COL32(layer.r, layer.g, layer.b, int(opacity * 255.0f));
    // An outline's weight goes with the shape's own size (and never thinner than a pixel can show).
    const float size = float(std::min(layer.w, layer.h)) / 100.0f * half;
    const float weight = std::max(1.0f, size * (0.05f + 0.022f * float(layer.weight())));

    if (layer.shape >= kEmblemFirstPicture && layer.shape < u8(EmblemShape::Count)) {
        // A picture: VanGUI draws the icon square at the larger of the layer's two sides, then
        // every point it made is carried into the layer's frame (squeezed to its shape, mirrored,
        // turned, placed), so a picture does everything a shape does. Filled, its details are
        // cut in a darker shade of its colour; outlined, it is the icon's own line drawing.
        const float k = std::max(std::fabs(f.sx), std::fabs(f.sy));
        if (k <= 0.0f) return;
        const float px = half * k;   // the icon's half-size in pixels
        VanIconParams p;
        p.Color = col;
        p.Secondary = VAN_COL32(layer.r * 2 / 7, layer.g * 2 / 7, layer.b * 2 / 7, int(opacity * 255.0f));
        p.Style = layer.outline() ? VanIconStyle_Outline : VanIconStyle_Filled;
        p.Thickness = std::max(1.0f, px * 2.0f / 24.0f * (1.2f + 0.35f * float(layer.weight())));
        const int first = dl->VtxBuffer.Size;
        VanGui::DrawIconEx(dl, kPictures[layer.shape - kEmblemFirstPicture], centre, px * 2.0f, p);
        const Frame unit{f.sx / k, f.sy / k, f.cs, f.sn, f.tx / k, f.ty / k};
        for (int v = first; v < dl->VtxBuffer.Size; ++v) {
            VanVec2& pos = dl->VtxBuffer[v].pos;
            const VanVec2 q = place(unit, {(pos.x - centre.x) / px, (pos.y - centre.y) / px});
            pos = {centre.x + q.x * px, centre.y + q.y * px};
        }
        return;
    }

    const Geo& g = geo(layer.shape);
    static std::vector<std::vector<VanVec2>> placed;
    static std::vector<const VanVec2*> starts;
    static std::vector<int> counts;
    auto put = [&](const std::vector<std::vector<VanVec2>>& set) {
        placed.resize(set.size());
        for (size_t c = 0; c < set.size(); ++c) {
            placed[c].resize(set[c].size());
            for (size_t k = 0; k < set[c].size(); ++k) {
                const VanVec2 p = place(f, set[c][k]);
                placed[c][k] = {centre.x + p.x * half, centre.y + p.y * half};
            }
        }
    };
    if (!g.lines.empty()) {
        put(g.lines);
        const VanGui::VanStroke stroke(col, std::max(1.0f, size * (0.035f + 0.014f * float(layer.weight()))), VanGui::VanVectorCap_Round,
                                       VanGui::VanVectorJoin_Round);
        for (const auto& line : placed) VanGui::StrokePath(dl, line.data(), int(line.size()), stroke, false);
        return;
    }
    put(g.contours);
    if (layer.outline()) {
        const VanGui::VanStroke stroke(col, weight, VanGui::VanVectorCap_Butt, VanGui::VanVectorJoin_Miter);
        for (const auto& c : placed) VanGui::StrokePath(dl, c.data(), int(c.size()), stroke, true);
    } else if (placed.size() == 1) {
        VanGui::FillPath(dl, placed[0].data(), int(placed[0].size()), col);
    } else {
        starts.clear(), counts.clear();
        for (const auto& c : placed) starts.push_back(c.data()), counts.push_back(int(c.size()));
        VanGui::FillContours(dl, starts.data(), counts.data(), int(starts.size()), col, VanGui::VanFillRule_EvenOdd);
    }
}

void draw_emblem(VanDrawList* dl, const ClanMark& mark, const VanVec2& a, const VanVec2& b, float alpha) {
    if (mark.layers.empty()) return;
    const VanVec2 centre{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    const float half = std::min(b.x - a.x, b.y - a.y) * 0.5f;
    dl->PushClipRect(a, b, true);
    for (const EmblemLayer& l : mark.layers) draw_emblem_layer(dl, l, centre, half, alpha);
    dl->PopClipRect();
}

}  // namespace lsf::ui
