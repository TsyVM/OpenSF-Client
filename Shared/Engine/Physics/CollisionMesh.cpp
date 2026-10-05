#include "Engine/Physics/CollisionMesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eng {

namespace {

// A node is split while splitting pays by the surface area heuristic; below kLeafSize it never
// does, and above kMaxLeaf it always is, however badly.
constexpr u32 kLeafSize = 4;
constexpr u32 kMaxLeaf = 12;
constexpr int kMaxDepth = 60;
constexpr int kBins = 16;
// A triangle test costs about this many node tests.
constexpr float kTriangleCost = 1.5f;
// A node is only skipped for starting beyond the closest hit by more than this share of the move,
// so float error in the box test never costs a hit the triangle test would have found.
constexpr float kPruneSlack = 1e-5f;
constexpr float kContactTolerance = 0.01f;

float surface_area(const Aabb& b) {
    if (!b.valid()) return 0.0f;
    const Vec3 e = b.max - b.min;
    return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
}

// Result of sweeping a box against one triangle.
struct SweepHit {
    bool hit = false;
    bool start_solid = false;
    float t = 1.0f;
    Vec3 normal;
};

SweepHit sweep_box_triangle(const Vec3& c0, const Vec3& d, const Vec3& h, const Vec3& a, const Vec3& b, const Vec3& c,
                            const Vec3& tri_normal) {
    SweepHit out;
    const Vec3 p0 = a - c0, p1 = b - c0, p2 = c - c0;
    const Vec3 e0 = b - a, e1 = c - b, e2 = a - c;

    Vec3 axes[13];
    int n = 0;
    axes[n++] = {1, 0, 0};
    axes[n++] = {0, 1, 0};
    axes[n++] = {0, 0, 1};
    axes[n++] = tri_normal;
    const Vec3 edges[3] = {e0, e1, e2};
    for (const Vec3& e : edges) {
        Vec3 cands[3] = {{0, -e.z, e.y}, {e.z, 0, -e.x}, {-e.y, e.x, 0}};
        for (const Vec3& ax : cands) {
            float len = length(ax);
            if (len > 1e-5f) axes[n++] = ax / len;
        }
    }

    float t_enter = -std::numeric_limits<float>::infinity();
    float t_exit = std::numeric_limits<float>::infinity();
    Vec3 enter_normal{0, 1, 0};
    float min_penetration = std::numeric_limits<float>::infinity();
    Vec3 min_pen_axis{0, 1, 0};

    for (int i = 0; i < n; ++i) {
        const Vec3& ax = axes[i];
        float r = h.x * std::fabs(ax.x) + h.y * std::fabs(ax.y) + h.z * std::fabs(ax.z) + CollisionMesh::kSkin;
        float q0 = dot(p0, ax), q1 = dot(p1, ax), q2 = dot(p2, ax);
        float tmin = std::min({q0, q1, q2});
        float tmax = std::max({q0, q1, q2});
        float v = dot(d, ax);

        float gap_above = tmin - r;    // triangle lies on the +axis side of the box
        float gap_below = -r - tmax;   // triangle lies on the -axis side

        float enter, exit;
        if (v > 1e-7f) {
            if (gap_below > -kContactTolerance) return out;   // behind and moving away
            enter = (gap_above > -kContactTolerance ? std::max(gap_above, 0.0f) : gap_above) / v;
            exit = (tmax + r) / v;
        } else if (v < -1e-7f) {
            if (gap_above > -kContactTolerance) return out;
            enter = (gap_below > -kContactTolerance ? std::max(gap_below, 0.0f) : gap_below) / -v;
            exit = (tmin - r) / v;
        } else {
            if (gap_above > -kContactTolerance || gap_below > -kContactTolerance) return out;
            enter = -std::numeric_limits<float>::infinity();
            exit = std::numeric_limits<float>::infinity();
        }

        float penetration = -std::max(gap_above, gap_below);
        if (penetration < min_penetration) {
            min_penetration = penetration;
            min_pen_axis = gap_above > gap_below ? -ax : ax;
        }

        if (enter > t_enter) {
            t_enter = enter;
            enter_normal = v > 0 ? -ax : ax;
        }
        t_exit = std::min(t_exit, exit);
        if (t_enter > t_exit || t_enter > 1.0f || t_exit < 0.0f) return out;
    }

    if (t_enter < 0.0f) {
        // Overlapping at the start. Let the box leave through the front face.
        if (dot(d, tri_normal) >= 0.0f && dot(d, min_pen_axis) >= 0.0f) return out;
        out.hit = true;
        out.start_solid = true;
        out.t = 0.0f;
        out.normal = min_pen_axis;
        return out;
    }
    out.hit = true;
    out.t = t_enter;
    out.normal = enter_normal;
    return out;
}

// Static overlap of a box centred at c with a triangle (strict, no skin).
bool box_triangle_overlap(const Vec3& c, const Vec3& h, const Vec3& a, const Vec3& b, const Vec3& d, const Vec3& n) {
    const Vec3 p0 = a - c, p1 = b - c, p2 = d - c;
    const Vec3 edges[3] = {b - a, d - b, a - d};
    Vec3 axes[13];
    int count = 0;
    axes[count++] = {1, 0, 0};
    axes[count++] = {0, 1, 0};
    axes[count++] = {0, 0, 1};
    axes[count++] = n;
    for (const Vec3& e : edges) {
        Vec3 cands[3] = {{0, -e.z, e.y}, {e.z, 0, -e.x}, {-e.y, e.x, 0}};
        for (const Vec3& ax : cands)
            if (length_sq(ax) > 1e-10f) axes[count++] = ax;
    }
    for (int i = 0; i < count; ++i) {
        const Vec3& ax = axes[i];
        float r = h.x * std::fabs(ax.x) + h.y * std::fabs(ax.y) + h.z * std::fabs(ax.z);
        float q0 = dot(p0, ax), q1 = dot(p1, ax), q2 = dot(p2, ax);
        if (std::min({q0, q1, q2}) >= r || std::max({q0, q1, q2}) <= -r) return false;
    }
    return true;
}

bool ray_triangle(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c, float& t) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = cross(d, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < 1e-10f) return false;
    float inv = 1.0f / det;
    const Vec3 s = o - a;
    float u = dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = cross(s, e1);
    float v = dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = dot(e2, q) * inv;
    return t >= 0.0f;
}

}  // namespace

