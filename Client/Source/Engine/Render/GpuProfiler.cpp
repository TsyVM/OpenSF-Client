#include "Engine/Render/GpuProfiler.hpp"

#include <algorithm>
#include <cstring>

namespace eng {

bool GpuProfiler::init(Device& device) {
    device_ = &device;
    for (auto& f : frames_) {
        f.timer = device.create_timer_frame(kMarks);
        if (!f.timer) return false;
    }
    adapter_name = device.adapter_name();
    adapter_memory = device.adapter_memory();
    return true;
}

void GpuProfiler::shutdown() {
    for (auto& f : frames_) f = Frame{};
    device_ = nullptr;
}

void GpuProfiler::begin_frame() {
    if (!enabled || !device_) return;
    Frame& f = frames_[current_];
    if (!f.timer) return;
    if (f.pending) collect(f);
    if (f.pending) return;   // still in flight: skip timing this frame
    f.timer->begin();
    f.count = 0;
    in_frame_ = true;
}

void GpuProfiler::mark(const char* name) {
    if (!in_frame_) return;
    Frame& f = frames_[current_];
    if (f.count >= kMarks - 1) return;
    f.timer->stamp(f.count);
    f.names[f.count] = name;
    ++f.count;
}

void GpuProfiler::end_frame() {
    if (!in_frame_) return;
    Frame& f = frames_[current_];
    f.timer->stamp(f.count);
    f.names[f.count] = nullptr;
    ++f.count;
    f.timer->end();
    f.pending = true;
    in_frame_ = false;
    current_ = (current_ + 1) % kFrames;
    // Read back what the oldest frames have finished.
    for (auto& other : frames_)
        if (other.pending && &other != &f) collect(other);
}

void GpuProfiler::collect(Frame& f) {
    double t[kMarks]{};
    bool valid = false;
    if (!f.timer->poll(t, f.count, valid)) return;
    f.pending = false;
    if (!valid || f.count < 2) return;
    float total = 0;
    for (int i = 0; i + 1 < f.count; ++i) {
        const float ms = float(t[i + 1] - t[i]);
        total += ms;
        auto it = std::find_if(sections_.begin(), sections_.end(), [&](const Section& s) { return std::strcmp(s.name, f.names[i]) == 0; });
        if (it == sections_.end()) sections_.push_back({f.names[i], ms});
        else it->ms += (ms - it->ms) * 0.1f;
    }
    // A section not timed this frame (an effect switched off) fades out of the list.
    for (auto& s : sections_) {
        bool seen = false;
        for (int i = 0; i + 1 < f.count; ++i) seen |= std::strcmp(s.name, f.names[i]) == 0;
        if (!seen) s.ms *= 0.9f;
    }
    std::erase_if(sections_, [](const Section& s) { return s.ms < 0.001f; });
    total_ms_ += (total - total_ms_) * 0.1f;
}

}  // namespace eng
