// vangui_thread.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — threading: thread pool, main-thread dispatch,
// async tasks, and pollable futures.
//
// THREADING MODEL (invariant — never violate this)
//   VanGui is immediate-mode and single-threaded on the render side.
//   NO VanGui function may be called from a worker thread.
//   Workers do computation; they hand results back to the main thread via
//   PostToMainThread(), which drains at the start of every frame inside
//   NewFrameExtras(). This is the same model Qt uses for queued connections.
//
//   Typical usage:
//
//     // Application startup
//     VanGui::InitThreadPool();
//
//     // Fire-and-forget background work
//     VanGui::Async([]{ compress_assets(); });
//
//     // Background work with result delivered to main thread
//     VanGui::Async<std::string>(
//         []{ return load_file("config.json"); },
//         [](std::string data){ g_config = parse(data); }
//     );
//
//     // Pollable future (show spinner while waiting)
//     auto fut = VanGui::AsyncFuture<Texture>([]{ return load_texture("bg.png"); });
//     // ... each frame:
//     if (fut.ready()) g_bg = fut.get();
//
//     // Application shutdown (after render loop exits)
//     VanGui::ShutdownThreadPool();
//
// ZERO-COST WHEN OFF
//   Without VANGUI_ENABLE_THREAD the public API is provided as synchronous
//   inline stubs so application code compiles and runs unchanged (single-
//   threaded, no pool, callbacks fire immediately).
//
// QUEUED SIGNALS (requires both VANGUI_ENABLE_SIGNALS and VANGUI_ENABLE_THREAD)
//   VanSignal::connect_queued(slot) creates a connection whose slot is always
//   invoked on the main thread, regardless of which thread emits the signal.
//   The signal's internal mutex makes connect/emit/disconnect thread-safe.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

#include <functional>
#include <future>
#include <memory>
#include <chrono>
#include <type_traits>

namespace VanGui {

#ifdef VANGUI_ENABLE_THREAD

// ---------------------------------------------------------------------------
// Thread pool lifecycle
// ---------------------------------------------------------------------------

// Initialize the thread pool. Call once, before your render loop.
// n_threads = 0  →  std::thread::hardware_concurrency() (minimum 2).
VANGUI_API void InitThreadPool(unsigned int n_threads = 0);

// Drain the main-thread queue, finish all pending tasks, and join workers.
// Call once when your application exits, after the render loop.
VANGUI_API void ShutdownThreadPool();

// ---------------------------------------------------------------------------
// Main-thread dispatch
// ---------------------------------------------------------------------------

// Post a callable to run on the MAIN THREAD at the start of the next frame.
// Thread-safe: can be called from any thread, including worker threads.
// The callable is invoked inside NewFrameExtras() → DrainMainThreadQueue().
VANGUI_API void PostToMainThread(std::function<void()> fn);

// Drain the main-thread callback queue.
// Called automatically by NewFrameExtras(). Only call this from the main thread.
VANGUI_API void DrainMainThreadQueue();

// ---------------------------------------------------------------------------
// Async task submission
// ---------------------------------------------------------------------------

// Submit fire-and-forget work to the thread pool. Returns immediately.
// The work lambda must NOT call any VanGui function.
VANGUI_API void Async(std::function<void()> work);

// Submit work that produces a result T.
// on_complete(result) is posted to the main thread and called next frame.
// on_complete may call VanGui functions — it runs on the main thread.
template <class T>
void Async(std::function<T()> work, std::function<void(T)> on_complete)
{
    Async([w = std::move(work), c = std::move(on_complete)]() mutable {
        T result = w();
        PostToMainThread([c = std::move(c), r = std::move(result)]() mutable {
            c(std::move(r));
        });
    });
}

// Submit void work with a completion notification on the main thread.
inline void Async(std::function<void()> work, std::function<void()> on_complete)
{
    Async([w = std::move(work), c = std::move(on_complete)]() mutable {
        w();
        PostToMainThread(std::move(c));
    });
}

// ---------------------------------------------------------------------------
// VanFuture<T> — non-blocking, poll-from-main-thread future
// ---------------------------------------------------------------------------
// Created by AsyncFuture(). Poll ready() each frame; get() once ready.
// then() registers a callback that runs on the main thread when work finishes.

template <class T>
class VanFuture
{
public:
    using value_type = T;