void CollisionMesh::clear() {
    tris_.clear();
    order_.clear();
    nodes_.clear();
    bounds_ = Aabb{};
    surface_mask_ = 0;
}

void CollisionMesh::build(std::span<const Vec3> vertices, std::span<const u32> indices, std::span<const u8> surfaces) {
    clear();
    size_t count = indices.size() / 3;
    tris_.reserve(count);
    std::vector<Vec3> centroids;
    std::vector<Aabb> boxes;
    centroids.reserve(count);
    boxes.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        u32 ia = indices[i * 3], ib = indices[i * 3 + 1], ic = indices[i * 3 + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) continue;
        Tri t;
        t.v0 = vertices[ia];
        t.v1 = vertices[ib];
        t.v2 = vertices[ic];
        if (!finite(t.v0) || !finite(t.v1) || !finite(t.v2)) continue;
        Vec3 n = cross(t.v1 - t.v0, t.v2 - t.v0);
        float len = length(n);
        if (len < 1e-6f) continue;   // degenerate
        t.normal = n / len;
        t.surface = i < surfaces.size() ? surfaces[i] : 0;
        if (t.surface < 32) surface_mask_ |= 1u << t.surface;
        tris_.push_back(t);
        Aabb tb;
        tb.add(t.v0);
        tb.add(t.v1);
        tb.add(t.v2);
        boxes.push_back(tb);
        centroids.push_back((t.v0 + t.v1 + t.v2) / 3.0f);
        bounds_.add(tb);
    }
    order_.resize(tris_.size());
    for (u32 i = 0; i < order_.size(); ++i) order_[i] = i;
    if (!tris_.empty()) {
        nodes_.reserve(tris_.size() * 2 / kLeafSize + 1);
        build_node(0, u32(tris_.size()), centroids, boxes, 0);
    }
}

