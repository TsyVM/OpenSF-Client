// The radar, top left. Soldier Front shipped none and no map carries an overhead picture, so the
// floorplan is baked from the map's own collision at load (TacticalFPS's Radar.cpp, the same two
// rules that make it readable):
//
//   - each texel keeps the LOWEST floor over it, not the highest: roofs are floors too, and
//     keeping them paints every building solid and hides the streets, which are the map;
//   - walls are drawn as their three edges, not scan-converted as faces: seen from above a wall is
//     a sliver thinner than a texel and a face raster drops it, and walls are what make a plan.
//
// Floors are shaded by height against the floors actually kept (not the collision box, which
// reaches the tallest roof and leaves everything one dark grey).
//
// The dial turns with you (forward is up) and shows teammates, you, and an enemy only for the
// moment after he fires: the snapshot carries everyone, so what the radar may show is decided here.
#include "Game/World/GameWorld.hpp"

#include "Game/App.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Log.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lsf {

using eng::Vec3;

namespace {

constexpr eng::u32 kTexels = 512;
constexpr float kFloorNormalY = 0.5f;
constexpr float kWallNormalY = 0.35f;
constexpr float kRangeCm = 3500.0f;   // what the dial's rim is from its centre

template <typename Plot>
void raster_line(float ax, float ay, float bx, float by, int w, int h, Plot plot) {
    const float dx = bx - ax, dy = by - ay;
    const int steps = int(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))) + 1;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const int x = int(ax + dx * t), y = int(ay + dy * t);
        if (x >= 0 && y >= 0 && x < w && y < h) plot(x, y);
    }
}

template <typename Plot>
void raster_triangle(float ax, float ay, float bx, float by, float cx, float cy, int w, int h, Plot plot) {
    const int x0 = std::max(0, int(std::floor(std::min({ax, bx, cx}))));
    const int x1 = std::min(w - 1, int(std::ceil(std::max({ax, bx, cx}))));
    const int y0 = std::max(0, int(std::floor(std::min({ay, by, cy}))));
    const int y1 = std::min(h - 1, int(std::ceil(std::max({ay, by, cy}))));
    if (x1 < x0 || y1 < y0) return;
    const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::fabs(area) < 1e-6f) return;
    const float inv = 1.0f / area;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;
            const float w0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) * inv;
            const float w1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) * inv;
            const float w2 = ((ax - cx) * (py - cy) - (ay - cy) * (px - cx)) * inv;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            plot(x, y);
        }
}

}  // namespace

