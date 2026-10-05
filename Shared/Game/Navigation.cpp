#include "Game/Navigation.hpp"

#include "Engine/Core/Time.hpp"
#include "Game/Movement.hpp"
#include "Game/Protocol.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <queue>

namespace lsf {

using namespace eng;
using eng::Vec3;

namespace {

constexpr int kMaxFloorsPerColumn = 8;
// How far a bot will walk off an edge. There is no fall damage (Gameplan 05 D13), so this is about
// not leaving a bot at the bottom of something it cannot climb back out of, not about hurting it.
constexpr float kMaxDrop = 400.0f;
// A ledge a bot will jump onto. The jump peaks near 96 cm; the hull needs the rest of it to clear
// the lip, and a jump that only just makes it in a test fails half the time in a match.
constexpr float kMaxJumpUp = 60.0f;
// The band a link's corridor is tested in: from just above a step, so stairs pass, up to where a
// crouching player's head is, so a low opening a bot can crouch through still links. The bottom
// has to sit right on the step height: anything between the step and the band would be invisible
// to the graph and a wall to the hull, and the first version left 38 to 46 cm in that gap.
constexpr float kBandHigh = 108.0f;

// Whether a hull could walk straight from one floor point to another: a box the width of the hull
// swept through the band between a step and a crouching head. A wall thinner than a cell is
// caught; a door a hull fits through is not.
bool clear_corridor(const CollisionMesh& world, const MovementDef& def, const Vec3& a_feet, const Vec3& b_feet) {
    const float low = def.step_height + 1.0f;
    const float band = (kBandHigh - low) * 0.5f;
    // A little wider than the hull, not narrower: the first version was two centimetres inside it,
    // and every diagonal that shaved a crate's corner in the graph caught the real hull on it.
    const Vec3 corridor{def.hull_half_width + 1.0f, band, def.hull_half_width + 1.0f};
    const Vec3 up{0, low + band, 0};
    return !world.trace_box(a_feet + up, b_feet + up, corridor).hit();
}

// Whether a straight walk from one floor point to another has floor under it all the way: every
// forty centimetres there must be something to stand on within a step of the straight line
// between the two heights. The corridor above says nothing is in the way; this says nothing is
// missing, which on a ramp with an open side is the question that matters.
bool floor_along(const CollisionMesh& world, const MovementDef& def, const Vec3& a_feet, const Vec3& b_feet) {
    const Vec3 d = b_feet - a_feet;
    const int samples = std::max(1, int(std::ceil(std::sqrt(d.x * d.x + d.z * d.z) / 40.0f)));
    for (int i = 1; i <= samples; ++i) {
        const Vec3 p = a_feet + d * (float(i) / float(samples));
        const TraceResult t = world.trace_ray(p + Vec3{0, def.step_height, 0}, p - Vec3{0, def.step_height + 10.0f, 0});
        if (t.fraction >= 1.0f || t.normal.y < def.max_slope_normal_y) return false;
    }
    return true;
}

}  // namespace

int NavGraph::column_of(float x, float z) const {
    const int c = int(std::floor((x - origin_.x) / cell_));
    const int r = int(std::floor((z - origin_.z) / cell_));
    if (c < 0 || r < 0 || c >= cols_ || r >= rows_) return -1;
    return r * cols_ + c;
}

void NavGraph::build(const CollisionMesh& world, const MovementDef& def, const Aabb& bounds, size_t max_columns) {
    const double started = time::now();
    nodes_.clear();
    links_.clear();
    by_component_.clear();
    stats_ = Stats{};
    if (world.empty() || !bounds.valid()) return;

    const Vec3 size = bounds.max - bounds.min;
    cell_ = std::max(50.0f, std::sqrt(size.x * size.z / float(std::max<size_t>(max_columns, 1))));
    cols_ = std::max(1, int(std::ceil(size.x / cell_)));
    rows_ = std::max(1, int(std::ceil(size.z / cell_)));
    origin_ = bounds.min;
    const float top = bounds.max.y + 50.0f, bottom = bounds.min.y - 50.0f;
    const Vec3 stand_half = hull_half_extents(def, false), crouch_half = hull_half_extents(def, true);

    // ── Cells: every surface in a column a player could stand on and fit on ─────────────────
    column_start_.assign(size_t(cols_) * size_t(rows_) + 1, 0);
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c) {
            column_start_[size_t(r) * cols_ + c] = u32(nodes_.size());
            const float x = origin_.x + (float(c) + 0.5f) * cell_, z = origin_.z + (float(r) + 0.5f) * cell_;
            float from = top;
            for (int k = 0; k < kMaxFloorsPerColumn && from > bottom; ++k) {
                const TraceResult t = world.trace_ray({x, from, z}, {x, bottom, z});
                if (t.fraction >= 1.0f) break;
                from = t.end.y - 1.0f;
                // Faces met from either side count: the hull test below is what says whether a
                // player can really stand there, and some imported floors are wound backwards.
                if (t.normal.y < def.max_slope_normal_y) continue;
                const Vec3 feet{x, t.end.y, z};
                NavNode n;
                n.pos = feet;
                // On a slope the ground under the hull's uphill corner is higher than under its
                // middle, and a player's hull rests on that corner. A test box at the middle's height
                // sits in the slope -- which is how every ramp in the library first came out with
                // no cells on it. Lift it by the rise across the hull's half-diagonal.
                const float ny = std::max(t.normal.y, 0.2f);
                const float rise = def.hull_half_width * 1.42f * std::sqrt(std::max(0.0f, 1.0f - ny * ny)) / ny;
                const Vec3 lift{0, 2.0f + rise, 0};
                if (world.box_overlaps(hull_center(def, feet + lift, false), stand_half)) {
                    if (world.box_overlaps(hull_center(def, feet + lift, true), crouch_half)) continue;
                    n.crouch = true;
                }
                nodes_.push_back(n);
            }
        }
    column_start_.back() = u32(nodes_.size());
    stats_.cell = cell_;
    stats_.columns = size_t(cols_) * size_t(rows_);
    stats_.nodes = nodes_.size();