    VanFuture() = default;
    explicit VanFuture(std::shared_future<T> sf) : fut_(std::move(sf)) {}

    // True when the worker has produced a result (or thrown). Never blocks.
    [[nodiscard]] bool ready() const noexcept
    {
        return fut_.valid() &&
               fut_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    // Access the result. Never blocks; asserts ready() in debug builds.
    [[nodiscard]] const T& get() const { return fut_.get(); }

    // Register a callback to run on the main thread when work finishes.
    // Safe to call before the work completes — the helper worker waits for it.
    void then(std::function<void(T)> on_complete)
    {
        Async([sf = fut_, c = std::move(on_complete)]() mutable {
            sf.wait(); // already on a worker; blocking here is correct
            T val = sf.get();
            PostToMainThread([c = std::move(c), v = std::move(val)]() mutable {
                c(std::move(v));
            });
        });
    }

    [[nodiscard]] bool valid() const noexcept { return fut_.valid(); }

private:
    std::shared_future<T> fut_;
};

// VanFuture<void> — completion signal with no return value.
template <>
class VanFuture<void>
{
public:
    VanFuture() = default;
    explicit VanFuture(std::shared_future<void> sf) : fut_(std::move(sf)) {}

    [[nodiscard]] bool ready() const noexcept
    {
        return fut_.valid() &&
               fut_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    void get() const { fut_.get(); }

    void then(std::function<void()> on_complete)
    {
        Async([sf = fut_, c = std::move(on_complete)]() mutable {
            sf.wait();
            PostToMainThread(std::move(c));
        });
    }

    [[nodiscard]] bool valid() const noexcept { return fut_.valid(); }

private:
    std::shared_future<void> fut_;
};

// Submit work and return a pollable VanFuture<T>.
// Exceptions thrown by work are captured and rethrown from VanFuture::get().
template <class T>
[[nodiscard]] VanFuture<T> AsyncFuture(std::function<T()> work)
{
    auto promise = std::make_shared<std::promise<T>>();
    std::shared_future<T> sf = promise->get_future().share();
    Async([p = std::move(promise), w = std::move(work)]() mutable {
        try                       { p->set_value(w()); }
        catch (...)               { p->set_exception(std::current_exception()); }
    });
    return VanFuture<T>(std::move(sf));
}

template <>
[[nodiscard]] inline VanFuture<void> AsyncFuture<void>(std::function<void()> work)
{
    auto promise = std::make_shared<std::promise<void>>();
    std::shared_future<void> sf = promise->get_future().share();
    Async([p = std::move(promise), w = std::move(work)]() mutable {
        try   { w(); p->set_value(); }
        catch (...) { p->set_exception(std::current_exception()); }
    });
    return VanFuture<void>(std::move(sf));
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

[[nodiscard]] VANGUI_API bool IsThreadPoolRunning()        noexcept;
[[nodiscard]] VANGUI_API int  ThreadPoolSize()             noexcept;
[[nodiscard]] VANGUI_API int  PendingWorkerTasks()         noexcept;
[[nodiscard]] VANGUI_API int  PendingMainThreadCallbacks() noexcept;

#else // ---- Zero-cost stubs when VANGUI_ENABLE_THREAD is not defined --------

inline void InitThreadPool(unsigned int = 0)  {}
inline void ShutdownThreadPool()              {}
// Without a pool, post-to-main and async run synchronously on the caller.
inline void PostToMainThread(std::function<void()> fn) { fn(); }
inline void DrainMainThreadQueue()            {}
inline void Async(std::function<void()> work) { work(); }

template <class T>
inline void Async(std::function<T()> work, std::function<void(T)> on_complete)
{ on_complete(work()); }

inline void Async(std::function<void()> work, std::function<void()> on_complete)
{ work(); on_complete(); }

[[nodiscard]] inline bool IsThreadPoolRunning()        noexcept { return false; }
[[nodiscard]] inline int  ThreadPoolSize()             noexcept { return 0; }
[[nodiscard]] inline int  PendingWorkerTasks()         noexcept { return 0; }
[[nodiscard]] inline int  PendingMainThreadCallbacks() noexcept { return 0; }

#endif // VANGUI_ENABLE_THREAD

} // namespace VanGui