void GameWorld::bake_radar(const sf::Level& level) {
    radar_image_ = eng::Image{};
    const std::vector<Vec3>& verts = level.collision_vertices;
    const std::vector<eng::u32>& idx = level.collision_indices;
    if (verts.empty() || idx.size() < 3) return;
    Vec3 lo = verts[0], hi = verts[0];
    for (const Vec3& v : verts) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    const float span_x = std::max(1.0f, hi.x - lo.x), span_z = std::max(1.0f, hi.z - lo.z);
    const float cm = std::max(span_x, span_z) / float(kTexels);
    const int w = std::clamp(int(std::ceil(span_x / cm)), 16, int(kTexels));
    const int h = std::clamp(int(std::ceil(span_z / cm)), 16, int(kTexels));
    std::vector<float> height(size_t(w) * size_t(h), 1e30f);
    std::vector<eng::u8> wall(size_t(w) * size_t(h), 0);
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        if (idx[t] >= verts.size() || idx[t + 1] >= verts.size() || idx[t + 2] >= verts.size()) continue;
        const Vec3& a = verts[idx[t]];
        const Vec3& b = verts[idx[t + 1]];
        const Vec3& c = verts[idx[t + 2]];
        const Vec3 n = eng::normalize(eng::cross(b - a, c - a));
        const float ax = (a.x - lo.x) / cm, az = (a.z - lo.z) / cm;
        const float bx = (b.x - lo.x) / cm, bz = (b.z - lo.z) / cm;
        const float cx = (c.x - lo.x) / cm, cz = (c.z - lo.z) / cm;
        if (std::fabs(n.y) > kFloorNormalY) {
            const float y = (a.y + b.y + c.y) / 3.0f;
            raster_triangle(ax, az, bx, bz, cx, cz, w, h, [&](int x, int z) {
                float& cell = height[size_t(z) * size_t(w) + size_t(x)];
                if (y < cell) cell = y;
            });
        } else if (std::fabs(n.y) < kWallNormalY) {
            auto paint = [&](int x, int z) { wall[size_t(z) * size_t(w) + size_t(x)] = 1; };
            raster_line(ax, az, bx, bz, w, h, paint);
            raster_line(bx, bz, cx, cz, w, h, paint);
            raster_line(cx, cz, ax, az, w, h, paint);
        }
    }
    float low = 1e30f, high = -1e30f;
    for (float y : height)
        if (y < 1e29f) low = std::min(low, y), high = std::max(high, y);
    const float shade_span = std::max(1.0f, high - low);
    eng::Image img;
    img.width = eng::u32(w);
    img.height = eng::u32(h);
    img.rgba.assign(size_t(w) * size_t(h) * 4, 0);
    size_t painted = 0;
    for (size_t i = 0; i < height.size(); ++i) {
        const bool floor = height[i] < 1e29f;
        if (!floor && !wall[i]) continue;
        ++painted;
        eng::u8* p = &img.rgba[i * 4];
        if (wall[i] && !floor) {
            p[0] = 26, p[1] = 30, p[2] = 24, p[3] = 235;
            continue;
        }
        const float t = high - low > 50.0f ? std::clamp((height[i] - low) / shade_span, 0.0f, 1.0f) : 0.5f;
        const eng::u8 g = eng::u8(76.0f + 104.0f * t);
        // The kit's olive rather than TacticalFPS's blue-grey.
        if (wall[i]) p[0] = eng::u8(g / 3 + 6), p[1] = eng::u8(g / 3 + 8), p[2] = eng::u8(g / 3), p[3] = 245;
        else p[0] = eng::u8(g + 8), p[1] = eng::u8(g + 10), p[2] = g, p[3] = 205;
    }
    if (!painted) return;
    radar_image_ = std::move(img);
    radar_origin_ = lo;
    radar_w_ = float(w) * cm;
    radar_h_ = float(h) * cm;
    LOG_INFO("Radar: %dx%d floorplan at %.0f cm a texel", w, h, double(cm));
}