    // ── Links: what Game/Movement.cpp can actually do between neighbouring cells ────────────
    auto swept = [&](const Vec3& a_feet, const Vec3& b_feet) { return clear_corridor(world, def, a_feet, b_feet); };
    std::vector<std::vector<NavLink>> adj(nodes_.size());
    const int dirs[8][2] = {{1, 0}, {0, 1}, {1, 1}, {-1, 1}, {-1, 0}, {0, -1}, {-1, -1}, {1, -1}};
    const float step = def.step_height + 2.0f;
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c) {
            const size_t col = size_t(r) * cols_ + c;
            for (u32 ai = column_start_[col]; ai < column_start_[col + 1]; ++ai) {
                const NavNode& a = nodes_[ai];
                for (int d = 0; d < 8; ++d) {
                    const int nc = c + dirs[d][0], nr = r + dirs[d][1];
                    if (nc < 0 || nr < 0 || nc >= cols_ || nr >= rows_) continue;
                    const size_t ncol = size_t(nr) * cols_ + nc;
                    // Walking, both ways, is tested once per pair: only from the first four
                    // directions, and linked in both. Up to two steps' height is a walk when there
                    // is a tread halfway -- a flight steeper than a step per half cell, which the
                    // movement code climbs a step at a time -- and not when there is only a ledge.
                    if (d < 4) {
                        int best = -1;
                        float best_dy = 2.0f * step;
                        for (u32 bi = column_start_[ncol]; bi < column_start_[ncol + 1]; ++bi) {
                            const float dy = std::fabs(nodes_[bi].pos.y - a.pos.y);
                            if (dy <= best_dy) {
                                best_dy = dy;
                                best = int(bi);
                            }
                        }
                        bool walkable = best >= 0 && swept(a.pos, nodes_[size_t(best)].pos);
                        // A diagonal cuts no corners: both cells it passes between have to be floor
                        // at a height a player could be at on the way. A diagonal from the side of a
                        // ramp to the kerb beside it crosses the ramp's edge over nothing, and the
                        // corridor above it is clear all the same -- Relay Station's dock ramp
                        // walked a walker off its side that way. (A floor test at the midpoint was
                        // tried first, and the midpoint landed exactly on the ramp's edge.)
                        if (walkable && d >= 2) {
                            const Vec3& b = nodes_[size_t(best)].pos;
                            auto floor_between = [&](size_t side) {
                                for (u32 si = column_start_[side]; si < column_start_[side + 1]; ++si) {
                                    const float y = nodes_[si].pos.y;
                                    if (std::fabs(y - a.pos.y) <= step && std::fabs(y - b.y) <= step) return true;
                                }
                                return false;
                            };
                            walkable = floor_between(size_t(r) * cols_ + nc) && floor_between(size_t(nr) * cols_ + c);
                        }
                        // Floor under the middle of a climb of more than a step: the tread of a
                        // steep flight, which a sheer ledge does not have.
                        if (walkable && best_dy > step) {
                            const Vec3& b = nodes_[size_t(best)].pos;
                            const float hi = std::max(a.pos.y, b.y), lo = std::min(a.pos.y, b.y);
                            const Vec3 mid{(a.pos.x + b.x) * 0.5f, hi + step, (a.pos.z + b.z) * 0.5f};
                            const TraceResult tread = world.trace_ray(mid, mid - Vec3{0, 2.0f * step + 10.0f + (hi - lo), 0});
                            walkable = tread.fraction < 1.0f && tread.normal.y >= def.max_slope_normal_y &&
                                       tread.end.y - lo <= step && hi - tread.end.y <= step;
                        }
                        if (walkable) {
                            adj[ai].push_back({u32(best), NavMove::Walk});
                            adj[size_t(best)].push_back({ai, NavMove::Walk});
                        }
                        // Nothing to stand on next door, and floor within a step two cells out: a lip
                        // between them (a kerb's narrow top, a sill) that no hull stands on and every
                        // soldier walks over. Missile's red spawn is a ledge 35 cm up behind one, and
                        // was an island until this: its bots never left it.
                        if (best < 0 && d < 2) {
                            const int fc = c + 2 * dirs[d][0], fr = r + 2 * dirs[d][1];
                            if (fc < cols_ && fr < rows_) {
                                const size_t fcol = size_t(fr) * cols_ + fc;
                                for (u32 bi = column_start_[fcol]; bi < column_start_[fcol + 1]; ++bi) {
                                    const Vec3& b = nodes_[bi].pos;
                                    if (std::fabs(b.y - a.pos.y) > step || !swept(a.pos, b)) continue;
                                    const float hi = std::max(a.pos.y, b.y);
                                    const Vec3 mid{(a.pos.x + b.x) * 0.5f, hi + step, (a.pos.z + b.z) * 0.5f};
                                    const TraceResult lip = world.trace_ray(mid, mid - Vec3{0, 2.0f * step + 10.0f, 0});
                                    if (lip.fraction >= 1.0f || lip.end.y - hi > step || std::min(a.pos.y, b.y) - lip.end.y > step) continue;
                                    adj[ai].push_back({bi, NavMove::Walk});
                                    adj[bi].push_back({ai, NavMove::Walk});
                                    break;
                                }
                            }
                        }
                    }
                    // Off an edge: the highest floor below that is more than a step down, if the
                    // way over the edge is clear and the fall lands on it and not on something else.
                    // Two columns out when the next one has nothing to land on: an edge often has a
                    // lip or a painted border a hull cannot stand on, and a drop that has to clear
                    // it is still a drop -- Relay Station's plinth, which site A stands on, was an
                    // island until this looked past its border.
                    int below = -1;
                    for (int reach = 1; reach <= 2 && below < 0; ++reach) {
                        const int fc = c + dirs[d][0] * reach, fr = r + dirs[d][1] * reach;
                        if (fc < 0 || fr < 0 || fc >= cols_ || fr >= rows_) break;
                        const size_t fcol = size_t(fr) * cols_ + fc;
                        for (u32 bi = column_start_[fcol]; bi < column_start_[fcol + 1]; ++bi) {
                            const float dy = a.pos.y - nodes_[bi].pos.y;
                            if (dy > step && dy <= kMaxDrop &&
                                (below < 0 || nodes_[bi].pos.y > nodes_[size_t(below)].pos.y))
                                below = int(bi);
                        }
                        // Something standable at about this height next door means this is not an
                        // edge in that direction, and looking further out would jump a gap.
                        if (below < 0 && reach == 1) {
                            bool level = false;
                            for (u32 bi = column_start_[fcol]; bi < column_start_[fcol + 1]; ++bi)
                                level |= std::fabs(nodes_[bi].pos.y - a.pos.y) <= 2.0f * step;
                            if (level) break;
                        }
                    }
                    if (below < 0) continue;
                    const NavNode& b = nodes_[size_t(below)];
                    const Vec3 over{b.pos.x, a.pos.y, b.pos.z};
                    if (!swept(a.pos, over)) continue;
                    const TraceResult fall = world.trace_ray(over + Vec3{0, 10, 0}, b.pos - Vec3{0, 10, 0});
                    if (fall.fraction >= 1.0f || std::fabs(fall.end.y - b.pos.y) > 12.0f) continue;
                    adj[ai].push_back({u32(below), NavMove::Drop});
                    // And back up, if it is low enough to jump and there is headroom for the jump.
                    const float rise = a.pos.y - b.pos.y;
                    if (rise <= kMaxJumpUp &&
                        world.trace_ray(b.pos + Vec3{0, def.stand_height, 0},
                                        b.pos + Vec3{0, def.stand_height + rise + 10.0f, 0})
                                .fraction >= 1.0f)
                        adj[size_t(below)].push_back({ai, NavMove::Jump});
                }
            }
        }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        nodes_[i].first_link = u32(links_.size());
        nodes_[i].link_count = u16(std::min<size_t>(adj[i].size(), 0xFFFF));
        links_.insert(links_.end(), adj[i].begin(), adj[i].begin() + nodes_[i].link_count);
        int walks = 0;
        for (const NavLink& l : adj[i]) walks += l.move == NavMove::Walk;
        nodes_[i].edge = walks < 8;
    }
    stats_.links = links_.size();

    // ── Components: which cells belong to one region a player could walk round ──────────────
    // Links treated both ways, so a room you can drop into and jump out of is one region with the
    // street it opens onto.
    std::vector<int> parent(nodes_.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto root = [&](int i) {
        while (parent[size_t(i)] != i) {
            parent[size_t(i)] = parent[size_t(parent[size_t(i)])];
            i = parent[size_t(i)];
        }
        return i;
    };
    for (size_t i = 0; i < nodes_.size(); ++i)
        for (u32 l = nodes_[i].first_link; l < nodes_[i].first_link + nodes_[i].link_count; ++l) {
            const int a = root(int(i)), b = root(int(links_[l].to));
            if (a != b) parent[size_t(a)] = b;
        }
    std::vector<int> label(nodes_.size(), -1);
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const int r0 = root(int(i));
        if (label[size_t(r0)] < 0) {
            label[size_t(r0)] = int(by_component_.size());
            by_component_.emplace_back();
        }
        nodes_[i].component = label[size_t(r0)];
        by_component_[size_t(nodes_[i].component)].push_back(int(i));
    }
    for (const auto& comp : by_component_) stats_.largest = std::max(stats_.largest, comp.size());
    stats_.build_ms = (time::now() - started) * 1000.0;
}

