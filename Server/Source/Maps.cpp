#include "Maps.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"

#include <algorithm>
#include <iterator>

namespace lsfs {

using eng::u32;
using eng::u8;

namespace {

std::shared_ptr<const ServerMap> read_map(const sf::Data& data, const std::string& id) {
    const double t0 = eng::time::now();
    std::string err;
    auto level = sf::load_level(data, id, sf::kLevelCollision | sf::kLevelGameplay, &err);
    if (!level) {
        LOG_WARN("Server map %s: %s", id.c_str(), err.c_str());
        return nullptr;
    }
    auto m = std::make_shared<ServerMap>();
    m->id = id;
    std::vector<u8> surfaces(level->collision_surfaces.size());
    for (size_t i = 0; i < surfaces.size(); ++i) surfaces[i] = u8(level->collision_surfaces[i]);
    m->collision.build(level->collision_vertices, level->collision_indices, surfaces);
    // The sight mesh: the same triangles less the glass, which an eye sees through.
    std::vector<u32> seen;
    seen.reserve(level->collision_indices.size());
    for (size_t t = 0; t + 2 < level->collision_indices.size(); t += 3)
        if (t / 3 >= level->collision_surfaces.size() || level->collision_surfaces[t / 3] != sf::Surface::Glass)
            seen.insert(seen.end(), {level->collision_indices[t], level->collision_indices[t + 1], level->collision_indices[t + 2]});
    m->sight.build(level->collision_vertices, seen);
    m->spawns = level->spawns;
    m->rules = lsf::map_rules(*level);
    m->objectives = level->objectives;
    m->zones = level->zones;
    m->places = level->places;
    m->npc_spots = level->npc_spots;
    m->ammo_spots = level->ammo_spots;
    m->zombie_spawns = level->zombie_spawns;
    m->human_spawns = level->human_spawns;
    m->bounds = m->collision.bounds();
    const double t1 = eng::time::now();
    m->nav.build(m->collision, lsf::MovementDef{}, m->bounds);
    const auto& ns = m->nav.stats();
    LOG_INFO("Server map %s: %zu triangles (%zu seen through), %zu spawns, nav %zu cells of %.0f cm (%zu in the largest region) in %.0f + %.0f ms",
             id.c_str(), m->collision.triangle_count(), m->collision.triangle_count() - m->sight.triangle_count(), m->spawns.size(), ns.nodes,
             double(ns.cell), ns.largest, (t1 - t0) * 1000.0, ns.build_ms);
    return m;
}

}  // namespace

bool MapCache::open(const std::filesystem::path& client_data) {
    if (client_data.empty() || !data_.open(client_data)) {
        if (!client_data.empty()) LOG_WARN("Server: %s is not a Soldier Front data folder: no bots, everyone sent to everyone",
                                           eng::str::narrow(client_data.wstring()).c_str());
        return false;
    }
    LOG_INFO("Server: maps from %s", eng::str::narrow(client_data.wstring()).c_str());
    return true;
}

std::shared_ptr<const ServerMap> MapCache::get(const std::string& id) {
    if (!available()) return nullptr;
    std::lock_guard lock(mutex_);
    auto it = maps_.find(id);
    if (it == maps_.end())
        it = maps_.emplace(id, std::async(std::launch::async, [this, id] { return read_map(data_, id); }).share()).first;
    if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    return it->second.get();
}

const std::vector<lsf::MapRules>& MapCache::all_rules() {
    std::lock_guard lock(mutex_);
    if (!rules_read_ && available()) {
        rules_read_ = true;
        for (const sf::LevelListing& l : sf::list_levels(data_))
            if (l.playable)
                if (auto level = sf::load_level(data_, l.id, sf::kLevelGameplay)) rules_.push_back(lsf::map_rules(*level));
        LOG_INFO("Server: %zu maps' game types read", rules_.size());
    }
    return rules_;
}

std::string MapCache::pick_map(lsf::Mode mode, const std::string& asked, eng::Rng& rng, const lsf::ServerGames& games, const std::vector<std::string>& only) {
    auto in_only = [&](std::string_view id) {
        return std::any_of(only.begin(), only.end(), [&](const std::string& o) { return eng::str::iequals(o, id); });
    };
    const std::vector<lsf::MapRules>& all = all_rules();
    if (all.empty()) {
        // No data to say which map plays what: the room's own choice, or one of the hot five.
        if (!lsf::is_random_map(asked) && games.takes_map(asked) && (only.empty() || in_only(asked))) return asked;
        std::vector<std::string> pool;
        for (const std::string& o : only)
            if (games.takes_map(o) && !lsf::is_random_map(o)) pool.push_back(o);
        if (pool.empty())
            for (const char* h : lsf::kHotMaps)
                if (games.takes_map(h)) pool.push_back(h);
        return pool.empty() ? std::string() : pool[rng.next() % pool.size()];
    }
    std::vector<std::string> offered, everywhere;
    for (const lsf::MapRules& r : all)
        if (lsf::mode_offers(mode, r) && games.takes_map(r.id)) {
            everywhere.push_back(r.id);
            if (only.empty() || in_only(r.id)) offered.push_back(r.id);
        }
    // A channel whose maps do not play this game type: the server's others, as before.
    if (offered.empty()) offered = std::move(everywhere);
    if (offered.empty()) return {};
    if (asked == lsf::kHotRandom) {
        std::vector<std::string> hot;
        for (const char* h : lsf::kHotMaps)
            if (std::find(offered.begin(), offered.end(), h) != offered.end()) hot.push_back(h);
        if (!hot.empty()) return hot[rng.next() % hot.size()];
    }
    if (lsf::is_random_map(asked)) return offered[rng.next() % offered.size()];
    if (std::find(offered.begin(), offered.end(), asked) != offered.end()) return asked;
    // Switched off since the room chose it: drawn as All Random would be.
    if (!games.takes_map(asked)) return offered[rng.next() % offered.size()];
    return offered.front();
}

std::shared_ptr<const ServerMap> MapCache::wait(const std::string& id) {
    if (!available()) return nullptr;
    (void)get(id);
    std::shared_future<std::shared_ptr<const ServerMap>> f;
    {
        std::lock_guard lock(mutex_);
        f = maps_[id];
    }
    return f.valid() ? f.get() : nullptr;
}

void MapCache::mount(const std::filesystem::path& layer) {
    if (!available()) return;
    std::lock_guard lock(mutex_);
    for (auto& [id, f] : maps_)
        if (f.valid()) f.wait();
    maps_.clear();
    rules_.clear();
    rules_read_ = false;
    data_.set_session(layer);
}

}  // namespace lsfs
