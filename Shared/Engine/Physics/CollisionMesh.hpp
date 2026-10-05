// Static triangle collision with a bounding volume hierarchy, built by the binned surface area
// heuristic and walked nearest child first with the search cut back to the closest hit so far.
//
// Box sweeps use the separating axis test in time: for every candidate triangle the
// 13 axes (3 box faces, the triangle normal, 9 edge cross products) give the interval
// during which the moving box overlaps it, and the latest entry across axes is the
// time of impact. The box is inflated by a small skin so it comes to rest slightly
// away from surfaces, which keeps sliding along floors and walls stable.
#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Core/Types.hpp"

#include <span>
#include <vector>

namespace eng {

struct TraceResult {
    float fraction = 1.0f;     // share of the move completed
    Vec3 end;                  // where the box/ray stopped
    Vec3 normal{0, 1, 0};      // surface normal at impact (valid when fraction < 1)
    bool start_solid = false;  // began inside geometry it was moving further into
    // Rays only: the triangle was met from behind, against its winding. Coming out of the far
    // side of a wall is how a bullet finds where the wall ends.
    bool back_face = false;
    int triangle = -1;
    u8 surface = 0;
    bool hit() const { return fraction < 1.0f || start_solid; }
};

class CollisionMesh {
public:
    static constexpr float kSkin = 0.25f;   // units kept between a swept box and surfaces

    void build(std::span<const Vec3> vertices, std::span<const u32> indices, std::span<const u8> surfaces = {});
    void clear();

    // Sweeps an axis-aligned box of `half_extents` whose centre moves from `start` to `end`.
    TraceResult trace_box(const Vec3& start, const Vec3& end, const Vec3& half_extents) const;
    // Closest hit along a segment.
    TraceResult trace_ray(const Vec3& start, const Vec3& end) const;
    // True when a box at rest intersects any triangle (no skin).
    bool box_overlaps(const Vec3& center, const Vec3& half_extents) const;
    // True when a box at rest intersects any triangle carrying `surface`; `normal_out` receives
    // the normal of those triangles averaged and renormalised, so a ladder built from several
    // coplanar quads reports one plane. Used to find the ladder a player is holding on to.
    bool box_overlaps_surface(const Vec3& center, const Vec3& half_extents, u8 surface, Vec3& normal_out) const;

    const Aabb& bounds() const { return bounds_; }
    size_t triangle_count() const { return tris_.size(); }
    bool empty() const { return tris_.empty(); }
    // Whether any triangle carries this surface, so per-tick searches for a rare one (a ladder)
    // cost nothing on the maps that have none.
    bool has_surface(u8 surface) const { return surface < 32 && (surface_mask_ & (1u << surface)); }

private:
    struct Tri {
        Vec3 v0, v1, v2;
        Vec3 normal;
        u8 surface;
    };
    struct Node {
        Aabb box;
        u32 first = 0;   // leaf: first index into order_; inner: right child index
        u32 count = 0;   // > 0 for leaves
    };

    u32 build_node(u32 first, u32 count, const std::vector<Vec3>& centroids, const std::vector<Aabb>& boxes, int depth);
    template <typename Visit>
    void walk_segment(const Vec3& start, const Vec3& end, const Vec3& expand, float& max_t, Visit&& visit) const;

    std::vector<Tri> tris_;
    std::vector<u32> order_;
    std::vector<Node> nodes_;
    Aabb bounds_;
    u32 surface_mask_ = 0;   // bit per surface id present in tris_
};

}  // namespace eng
