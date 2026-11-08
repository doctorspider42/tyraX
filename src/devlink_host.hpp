// devlink, the editor's end (docs/devlink.md).
//
// On a console the game no longer writes its Live Debugger report to a host:
// file: the IOP sends it here as numbered UDP chunks ('TXDO'). This receiver
// reassembles each report and writes it to bin/livedbg.bin exactly as the
// game used to, so everything that reads that file - the parsers, the
// stale-snapshot and hang detection - works unchanged.
//
// It listens on wire's devlink socket, the one the editor SENDS its datagrams
// from: the console answers to that address and port, which is what lets a
// stateful firewall pass the answer in. So the receiver only hears anything
// once the editor has sent the console something (a Live Debugger command or a
// Remote Pad state) - which the game also needs before it reports at all.
//
// No GL, no ImGui, no project.hpp.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace devlink {

// 'TXDO' - one chunk of a report: u32 magic, u32 seq, u32 (count << 16 |
// index), u32 total length, then up to kChunk bytes.
inline constexpr uint32_t kOutMagic = 0x4F445854U;
inline constexpr int kChunk = 1024;
inline constexpr int kReportMax = 32 * 1024;  // Tyra::DevLink::kReportMax

// 'TXDF' - the same chunk layout, carrying one SEGMENT of a file the game
// writes (phase 3): the reassembled report is {u32 file id, u32 file seq,
// u32 offset, u32 file length} followed by that segment's bytes.
inline constexpr uint32_t kOutFileMagic = 0x46445854U;

// 'TXFP' - a chunk of a bin/ file the editor pushes to the game's mirror:
// u32 magic, u32 file id, u32 seq (content hash), u32 file length, u32 chunk
// index, u32 chunk count, then up to kChunk bytes. Length 0 = deleted.
inline constexpr uint32_t kFilePushMagic = 0x50465854U;
inline constexpr int kFilePushHeader = 24;
inline constexpr uint32_t kDeletedSeq = 0xFFFFFFFFU;

/** The id both ends use for a file name: FNV-1a, never 0 - the twin of
 * fileId() in vendor/tyra/engine/src/debug/devlink.cpp. */
uint32_t fileId(const std::string& name);

/** Reassembles one report from its chunks. Pure - no sockets, no files - so a
 * harness can feed it datagrams in any order. `magic` picks which chunk
 * stream it accepts (kOutMagic or kOutFileMagic). */
class Reassembler {
public:
    explicit Reassembler(uint32_t magic = kOutMagic) : magic_(magic) {}
    /** Feeds one datagram. Returns true when it completed a report; the bytes
     * are then in report(). A chunk of a different report than the one in
     * progress abandons it (lost chunks are normal: the next report
     * supersedes); a repeat of the report just completed is ignored. */
    bool feed(const unsigned char* data, int len);
    const std::string& report() const { return done_; }
    uint32_t lastSeq() const { return doneSeq_; }

private:
    uint32_t magic_;
    uint32_t seq_ = 0, doneSeq_ = 0, total_ = 0, count_ = 0, have_ = 0;
    bool active_ = false;
    std::string buf_, got_, done_;
};

/** Builds a whole file from its segments (TXDF reports). Pure, like the
 * Reassembler. A segment of a newer version of the file starts over. */
class FileAssembler {
public:
    /** Returns true when `segment` (a reassembled TXDF report) completed its
     * file; then id() names it and bytes() holds it. */
    bool feed(const std::string& segment);
    uint32_t id() const { return doneId_; }
    const std::string& bytes() const { return done_; }

private:
    uint32_t id_ = 0, seq_ = 0, doneId_ = 0;
    size_t have_ = 0;
    std::string buf_, done_;
};

// 'TXLG' - a frame's worth of the game's LOG lines (phase 4), same chunking.
inline constexpr uint32_t kLogMagic = 0x474C5854U;
// 'TXWD' - the IOP's watchdog (phase 5): {magic, ms the game has been
// silent, last report seq heard, 0}.
inline constexpr uint32_t kWatchdogMagic = 0x44575854U;
// 'TXHI' - the editor's "send your reports here", every kHelloMs: the IOP
// only ever sends to the last address it heard from, and a CLI session (or
// an editor with no debugger attached) would otherwise never be told.
inline constexpr uint32_t kHelloMagic = 0x49485854U;
inline constexpr int kHelloMs = 500;

/** The worker: receives, reassembles, writes bin/livedbg.bin and the files
 * the game writes through devlink; hands log lines and watchdog notices to
 * the callbacks. Owned by the Runner for the length of a console session, so
 * the CLI gets the same [ps2] lines as the editor. */
class Receiver {
public:
    ~Receiver();
    /** Set before serve(); called on the worker thread. */
    std::function<void(const std::string&)> onLine;
    std::function<void(uint32_t silentMs)> onWatchdog;
    /** Where to write and which console to accept datagrams from. Empty
     * consoleIp stops the worker. Cheap to call every tick with the same
     * values. */
    void serve(const std::string& binDir, const std::string& consoleIp);
    uint32_t reports() const { return reports_.load(); }

private:
    void run();
    std::mutex mu_;
    std::string binDir_, ip_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<uint32_t> reports_{0};
};

// The bin/ files the game reads through a devlink mirror, and the ones it
// writes through devlink. The twins are the readFile/writeFile call sites in
// the generated runtimes (src/templates.cpp).
inline const char* const kMirroredFiles[] = {"livelink.bin", "livelogic.bin",
                                             "livetex.bin", "livetime.rst"};
inline const char* const kGameWrittenFiles[] = {"livetime.bin"};

/** Pushes the mirrored bin/ files to the console: on any change (size or
 * time), and every couple of seconds regardless - the game may have booted
 * after the last change, and a re-push of unchanged content costs the IOP one
 * comparison (seq = content hash). A deleted file is pushed as length 0.
 * Paced: a few dozen chunks per call, so a large file does not overrun the
 * IOP's receive queue. Main-thread object; call tick() every frame. */
class FilePusher {
public:
    void tick(const std::string& binDir, const std::string& consoleIp, double now);

private:
    struct File {
        std::string name;
        uint32_t id = 0;
        long long stamp = -2;     // size ^ mtime last seen; -1 = absent
        std::string bytes;        // what is being pushed
        uint32_t seq = 0;
        uint32_t next = 0;        // next chunk index to send
        uint32_t count = 0;
        bool sending = false;
    };
    std::vector<File> files_;
    double nextStat_ = 0.0, nextRepush_ = 0.0;
    std::string dir_, ip_;
};

}  // namespace devlink
