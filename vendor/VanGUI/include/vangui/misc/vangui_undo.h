// vangui_undo.h
// -----------------------------------------------------------------------------
// VanGUI utility — undo / redo. Header-only, no VanGUI dependency, just include.
//
// Two flavors:
//   * VanUndoStack<T> : snapshot-based. Record a copy of your state before a
//                       change; undo()/redo() swap it back. Best for small
//                       value-type documents (settings structs, small models).
//   * VanCommandStack : command-based. Pair a do/undo lambda per action; best
//                       for large models where copying the whole state is costly.
// -----------------------------------------------------------------------------

#pragma once

#include <vector>
#include <functional>
#include <cstddef>
#include <utility>

namespace VanGui {

// ----- snapshot-based --------------------------------------------------------
template <class T>
class VanUndoStack
{
public:
    explicit VanUndoStack(std::size_t capacity = 128) : cap_(capacity ? capacity : 1) {}

    // Call BEFORE mutating `current` (records the pre-change state).
    void record(const T& current)
    {
        past_.push_back(current);
        if (past_.size() > cap_) past_.erase(past_.begin());
        future_.clear();
    }

    bool undo(T& current)
    {
        if (past_.empty()) return false;
        future_.push_back(current);
        current = std::move(past_.back());
        past_.pop_back();
        return true;
    }
    bool redo(T& current)
    {
        if (future_.empty()) return false;
        past_.push_back(current);
        current = std::move(future_.back());
        future_.pop_back();
        return true;
    }

    [[nodiscard]] bool can_undo() const noexcept { return !past_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !future_.empty(); }
    [[nodiscard]] std::size_t undo_count() const noexcept { return past_.size(); }
    [[nodiscard]] std::size_t redo_count() const noexcept { return future_.size(); }
    void clear() { past_.clear(); future_.clear(); }

private:
    std::vector<T> past_, future_;
    std::size_t    cap_;
};

// ----- command-based ---------------------------------------------------------
class VanCommandStack
{
public:
    explicit VanCommandStack(std::size_t capacity = 256) : cap_(capacity ? capacity : 1) {}

    // Execute `do_fn` now and remember how to undo/redo it.
    void perform(std::function<void()> do_fn, std::function<void()> undo_fn)
    {
        if (do_fn) do_fn();
        past_.push_back(Cmd{ std::move(undo_fn), std::move(do_fn) });
        if (past_.size() > cap_) past_.erase(past_.begin());
        future_.clear();
    }

    bool undo()
    {
        if (past_.empty()) return false;
        Cmd c = std::move(past_.back());
        past_.pop_back();
        if (c.undo) c.undo();
        future_.push_back(std::move(c));
        return true;
    }
    bool redo()
    {
        if (future_.empty()) return false;
        Cmd c = std::move(future_.back());
        future_.pop_back();
        if (c.redo) c.redo();
        past_.push_back(std::move(c));
        return true;
    }

    [[nodiscard]] bool can_undo() const noexcept { return !past_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !future_.empty(); }
    void clear() { past_.clear(); future_.clear(); }

private:
    struct Cmd { std::function<void()> undo, redo; };
    std::vector<Cmd> past_, future_;
    std::size_t      cap_;
};

} // namespace VanGui