u32 CollisionMesh::build_node(u32 first, u32 count, const std::vector<Vec3>& centroids, const std::vector<Aabb>& boxes,
                              int depth) {
    u32 index = u32(nodes_.size());
    nodes_.push_back({});
    Aabb box, centre_box;
    for (u32 i = first; i < first + count; ++i) {
        box.add(boxes[order_[i]]);
        centre_box.add(centroids[order_[i]]);
    }
    nodes_[index].box = box;
    auto make_leaf = [&] {
        nodes_[index].first = first;
        nodes_[index].count = count;
        return index;
    };
    if (count <= kLeafSize || depth > kMaxDepth) return make_leaf();

    // Binned surface area heuristic: sort the centroids into bins along each axis and take the
    // plane between bins that minimises (area x triangles) summed over both sides. A median
    // split halves the count but not the space, so a room of big wall quads and a pile of tiny
    // prop triangles end up sharing boxes, and every ray through the room pays for the props.
    const Vec3 ext = centre_box.max - centre_box.min;
    int best_axis = -1, best_split = 0;
    float best_cost = std::numeric_limits<float>::infinity();
    struct Bin {
        Aabb box;
        u32 count = 0;
    };
    Bin all_bins[3][kBins];
    float scale[3];
    for (int axis = 0; axis < 3; ++axis) scale[axis] = ext[axis] > 1e-4f ? float(kBins) / ext[axis] : 0.0f;
    // One pass fills all three axes' bins: the triangles are the memory being walked, not the axes.
    for (u32 i = first; i < first + count; ++i) {
        const u32 ti = order_[i];
        const Vec3& c = centroids[ti];
        for (int axis = 0; axis < 3; ++axis) {
            const int b = std::min(kBins - 1, int((c[axis] - centre_box.min[axis]) * scale[axis]));
            all_bins[axis][b].box.add(boxes[ti]);
            ++all_bins[axis][b].count;
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (scale[axis] == 0.0f) continue;
        const Bin* bins = all_bins[axis];
        float right_area[kBins];
        u32 right_count[kBins];
        Aabb acc;
        u32 n = 0;
        for (int b = kBins - 1; b > 0; --b) {
            acc.add(bins[b].box);
            n += bins[b].count;
            right_area[b] = surface_area(acc);
            right_count[b] = n;
        }
        acc = Aabb{};
        n = 0;
        for (int b = 0; b < kBins - 1; ++b) {
            acc.add(bins[b].box);
            n += bins[b].count;
            if (n == 0 || right_count[b + 1] == 0) continue;
            const float cost = surface_area(acc) * float(n) + right_area[b + 1] * float(right_count[b + 1]);
            if (cost < best_cost) {
                best_cost = cost;
                best_axis = axis;
                best_split = b + 1;
            }
        }
    }

    u32 mid = first + count / 2;
    const float parent_area = std::max(surface_area(box), 1e-6f);
    const float split_cost = 1.0f + kTriangleCost * best_cost / parent_area;
    const float leaf_cost = kTriangleCost * float(count);
    if (best_axis >= 0 && split_cost >= leaf_cost && count <= kMaxLeaf) return make_leaf();
    if (best_axis >= 0) {
        const float s = scale[best_axis];
        auto* split = std::partition(order_.data() + first, order_.data() + first + count, [&](u32 ti) {
            return std::min(kBins - 1, int((centroids[ti][best_axis] - centre_box.min[best_axis]) * s)) < best_split;
        });
        mid = u32(split - order_.data());
    }
    if (best_axis < 0 || mid == first || mid == first + count) {
        // Every centroid in one place (or one bin): fall back to halving along the longest axis.
        const int axis = (ext.x > ext.y && ext.x > ext.z) ? 0 : (ext.y > ext.z ? 1 : 2);
        mid = first + count / 2;
        std::nth_element(order_.begin() + first, order_.begin() + mid, order_.begin() + first + count,
                         [&](u32 a, u32 b) { return centroids[a][axis] < centroids[b][axis]; });
    }
    build_node(first, mid - first, centroids, boxes, depth + 1);
    u32 right = build_node(mid, first + count - mid, centroids, boxes, depth + 1);
    nodes_[index].first = right;
    nodes_[index].count = 0;
    return index;
}

template <typename Visit>
void CollisionMesh::walk_segment(const Vec3& start, const Vec3& end, const Vec3& expand, float& max_t,
                                 Visit&& visit) const {
    if (nodes_.empty()) return;
    Vec3 d = end - start;
    Vec3 inv{d.x != 0 ? 1.0f / d.x : std::numeric_limits<float>::infinity(),
             d.y != 0 ? 1.0f / d.y : std::numeric_limits<float>::infinity(),
             d.z != 0 ? 1.0f / d.z : std::numeric_limits<float>::infinity()};
    // Nearest child first, and `max_t` comes down as the visitor finds hits, so everything past
    // the closest hit so far is skipped: a bullet that meets a wall a metre out no longer walks
    // every node its full range would cross. A visitor that has its answer sets `max_t` below
    // zero and the walk stops.
    auto entry = [&](u32 ni) {
        const Node& node = nodes_[ni];
        const Aabb grown{node.box.min - expand, node.box.max + expand};
        return ray_aabb(start, inv, grown, max_t + kPruneSlack);
    };
    struct Pending {
        u32 node;
        float t;
    };
    Pending stack[2 * kMaxDepth + 8];
    int top = 0;
    if (const float t = entry(0); t >= 0.0f) stack[top++] = {0, t};
    while (top > 0) {
        const Pending p = stack[--top];
        if (max_t < 0.0f) return;
        if (p.t > max_t + kPruneSlack) continue;   // a closer hit turned up since it was queued
        const Node& node = nodes_[p.node];
        if (node.count > 0) {
            for (u32 i = node.first; i < node.first + node.count; ++i) visit(order_[i]);
            continue;
        }
        const u32 near_child = p.node + 1, far_child = node.first;
        const float tn = entry(near_child), tf = entry(far_child);
        const bool hit_near = tn >= 0.0f, hit_far = tf >= 0.0f;
        if (hit_near && hit_far) {
            // Pushed far first so the nearer comes off the stack next.
            const bool swap = tf < tn;
            stack[top++] = swap ? Pending{near_child, tn} : Pending{far_child, tf};
            stack[top++] = swap ? Pending{far_child, tf} : Pending{near_child, tn};
        } else if (hit_near) {
            stack[top++] = {near_child, tn};
        } else if (hit_far) {
            stack[top++] = {far_child, tf};
        }
    }
}

TraceResult CollisionMesh::trace_box(const Vec3& start, const Vec3& end, const Vec3& half_extents) const {
    TraceResult result;
    result.end = end;
    Vec3 d = end - start;
    Vec3 expand = half_extents + Vec3{kSkin * 2, kSkin * 2, kSkin * 2};
    float max_t = 1.0f;
    walk_segment(start, end, expand, max_t, [&](u32 ti) {
        const Tri& t = tris_[ti];
        SweepHit h = sweep_box_triangle(start, d, half_extents, t.v0, t.v1, t.v2, t.normal);
        if (!h.hit) return;
        if (h.start_solid) {
            if (!result.start_solid || result.fraction > 0.0f) {
                result.start_solid = true;
                result.fraction = 0.0f;
                result.normal = h.normal;
                result.triangle = int(ti);
                result.surface = t.surface;
                max_t = 0.0f;
            }
            return;
        }
        if (h.t < result.fraction) {
            result.fraction = h.t;
            result.normal = h.normal;
            result.triangle = int(ti);
            result.surface = t.surface;
            max_t = h.t;
        }
    });
    result.end = start + d * result.fraction;
    return result;
}

TraceResult CollisionMesh::trace_ray(const Vec3& start, const Vec3& end) const {
    TraceResult result;
    Vec3 d = end - start;
    float max_t = 1.0f;
    walk_segment(start, end, Vec3{0, 0, 0}, max_t, [&](u32 ti) {
        const Tri& t = tris_[ti];
        float hit_t;
        if (ray_triangle(start, d, t.v0, t.v1, t.v2, hit_t) && hit_t <= result.fraction) {
            result.fraction = hit_t;
            result.back_face = dot(t.normal, d) > 0;
            result.normal = result.back_face ? -t.normal : t.normal;
            result.triangle = int(ti);
            result.surface = t.surface;
            max_t = hit_t;
        }
    });
    result.end = start + d * result.fraction;
    return result;
}

bool CollisionMesh::box_overlaps(const Vec3& center, const Vec3& half_extents) const {
    bool found = false;
    float max_t = 1.0f;
    walk_segment(center, center, half_extents, max_t, [&](u32 ti) {
        if (!found && box_triangle_overlap(center, half_extents, tris_[ti].v0, tris_[ti].v1, tris_[ti].v2,
                                           tris_[ti].normal)) {
            found = true;
            max_t = -1.0f;   // one is enough
        }
    });
    return found;
}

bool CollisionMesh::box_overlaps_surface(const Vec3& center, const Vec3& half_extents, u8 surface,
                                         Vec3& normal_out) const {
    if (!has_surface(surface)) return false;   // most maps have no ladder at all
    Vec3 sum{};
    int hits = 0;
    float max_t = 1.0f;
    walk_segment(center, center, half_extents, max_t, [&](u32 ti) {
        const Tri& t = tris_[ti];
        if (t.surface != surface) return;
        // A climbable face is near-vertical; the floor and ceiling of a ladder well are not
        // something to hang from even when they share the material.
        if (std::fabs(t.normal.y) > 0.7f) return;
        if (!box_triangle_overlap(center, half_extents, t.v0, t.v1, t.v2, t.normal)) return;
        // A ladder plane is usually two-sided geometry, so halves of it face opposite ways.
        // Fold the second half onto the first rather than letting them cancel out.
        sum += (hits > 0 && dot(sum, t.normal) < 0.0f) ? -t.normal : t.normal;
        ++hits;
    });
    if (hits == 0) return false;
    float len = length(sum);
    if (len < 1e-4f) return false;
    normal_out = sum / len;
    return true;
}

}  // namespace eng