void GameWorld::draw_radar(double now) {
    if (!radar_pic_.valid() || radar_w_ <= 0) return;
    const ui::Palette& pal = ui::pal();
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const float R = 88;   // stage units
    const VanVec2 c = ui::stage(18 + R, 18 + R);
    const float r = ui::px(R);
    const Vec3 eye = move_.origin;
    const float th = yaw_ * eng::kDegToRad;
    const float cs = std::cos(th), sn = std::sin(th);
    const float ppc = r / kRangeCm;   // pixels a centimetre
    // World -> dial: right of you is +x on screen, ahead of you is up.
    auto to_dial = [&](const Vec3& p) {
        const float dx = p.x - eye.x, dz = p.z - eye.z;
        const float rx = dx * cs - dz * sn, fz = dx * sn + dz * cs;
        return VanVec2(rx * ppc, -fz * ppc);
    };
    // Dial -> the floorplan's texture coordinates.
    auto uv_of = [&](float sx, float sy) {
        const float rx = sx / ppc, fz = -sy / ppc;
        const float dx = rx * cs + fz * sn, dz = -rx * sn + fz * cs;
        return VanVec2((eye.x + dx - radar_origin_.x) / radar_w_, (eye.z + dz - radar_origin_.z) / radar_h_);
    };

    dl->AddCircleFilled(c, r + ui::px(3), VAN_COL32(0, 0, 0, 150), 64);
    dl->AddCircleFilled(c, r, VAN_COL32(14, 16, 10, 215), 64);
    {
        // A textured fan clipped to the circle (VanGUI clips to rectangles only).
        constexpr int kSegments = 64;
        const VanU32 tint = VAN_COL32(255, 255, 255, 235);
        dl->PushTextureID(VanTextureRef(VanTextureID(radar_pic_.tex)));
        dl->PrimReserve(kSegments * 3, kSegments + 1);
        const unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(c, uv_of(0, 0), tint);
        for (int i = 0; i < kSegments; ++i) {
            const float a = float(i) / kSegments * 6.2831853f;
            const float sx = std::cos(a) * r, sy = std::sin(a) * r;
            dl->PrimWriteVtx(VanVec2(c.x + sx, c.y + sy), uv_of(sx, sy), tint);
        }
        for (int i = 0; i < kSegments; ++i) {
            dl->PrimWriteIdx(VanDrawIdx(base));
            dl->PrimWriteIdx(VanDrawIdx(base + 1 + unsigned(i)));
            dl->PrimWriteIdx(VanDrawIdx(base + 1 + unsigned((i + 1) % kSegments)));
        }
        dl->PopTextureID();
    }
    // A sweep, the way a radar reads.
    {
        const float sweep = float(std::fmod(now * 1.4, 6.2831853));
        for (int k = 0; k < 10; ++k) {
            const float a0 = sweep - 0.06f * float(k), a1 = a0 - 0.06f;
            dl->AddTriangleFilled(c, VanVec2(c.x + std::cos(a0) * r, c.y + std::sin(a0) * r), VanVec2(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r),
                                  VAN_COL32(150, 220, 90, 26 - k * 2));
        }
    }

    // Everyone else: teammates always, an enemy for two seconds after he fires.
    for (const auto& [id, p] : players_) {
        if (id == me_ || !p.alive || p.hidden) continue;
        const bool mate = mode_info(settings_.mode).teams && p.team == team_;
        const float since_fired = float(now - p.fired_at);
        if (!mate && since_fired > 2.0f) continue;
        const Vec3 at = p.position;
        const VanVec2 o = to_dial(at);
        const float d = std::sqrt(o.x * o.x + o.y * o.y);
        VanVec2 dot(c.x + o.x, c.y + o.y);
        if (d > r - ui::px(4)) {
            if (!mate) continue;   // an enemy out of range is not on the dial
            const float k = (r - ui::px(4)) / d;
            dot = VanVec2(c.x + o.x * k, c.y + o.y * k);
        }
        const VanU32 col = mate ? ui::col(ui::team_colour(u8(p.team))) : VAN_COL32(255, 60, 40, int(255 * std::clamp(2.0f - since_fired, 0.0f, 1.0f)));
        const float dy = at.y - eye.y;
        if (mate) {
            const float fa = (p.yaw - yaw_) * eng::kDegToRad;
            dl->AddLine(dot, VanVec2(dot.x + std::sin(fa) * ui::px(9), dot.y - std::cos(fa) * ui::px(9)), VAN_COL32(255, 255, 255, 110), 1.5f);
        }
        if (std::fabs(dy) > 180.0f) {   // a storey away: hollow
            dl->AddCircleFilled(dot, ui::px(4.5f), VAN_COL32(8, 10, 6, 220), 10);
            dl->AddCircle(dot, ui::px(4.5f), col, 10, 1.8f);
        } else {
            dl->AddCircleFilled(dot, ui::px(4.0f), col, 10);
            dl->AddCircle(dot, ui::px(4.0f), VAN_COL32(8, 10, 6, 200), 10, 1.0f);
        }
    }

    // The game type's: sites, the bomb, the item, zones, consoles, strongholds; captains and searched humans.
    draw_radar_objectives(dl, c, r, to_dial);

    // You, pointing up, with what you can see.
    const float fov = std::clamp(app_.settings().fov, 40.0f, 110.0f) * 0.5f * eng::kDegToRad;
    const float reach = ui::px(28);
    dl->AddTriangleFilled(c, VanVec2(c.x - std::sin(fov) * reach, c.y - std::cos(fov) * reach), VanVec2(c.x + std::sin(fov) * reach, c.y - std::cos(fov) * reach),
                          VAN_COL32(255, 255, 255, 34));
    dl->AddTriangleFilled(VanVec2(c.x, c.y - ui::px(6)), VanVec2(c.x - ui::px(4), c.y + ui::px(4)), VanVec2(c.x + ui::px(4), c.y + ui::px(4)),
                          alive_ ? VAN_COL32(255, 255, 255, 255) : ui::col(pal.text_dim, 0.8f));
    dl->AddCircle(c, r, ui::col(pal.gold, 0.85f), 64, 1.6f);
    // North, turning with you.
    const VanVec2 n = to_dial(eye + Vec3{0, 0, kRangeCm});
    const float nl = std::sqrt(n.x * n.x + n.y * n.y);
    if (nl > 1.0f) {
        const VanVec2 at(c.x + n.x / nl * (r - ui::px(9)), c.y + n.y / nl * (r - ui::px(9)));
        VanFont* f = ui::font_bold();
        const VanVec2 ts = f->CalcTextSizeA(ui::px(11), FLT_MAX, 0.0f, "N");
        dl->AddText(f, ui::px(11), VanVec2(at.x - ts.x * 0.5f, at.y - ts.y * 0.5f), ui::col(pal.gold_bright), "N");
    }
}

}  // namespace lsf