int NavGraph::nearest_in(const Vec3& at, int component, float radius, float max_dy) const {
    const std::vector<int> cells = nearest_cells(at, component, radius, max_dy, 1);
    return cells.empty() ? -1 : cells.front();
}

std::vector<int> NavGraph::nearest_cells(const Vec3& at, int component, float radius, float max_dy, size_t count) const {
    std::vector<std::pair<float, int>> found;
    if (nodes_.empty()) return {};
    const int c = int(std::floor((at.x - origin_.x) / cell_)), r = int(std::floor((at.z - origin_.z) / cell_));
    const int reach = int(std::ceil(radius / cell_)) + 1;
    for (int rr = std::max(0, r - reach); rr <= std::min(rows_ - 1, r + reach); ++rr)
        for (int cc = std::max(0, c - reach); cc <= std::min(cols_ - 1, c + reach); ++cc) {
            const size_t col = size_t(rr) * cols_ + cc;
            for (u32 i = column_start_[col]; i < column_start_[col + 1]; ++i) {
                const NavNode& n = nodes_[i];
                if ((component >= 0 && n.component != component) || std::fabs(n.pos.y - at.y) > max_dy) continue;
                const float dx = n.pos.x - at.x, dz = n.pos.z - at.z, d = dx * dx + dz * dz;
                if (d <= radius * radius) found.push_back({d, int(i)});
            }
        }
    std::sort(found.begin(), found.end());
    std::vector<int> out;
    for (size_t k = 0; k < found.size() && out.size() < count; ++k) out.push_back(found[k].second);
    return out;
}

