// vangui_signals.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Pillar 3: signals / slots (header-only).
//
// The one intentional retained-state concession in the suite — fully opt-in,
// instance-based (no global mutable state), and composition-friendly.
//
//   * VANGUI_ENABLE_SIGNALS — required to make the types available.
//   * VANGUI_ENABLE_THREAD  — when also defined, VanSignal is thread-safe and
//       VanSignal::connect_queued() becomes available. Emitting from a worker
//       thread posts the call to the main thread via PostToMainThread().
//
// DESIGN
//   * RAII lifetime: VanConnection is move-only; destructor auto-disconnects.
//     Slots live in a shared control block — a connection that outlives its
//     signal disconnects harmlessly.
//   * SBO: std::function small-buffer-optimizes the common ≤2-capture case.
//   * Thread safety: guarded by a std::mutex inside Impl when THREAD is on.
//     The mutex is absent (zero-size) in single-threaded builds.
// -----------------------------------------------------------------------------

#pragma once

#ifdef VANGUI_ENABLE_SIGNALS

#include <functional>
#include <memory>
#include <vector>
#include <cstdint>
#include <utility>
#include <algorithm>

#ifdef VANGUI_ENABLE_THREAD
#  include "vangui_thread.h"  // PostToMainThread
#  include <mutex>
#endif

namespace VanGui {

// Move-only RAII connection handle. Disconnects on destruction or disconnect().
class VanConnection
{
public:
    VanConnection() = default;
    explicit VanConnection(std::function<void()> disconnector)
        : disc_(std::move(disconnector)), connected_(true) {}

    VanConnection(VanConnection&& o) noexcept { steal(std::move(o)); }
    VanConnection& operator=(VanConnection&& o) noexcept
    {
        if (this != &o) { disconnect(); steal(std::move(o)); }
        return *this;
    }
    VanConnection(const VanConnection&) = delete;
    VanConnection& operator=(const VanConnection&) = delete;
    ~VanConnection() { disconnect(); }

    void disconnect() noexcept
    {
        if (connected_ && disc_) disc_();
        connected_ = false;
        disc_      = nullptr;
    }

    // Keep the slot alive for the lifetime of the signal; drop this handle.
    void release() noexcept { connected_ = false; disc_ = nullptr; }

    [[nodiscard]] bool connected() const noexcept { return connected_; }

private:
    void steal(VanConnection&& o) noexcept
    {
        disc_      = std::move(o.disc_);
        connected_ = o.connected_;
        o.connected_ = false;
        o.disc_      = nullptr;
    }

    std::function<void()> disc_;
    bool                  connected_ = false;
};

template <class... Args>
class VanSignal
{
public:
    using SlotFn = std::function<void(Args...)>;

    VanSignal() : impl_(std::make_shared<Impl>()) {}
    VanSignal(const VanSignal&) = delete;
    VanSignal& operator=(const VanSignal&) = delete;
    VanSignal(VanSignal&&) noexcept = default;
    VanSignal& operator=(VanSignal&&) noexcept = default;

    // Connect a direct slot. Invoked on whatever thread calls emit().
    // Thread-safe when VANGUI_ENABLE_THREAD is defined.
    [[nodiscard]] VanConnection connect(SlotFn slot)
    {
        const std::uint64_t id = [&]{
            lock_guard g(*impl_);
            const std::uint64_t i = impl_->next++;
            impl_->slots.push_back(Slot{ i, std::move(slot), true });
            return i;
        }();
        std::weak_ptr<Impl> w = impl_;
        return VanConnection([w, id]() noexcept {
            if (auto s = w.lock())
            {
                lock_guard g(*s);
                for (auto& sl : s->slots)
                    if (sl.id == id) { sl.live = false; break; }
            }
        });
    }

#ifdef VANGUI_ENABLE_THREAD
    // Connect a QUEUED slot: regardless of which thread emits the signal,
    // the slot is always invoked on the main thread via PostToMainThread().
    // Args must be copyable (they are captured into the posted lambda).
    [[nodiscard]] VanConnection connect_queued(SlotFn slot)
    {
        return connect([fn = std::move(slot)](Args... args) {
            PostToMainThread([fn, args...]() mutable {
                fn(args...);
            });
        });
    }
#endif

    // Emit: call every live slot in connection order.
    // Thread-safe when VANGUI_ENABLE_THREAD is defined.
    void emit(Args... args) const
    {
        // Snapshot live slots under the lock so we don't hold it during calls.
        std::vector<SlotFn> live;
        {
            lock_guard g(*impl_);
            live.reserve(impl_->slots.size());
            for (const auto& sl : impl_->slots)
                if (sl.live) live.push_back(sl.fn);
            compact_unlocked();
        }
        for (auto& fn : live) fn(args...);
    }

    void operator()(Args... args) const { emit(args...); }

    void disconnect_all() noexcept
    {
        lock_guard g(*impl_);
        impl_->slots.clear();
    }

    [[nodiscard]] std::size_t slot_count() const noexcept
    {
        lock_guard g(*impl_);
        std::size_t n = 0;
        for (const auto& s : impl_->slots) if (s.live) ++n;
        return n;
    }

private:
    struct Slot { std::uint64_t id; SlotFn fn; bool live; };

    struct Impl
    {
        std::vector<Slot>  slots;
        std::uint64_t      next = 1;
        bool               needs_compact = false;
#ifdef VANGUI_ENABLE_THREAD
        mutable std::mutex mutex;
#endif
    };

    // RAII lock that is a no-op in single-threaded builds.
    struct lock_guard
    {
#ifdef VANGUI_ENABLE_THREAD
        explicit lock_guard(const Impl& impl) : lk_(impl.mutex) {}
        std::unique_lock<std::mutex> lk_;
#else
        explicit lock_guard(const Impl&) {}
#endif
    };

    // Erase dead slots. Must be called while the lock is NOT held
    // (or the lock is already held by the caller — the compact inside emit
    //  is called before releasing, hence the _unlocked suffix for clarity).
    void compact_unlocked() const
    {
        auto& v = impl_->slots;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [](const Slot& s) { return !s.live; }),
                v.end());
    }

    std::shared_ptr<Impl> impl_;
};

} // namespace VanGui

#endif // VANGUI_ENABLE_SIGNALS
