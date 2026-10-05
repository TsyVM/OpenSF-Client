// Where the frame's time and the card's memory go, for the profiler overlay.
//
// GPU time: a timestamp at each named mark (the scene, each effect, the HUD); a section runs from
// its mark to the next. Timestamps are read back a few frames later without stalling and smoothed.
// Memory: the process's use of the card's own memory against the budget the system gives it, and
// the size of each texture, target and buffer (from its description).
#pragma once

#include "Engine/Render/Device.hpp"

#include <memory>
#include <string>
#include <vector>

namespace eng {

class GpuProfiler {
public:
    bool init(Device& device);
    void shutdown();
    bool enabled = false;

    void begin_frame();
    // Ends the section running and starts `name` (a string literal: kept by pointer).
    void mark(const char* name);
    void end_frame();

    struct Section {
        const char* name = "";
        float ms = 0;     // smoothed
    };
    const std::vector<Section>& sections() const { return sections_; }
    float total_ms() const { return total_ms_; }

    // The card: its name, dedicated memory, and what this process uses of its budget (bytes).
    std::string adapter_name;
    size_t adapter_memory = 0;
    bool video_memory(size_t& used, size_t& budget) const { return device_ && device_->video_memory(used, budget); }

    // What a texture or buffer takes on the card (0 for none).
    static size_t bytes_of(const Texture* t) { return t ? t->bytes() : 0; }
    static size_t bytes_of(const TextureRef& t) { return bytes_of(t.get()); }
    static size_t bytes_of(const BufferRef& b) { return b ? b->bytes() : 0; }

private:
    static constexpr int kFrames = 4, kMarks = 24;
    struct Frame {
        std::unique_ptr<TimerFrame> timer;
        const char* names[kMarks]{};
        int count = 0;
        bool pending = false;
    };
    void collect(Frame& f);

    Device* device_ = nullptr;
    Frame frames_[kFrames];
    int current_ = 0;
    bool in_frame_ = false;
    std::vector<Section> sections_;
    float total_ms_ = 0;
};

}  // namespace eng
