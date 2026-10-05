// The maps as the server knows them: each one's collision, its sight mesh (the collision less the
// glass: what can be seen through), its spawns and, for bots, its navigation graph. Read from the
// Soldier Front client's own data (the game's, when it hosts; --data for the dedicated server),
// each on a worker the first time a match on it is started, so the server never stalls on one
// (TacticalFPS's ServerMap::navigation: a synchronous build froze its server for two seconds).
//
// Without a data folder there are no maps: the match runs as it always did, everyone sent to
// everyone, and no bots.
#pragma once

#include "ServerOnly.hpp"

#include "Engine/Core/Hash.hpp"
#include "Engine/Physics/CollisionMesh.hpp"
#include "Game/Modes.hpp"
#include "Game/Navigation.hpp"
#include "SF/Data.hpp"
#include "SF/Level.hpp"

#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lsfs {

struct ServerMap {
    std::string id;
    eng::CollisionMesh collision;   // what a soldier stands on and a bullet stops at
    eng::CollisionMesh sight;       // the same less the glass: what an eye cannot see through
    std::vector<sf::LevelSpawn> spawns;
    eng::Aabb bounds;
    lsf::NavGraph nav;              // where a bot can walk (Game/Navigation.hpp)
    // The game types' pieces (Game/Modes.hpp): what the map offers, its mission objects, the zones
    // each side is sent to, strongholds, supply and NPC spots, Horror Mode 2's own spawns.
    lsf::MapRules rules;
    std::vector<sf::LevelObjective> objectives;
    std::vector<sf::LevelZone> zones;
    std::vector<sf::LevelPlace> places;
    std::vector<eng::Vec3> npc_spots, ammo_spots;
    std::vector<sf::LevelSpawn> zombie_spawns, human_spawns;
};

class MapCache {
public:
    // The client's data folder (empty: none).
    bool open(const std::filesystem::path& client_data);
    bool available() const { return data_.is_open(); }

    // The map once it is read, else null -- and the reading started, on a worker.
    std::shared_ptr<const ServerMap> get(const std::string& id);
    // Blocks until the map is read (tools and tests).
    std::shared_ptr<const ServerMap> wait(const std::string& id);
    // Every map's game types (its world script only; read once, the first time it is asked).
    const std::vector<lsf::MapRules>& all_rules();
    // A map for a room that asked for a random one (Game/Modes.hpp kAllRandom, kHotRandom), or the
    // map itself when it is one the game type offers; empty when nothing will do. Never one the
    // server switched off (`games`); one of the channel's own (`only`) when any of them plays it.
    std::string pick_map(lsf::Mode mode, const std::string& asked, eng::Rng& rng, const lsf::ServerGames& games = {},
                         const std::vector<std::string>& only = {});
    const sf::Data& data() const { return data_; }
    // The server's packs laid out as archives (Game/PackMount.hpp): the maps they add are read as
    // the client's own are. Empty: none. Only at start and stop, with no match on any map.
    void mount(const std::filesystem::path& layer);

private:
    sf::Data data_;
    std::mutex mutex_;
    std::vector<lsf::MapRules> rules_;
    bool rules_read_ = false;
    std::map<std::string, std::shared_future<std::shared_ptr<const ServerMap>>> maps_;
};

}  // namespace lsfs
