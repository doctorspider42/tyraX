#pragma once

// A bounded, crash-safe copy of one console session's log on disk
// (docs/ps2link-setup.md, "The session log").
//
// Run on PS2 streams the console's output into the Output panel, which is
// memory: close the editor, lose the editor to a crash, or simply not look, and
// the only record of what the game said is gone. This keeps it in
// <project>/logs/ps2-YYYYMMDD-HHMMSS-mmm.log as well.
//
// Bounded two ways, both editor preferences (Edit > Preferences > Real PS2):
// - a file keeps the LAST maxLines lines. It is rewritten down to maxLines each
//   time it reaches twice that, so it holds between maxLines and 2*maxLines, the
//   write stays an append, and the end of a session - what a crash leaves - is
//   always there;
// - at most keepFiles session files stay in logs/; opening a new one deletes the
//   oldest beyond that.
//
// Every line is flushed as it is written: the point is to survive the editor
// dying, and the console's line rate is a few lines a second.

#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

namespace sessionlog {

class File {
public:
    ~File();

    // Creates <dir>/<prefix>-<local time>.log after pruning <dir> down to
    // keepFiles - 1 older <prefix>-*.log files. maxLines <= 0 disables the file
    // (open returns false and write does nothing). Returns true when a file is
    // open.
    bool open(const std::string& dir, const std::string& prefix, int maxLines,
              int keepFiles);
    void write(const std::string& line);
    void close();
    // The file being written; empty when none is open.
    std::string path() const;

    // Deletes all but the newest `keep` <prefix>-*.log files in dir. Exposed
    // for the tests; open() calls it with keepFiles - 1.
    static void prune(const std::string& dir, const std::string& prefix,
                      int keep);

private:
    void rewriteTail();  // caller holds mutex_

    mutable std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::string path_;
    std::deque<std::string> tail_;  // the newest maxLines_ lines
    int maxLines_ = 0;
    size_t linesInFile_ = 0;
};

}  // namespace sessionlog
