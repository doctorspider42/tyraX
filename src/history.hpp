#pragma once

#include <utility>
#include <vector>

#include "project.hpp"

// One editable-scene state (everything undo/redo affects).
struct SceneSnapshot {
    TerrainConfig terrain;
    std::vector<SceneObject> objects;
};

inline bool operator==(const SceneSnapshot& a, const SceneSnapshot& b) {
    return a.terrain.width == b.terrain.width && a.terrain.depth == b.terrain.depth &&
           a.objects == b.objects;
}

// Linear undo/redo stack. entries_[index_] is always the current state;
// pushing after an undo discards the redo tail (classic editor behavior).
class History {
public:
    static constexpr int kMaxEntries = 100;

    void reset(SceneSnapshot s) {
        entries_.assign(1, std::move(s));
        index_ = 0;
    }

    void push(SceneSnapshot s) {
        if (index_ >= 0 && entries_[index_] == s) return;  // no-op change
        entries_.resize(index_ + 1);
        entries_.push_back(std::move(s));
        ++index_;
        if ((int)entries_.size() > kMaxEntries) {
            entries_.erase(entries_.begin());
            --index_;
        }
    }

    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ + 1 < (int)entries_.size(); }
    const SceneSnapshot& undo() { return entries_[--index_]; }
    const SceneSnapshot& redo() { return entries_[++index_]; }

    bool empty() const { return entries_.empty(); }
    const SceneSnapshot& current() const { return entries_[index_]; }

    // Persistence (solution file)
    const std::vector<SceneSnapshot>& entries() const { return entries_; }
    int index() const { return index_; }
    void restore(std::vector<SceneSnapshot> entries, int index) {
        entries_ = std::move(entries);
        index_ = index;
    }

private:
    std::vector<SceneSnapshot> entries_;
    int index_ = -1;
};