std::vector<char> NavGraph::reachable_from(int from) const {
    std::vector<char> seen(nodes_.size(), 0);
    if (from < 0 || size_t(from) >= nodes_.size()) return seen;
    std::vector<int> todo{from};
    seen[size_t(from)] = 1;
    while (!todo.empty()) {
        const NavNode& n = nodes_[size_t(todo.back())];
        todo.pop_back();
        for (u32 l = n.first_link; l < n.first_link + n.link_count; ++l)
            if (!seen[links_[l].to]) seen[links_[l].to] = 1, todo.push_back(int(links_[l].to));
    }
    return seen;
}

int NavGraph::locate(const Vec3& feet) const {
    if (nodes_.empty()) return -1;
    const int c = int(std::floor((feet.x - origin_.x) / cell_));
    const int r = int(std::floor((feet.z - origin_.z) / cell_));
    int best = -1;
    float best_score = 1e30f;
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc) {
            const int cc = c + dc, rr = r + dr;
            if (cc < 0 || rr < 0 || cc >= cols_ || rr >= rows_) continue;
            const size_t col = size_t(rr) * cols_ + cc;
            for (u32 i = column_start_[col]; i < column_start_[col + 1]; ++i) {
                const Vec3& p = nodes_[i].pos;
                const float dy = feet.y - p.y;
                // The floor under the feet, not the one overhead: a cell a little above counts (a
                // step's worth), anything higher is the storey above.
                if (dy < -60.0f) continue;
                const float dx = feet.x - p.x, dz = feet.z - p.z;
                const float score = dx * dx + dz * dz + dy * dy * 4.0f;
                if (score < best_score) {
                    best_score = score;
                    best = int(i);
                }
            }
        }
    return best;
}

