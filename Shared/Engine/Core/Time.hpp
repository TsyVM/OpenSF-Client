#pragma once

#include "Engine/Core/Types.hpp"

namespace eng::time {

// Seconds since process start, from the performance counter.
double now();

// Sleeps close to `seconds`: coarse Sleep, then a busy wait for the last millisecond.
void sleep_precise(double seconds);

// Asks the scheduler for 1 ms timer resolution for the lifetime of the object.
struct TimerResolution {
    TimerResolution();
    ~TimerResolution();
};

}  // namespace eng::time
