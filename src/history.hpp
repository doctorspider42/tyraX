#pragma once

#include <utility>
#include <vector>

#include "project.hpp"

// One editable state (everything undo/redo affects): all scenes with their
// objects, flow graphs, terrain and lighting.
struct SceneSnapshot {
    std::vector<SceneData> scenes;
};

inline bool operator==(const SceneSnapshot& a, const SceneSnapshot& b) {
    return a.scenes == b.scenes;
}

// Linear undo/redo stack. entries_[index_] is always the current state;
// pushing after an undo discards the redo tail (classic editor behavior).
class History {
public:
    static constexpr int kMaxEntries = 100;

    void reset(SceneSnapshot s) {
        entries_.assign(1, std::move(s));
        index_ = 0;
        markSaved();
    }

    // Returns true when the snapshot differed and was pushed (a real edit);
    // false for a no-op change, so callers can skip marking the project dirty.
    bool push(SceneSnapshot s) {
        if (index_ >= 0 && entries_[index_] == s) return false;  // no-op change
        // The redo tail is about to go. If the saved state was in it, it is now
        // off this branch: remember how far past the fork it was.
        if (savedOff_ > 0 && savedAt_ > index_) {
            savedOff_ += savedAt_ - index_;
            savedAt_ = index_;
        } else if (savedOff_ == 0 && savedAt_ > index_) {
            savedOff_ = savedAt_ - index_;
            savedAt_ = index_;
        }
        entries_.resize(index_ + 1);
        entries_.push_back(std::move(s));
        ++index_;
        if ((int)entries_.size() > kMaxEntries) {
            entries_.erase(entries_.begin());
            --index_;
            --savedAt_;  // may go negative: the saved state fell off the front
        }
        return true;
    }

    // The state on disk is the current one (after a save or a load).
    void markSaved() {
        savedAt_ = index_;
        savedOff_ = 0;
    }
    // Undo/redo steps between the current state and the saved one - the
    // top bar's "N unsaved". When the saved state was cut off by a new edit
    // after an undo, the path goes back to the fork and out again.
    int stepsFromSaved() const {
        const int d = index_ - savedAt_;
        return (d < 0 ? -d : d) + savedOff_;
    }

    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ + 1 < (int)entries_.size(); }
    const SceneSnapshot& undo() { return entries_[--index_]; }
    const SceneSnapshot& redo() { return entries_[++index_]; }

    bool empty() const { return entries_.empty(); }
    const SceneSnapshot& current() const { return entries_[index_]; }

    // Persistence (history file)
    const std::vector<SceneSnapshot>& entries() const { return entries_; }
    int index() const { return index_; }
    void restore(std::vector<SceneSnapshot> entries, int index) {
        entries_ = std::move(entries);
        index_ = index;
        markSaved();
    }

private:
    std::vector<SceneSnapshot> entries_;
    int index_ = -1;
    int savedAt_ = 0;   // index of the saved state, or of the fork it is off
    int savedOff_ = 0;  // > 0: the saved state is that many steps past the fork
};
