// Where a bot can walk, and how to get from one place to another: a navigation graph derived from
// a map's own collision when the map is loaded (TacticalFPS's Game/Navigation, Gameplan 04 §5.1).
//
// Derived, not authored: it cannot fall out of step with the geometry, and every Soldier Front map
// has one without anybody making it. A grid of floor cells, not a polygon mesh: every column of the
// map is probed top to bottom for surfaces a soldier could stand on and fit on -- a building's
// floors, its roof and the street under it are three cells in one column -- and neighbouring cells
// are linked where Game/Movement.cpp could take a soldier from one to the other: walking (up or down
// a step), dropping off an edge (one way), or jumping up a ledge. A path is A* over those links.
//
// Ladders are not linked: a bot does not climb.
#pragma once

#include "Engine/Core/Hash.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Physics/CollisionMesh.hpp"
#include "Game/Movement.hpp"

#include <vector>

namespace lsf {

enum class NavMove : u8 { Walk = 0, Drop = 1, Jump = 2 };

struct NavNode {
    eng::Vec3 pos;         // the floor, at the cell's centre
    u32 first_link = 0;
    u16 link_count = 0;
    bool crouch = false;   // only a crouching soldier fits here
    bool edge = false;     // short of a walking neighbour on some side: a wall, a drop, a ledge
    int component = -1;    // cells reachable from each other share one
};

struct NavLink {
    u32 to = 0;
    NavMove move = NavMove::Walk;
};

class NavGraph {
public:
    struct Stats {
        float cell = 0;          // cm
        size_t columns = 0;
        size_t nodes = 0;
        size_t links = 0;
        size_t largest = 0;      // nodes in the biggest walkable component
        double build_ms = 0;
    };

    // Probes `bounds` of `world` on a grid fine enough for a soldier's hull and coarse enough to stay
    // under `max_columns`: a big map gets bigger cells rather than a minute's build.
    void build(const eng::CollisionMesh& world, const MovementDef& def, const eng::Aabb& bounds, size_t max_columns = 250000);
    bool empty() const { return nodes_.empty(); }
    size_t size() const { return nodes_.size(); }
    const NavNode& node(int i) const { return nodes_[size_t(i)]; }
    const Stats& stats() const { return stats_; }

    // The cell a soldier standing at `feet` is on, or -1 (the neighbouring columns too, so somebody
    // against a wall -- whose own cell's centre is in the wall -- is still found).
    int locate(const eng::Vec3& feet) const;
    // A path of cells from one to the other, inclusive, or false when there is none.
    bool find_path(int from, int to, std::vector<int>& path) const;
    NavMove move_between(int from, int to) const;
    // A cell somewhere in the same walkable region as `near`, for a bot with nowhere better to go.
    int random_node(eng::Rng& rng, int near) const;
    // The cell of region `component` (-1: any) nearest `at` across the floor, within `radius` of it
    // and `max_dy` up or down; -1 when there is none. A mission's mark often sits on something (a
    // bomb site on a crate, a laptop on a table): this is where a soldier stands to reach it.
    int nearest_in(const eng::Vec3& at, int component, float radius, float max_dy) const;
    // The same, every such cell nearest first (at most `count`): the nearest is often on top of the
    // crate a mark sits on, a cell dropped off and never walked onto, so a caller takes the first
    // it has a path to.
    std::vector<int> nearest_cells(const eng::Vec3& at, int component, float radius, float max_dy, size_t count) const;
    // Every cell walked to from `from` along the links as they go (a drop one way only), 1 for each:
    // one flood, where asking for a path to each candidate floods once a failure.
    std::vector<char> reachable_from(int from) const;

private:
    int column_of(float x, float z) const;

    Stats stats_;
    float cell_ = 50.0f;
    eng::Vec3 origin_;
    int cols_ = 0, rows_ = 0;
    std::vector<NavNode> nodes_;
    std::vector<NavLink> links_;
    std::vector<u32> column_start_;   // cols_*rows_+1 offsets into nodes_, which are sorted by column
    std::vector<std::vector<int>> by_component_;
};

// Walks a soldier along a path by filling in the movement half of their command. A grid path
// zigzags, so it aims at the furthest cell ahead it could walk to in a straight line, and only
// takes a drop or a jump when it is at one.
class NavFollower {
public:
    void start(std::vector<int> path);
    void clear() { path_.clear(), at_ = 0; }
    bool active() const { return at_ < path_.size(); }
    // Sets forward, side, jump and crouch of `cmd` to follow the path from `me`, relative to the
    // `cmd.yaw` already there unless `face_path` turns the view to the way it is going. False once
    // the path is walked (and `cmd` left standing).
    bool steer(const NavGraph& nav, const eng::CollisionMesh& world, const MovementDef& def, const MoveState& me, MoveCmd& cmd, bool face_path);
    // For a walker that has stopped making ground: back off, sidestep and hop for half a second.
    void unstick(bool left);
    float heading() const { return heading_; }

private:
    std::vector<int> path_;
    float heading_ = 0;
    size_t at_ = 0;
    size_t aim_ = 0;
    int since_aim_ = 0;
    int recover_ = 0;
    float recover_side_ = 1;
};

}  // namespace lsf