NavMove NavGraph::move_between(int from, int to) const {
    const NavNode& n = nodes_[size_t(from)];
    for (u32 l = n.first_link; l < n.first_link + n.link_count; ++l)
        if (int(links_[l].to) == to) return links_[l].move;
    return NavMove::Walk;
}

bool NavGraph::find_path(int from, int to, std::vector<int>& path) const {
    path.clear();
    if (from < 0 || to < 0 || size_t(from) >= nodes_.size() || size_t(to) >= nodes_.size()) return false;
    if (from == to) {
        path.push_back(from);
        return true;
    }
    const Vec3 goal = nodes_[size_t(to)].pos;
    std::vector<float> cost(nodes_.size(), 1e30f);
    std::vector<int> came(nodes_.size(), -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    cost[size_t(from)] = 0;
    open.push({length(goal - nodes_[size_t(from)].pos), from});
    while (!open.empty()) {
        const auto [f, i] = open.top();
        open.pop();
        if (i == to) break;
        const NavNode& n = nodes_[size_t(i)];
        if (f - length(goal - n.pos) > cost[size_t(i)] + 1e-3f) continue;   // a stale entry
        for (u32 l = n.first_link; l < n.first_link + n.link_count; ++l) {
            const NavLink& link = links_[l];
            const NavNode& m = nodes_[link.to];
            // Distance, and a little more for anything that is not a plain walk: a bot should drop
            // or jump when it saves a real detour and not otherwise.
            float step = length(m.pos - n.pos);
            if (link.move == NavMove::Drop) step += 60.0f;
            if (link.move == NavMove::Jump) step += 120.0f;
            if (m.crouch) step *= 1.8f;
            // Along a wall or a drop costs more than across open floor, so a path keeps to the
            // middle of a ramp and off a ledge a hull only just fits on -- a turn at speed on the
            // edge of Relay Station's dock carried a walker over it.
            if (m.edge) step *= 1.6f;
            const float c = cost[size_t(i)] + step;
            if (c < cost[link.to]) {
                cost[link.to] = c;
                came[link.to] = i;
                open.push({c + length(goal - m.pos), int(link.to)});
            }
        }
    }
    if (came[size_t(to)] < 0) return false;
    for (int i = to; i >= 0; i = came[size_t(i)]) {
        path.push_back(i);
        if (i == from) break;
    }
    std::reverse(path.begin(), path.end());
    return !path.empty() && path.front() == from;
}

int NavGraph::random_node(Rng& rng, int near) const {
    if (nodes_.empty()) return -1;
    const int comp = near >= 0 ? nodes_[size_t(near)].component : 0;
    const auto& pool = by_component_[size_t(std::max(comp, 0))];
    return pool[rng.next() % pool.size()];
}

// ── Following a path ───────────────────────────────────────────────────────────

void NavFollower::unstick(bool left) {
    recover_ = 32;
    recover_side_ = left ? -1.0f : 1.0f;
}

void NavFollower::start(std::vector<int> path) {
    path_ = std::move(path);
    // The first cell is the one underfoot; the walk is to the next.
    at_ = path_.size() > 1 ? 1 : path_.size();
    aim_ = at_;
    since_aim_ = 1 << 20;
}

bool NavFollower::steer(const NavGraph& nav, const CollisionMesh& world, const MovementDef& def, const MoveState& me, MoveCmd& cmd,
                        bool face_path) {
    cmd.forward = 0;
    cmd.side = 0;
    cmd.buttons &= u16(~(proto::kButtonJump | proto::kButtonCrouch));
    const Vec3 feet = me.origin;
    // The furthest of the next dozen cells the feet are on is where the walk has got to. Not the
    // next one only: the straight lines below walk past cells without touching their centres, and
    // the first version waited for each centre in turn -- so the walker reached its aim, found the
    // cell it was "at" behind it, and turned round to fetch it.
    {
        size_t reached = at_;
        bool on = false;
        for (size_t j = at_; j < std::min(path_.size(), at_ + 12); ++j) {
            const Vec3& p = nav.node(path_[j]).pos;
            const float dx = p.x - feet.x, dz = p.z - feet.z;
            if (dx * dx + dz * dz <= 50.0f * 50.0f && std::fabs(p.y - feet.y) <= 70.0f) {
                reached = j;
                on = true;
            }
        }
        if (on) at_ = reached + 1;
    }
    if (!active()) return false;

    // What to walk straight at: the furthest of the next few cells a hull could reach in a line.
    // Chosen again every few commands rather than every one, because each candidate is a swept
    // box and there are sixteen bots.
    if (aim_ < at_ || ++since_aim_ >= 8) {
        aim_ = at_;
        since_aim_ = 0;
        const size_t last = std::min(path_.size() - 1, at_ + 8);
        for (size_t j = at_ + 1; j <= last; ++j) {
            // A drop or a jump happens where the path puts it, never smoothed over.
            if (nav.move_between(path_[j - 1], path_[j]) != NavMove::Walk) break;
            const Vec3& q = nav.node(path_[j]).pos;
            if (std::fabs(q.y - feet.y) > 200.0f || !clear_corridor(world, def, feet, q) ||
                !floor_along(world, def, feet, q))
                break;
            aim_ = j;
        }
    }
    const Vec3 target = nav.node(path_[aim_]).pos;
    const Vec3 to{target.x - feet.x, 0, target.z - feet.z};
    const float heading = length(to) > 1.0f ? forward_to_yaw(to) : cmd.yaw;
    heading_ = heading;
    if (face_path) cmd.yaw = heading;
    float off = wrap_degrees(heading - cmd.yaw) * kDegToRad;
    if (recover_ > 0) {
        // Backing away at an angle to the side, with one hop part way: what gets a hull off the
        // corner it is pressed against, whichever way round the corner the path goes next.
        --recover_;
        off += (180.0f - 60.0f * recover_side_) * kDegToRad;
        if (recover_ == 20 && me.on_ground) cmd.buttons |= proto::kButtonJump;
        if (recover_ == 0) since_aim_ = 1 << 20;   // look ahead afresh from wherever it ended up
    }
    cmd.forward = std::cos(off);
    cmd.side = std::sin(off);
    if (recover_ > 0) return true;

    // A jump is taken at the edge, on the ground: the link into the next cell says it is one.
    const NavNode& next = nav.node(path_[at_]);
    if (at_ > 0 && nav.move_between(path_[at_ - 1], path_[at_]) == NavMove::Jump && me.on_ground) {
        const float dx = next.pos.x - feet.x, dz = next.pos.z - feet.z;
        if (dx * dx + dz * dz < 90.0f * 90.0f) cmd.buttons |= proto::kButtonJump;
    }
    // Under anything that only a crouching player fits beneath.
    const int here = at_ > 0 ? path_[at_ - 1] : path_[at_];
    if (next.crouch || nav.node(here).crouch) cmd.buttons |= proto::kButtonCrouch;
    return true;
}

}  // namespace lsf
