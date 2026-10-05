#include "Engine/Core/Time.hpp"

#ifndef _WIN32
#include <thread>
#include <time.h>

namespace eng::time {

namespace {

double monotonic() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}
const double g_start = monotonic();

}  // namespace

double now() { return monotonic() - g_start; }

void sleep_precise(double seconds) {
    if (seconds <= 0) return;
    const double end = now() + seconds;
    if (seconds > 0.0015) {
        timespec ts{};
        const double s = seconds - 0.001;
        ts.tv_sec = time_t(s);
        ts.tv_nsec = long((s - double(ts.tv_sec)) * 1e9);
        nanosleep(&ts, nullptr);
    }
    while (now() < end) std::this_thread::yield();
}

TimerResolution::TimerResolution() {}
TimerResolution::~TimerResolution() {}

}  // namespace eng::time

#else
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002   // Windows 10 1803 and later
#endif

namespace eng::time {

namespace {

struct Clock {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    Clock() {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
    }
};

Clock& clock() {
    static Clock c;
    return c;
}

}  // namespace

double now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - clock().start.QuadPart) / double(clock().frequency.QuadPart);
}

// A high-resolution waitable timer sleeps to within a fraction of a millisecond, so only that
// fraction is left to spin. `Sleep` is only as fine as the system timer, which Windows 11 leaves at
// 15.6 ms for a process with no visible window whatever it asked for -- so a dedicated server that
// slept with it spun away the rest, and one match cost most of a core (measured with SightBot).
void sleep_precise(double seconds) {
    if (seconds <= 0) return;
    const double end = now() + seconds;
    static thread_local HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    constexpr double kSpin = 0.0002;
    if (timer && seconds > kSpin) {
        LARGE_INTEGER due;
        due.QuadPart = -LONGLONG((seconds - kSpin) * 1e7);   // relative, in 100 ns units
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
    } else if (seconds > 0.002) {
        Sleep(DWORD((seconds - 0.0015) * 1000.0));
    }
    while (now() < end) YieldProcessor();
}

TimerResolution::TimerResolution() { timeBeginPeriod(1); }
TimerResolution::~TimerResolution() { timeEndPeriod(1); }

}  // namespace eng::time
#endif
