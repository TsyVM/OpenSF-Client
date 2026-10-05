// The soldiers in the menus: a force's own 3D model (the one the match draws), posed by the
// lobby's interface clips and drawn with the match's renderer into a picture the interface shows.
// The Character Shop's cards are stills made once; its details and the room's My Soldier turn
// slowly and are drawn again each time they are asked for.
//
// A soldier is a force with the character parts it wears (Game/Items.hpp): each combination is a
// model of its own, loaded the first time it is asked for.
//
// The models load on a thread of their own, in the order they are asked for, so a screen full
// of them fills in as they arrive; until then a picture is invalid and the caller shows it is
// on its way.
#pragma once

#include "Game/Render/WorldRenderer.hpp"
#include "Game/Ui/Atlas.hpp"

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace lsf {

class App;

class ModelStage {
public:
    explicit ModelStage(App& app);
    ~ModelStage();

    // A still of the force, three-quarters on (the shop's cards).
    ui::Picture photo(u8 force);
    // The force turning (the shop's details, the room) in the parts `parts`: drawn again on every
    // call, `yaw` degrees beyond its three-quarters.
    ui::Picture live(u8 force, float yaw, std::span<const u16> parts = {});
    // Asked for and not here yet.
    bool loading(u8 force, std::span<const u16> parts = {}) const;

private:
    struct Soldier {
        std::shared_ptr<sf::Model> model;
        std::shared_ptr<sf::ModelAnimation> pose;
        std::unique_ptr<ModelGpu> gpu;
        eng::Aabb box;
        bool failed = false;
        double used = 0;   // last asked for (the oldest looks go first)
    };
    // Looks kept loaded at once: trying parts on in the Character Shop makes a model of each
    // combination, and kept for good they filled the memory a few hundred at a time. A force's
    // bare look (the shop's card) is always kept.
    static constexpr size_t kKeepLooks = 32;
    void forget_oldest(const std::string& keep);
    struct Shot {
        eng::TextureRef msaa, depth, picture;
        int w = 0, h = 0;
        bool drawn = false;
    };

    struct Look {
        u8 force = 0;
        std::vector<u16> parts;
    };
    static std::string look_key(u8 force, std::span<const u16> parts);
    bool ready();
    Soldier* soldier(const std::string& key);
    void pose(const Soldier& s, float t, std::vector<eng::Mat4>& skin) const;
    void request(const std::string& key, u8 force, std::span<const u16> parts);
    void load_thread();
    Shot& shot_for(std::map<std::string, Shot>& shots, const std::string& key, int w, int h);
    void draw(Soldier& s, Shot& shot, float yaw, double time);

    App& app_;
    WorldRenderer renderer_;
    bool renderer_ok_ = false, renderer_tried_ = false;
    int samples_ = 1;
    std::map<std::string, Soldier> soldiers_;       // uploaded (main thread), by look_key
    std::map<std::string, Shot> photos_, lives_;
    // Loading.
    struct Loaded {
        std::shared_ptr<sf::Model> model;
        std::shared_ptr<sf::ModelAnimation> pose;
    };
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::string> queue_;
    std::map<std::string, Look> looks_;
    std::map<std::string, Loaded> loaded_;          // read, waiting to go onto the card
    std::map<std::string, bool> asked_;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace lsf
