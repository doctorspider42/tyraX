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
#include <mutex>
#include <string>
#include <thread>

namespace devlink {

// 'TXDO' - one chunk of a report: u32 magic, u32 seq, u32 (count << 16 |
// index), u32 total length, then up to kChunk bytes.
inline constexpr uint32_t kOutMagic = 0x4F445854U;
inline constexpr int kChunk = 1024;
inline constexpr int kReportMax = 32 * 1024;  // Tyra::DevLink::kReportMax

/** Reassembles one report from its chunks. Pure - no sockets, no files - so a
 * harness can feed it datagrams in any order. */
class Reassembler {
public:
    /** Feeds one datagram. Returns true when it completed a report; the bytes
     * are then in report(). A chunk of a different report than the one in
     * progress abandons it (lost chunks are normal: the next report
     * supersedes); a repeat of the report just completed is ignored. */
    bool feed(const unsigned char* data, int len);
    const std::string& report() const { return done_; }
    uint32_t lastSeq() const { return doneSeq_; }

private:
    uint32_t seq_ = 0, doneSeq_ = 0, total_ = 0, count_ = 0, have_ = 0;
    bool active_ = false;
    std::string buf_, got_, done_;
};

/** The worker: receives, reassembles, writes bin/livedbg.bin. */
class Receiver {
public:
    ~Receiver();
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

}  // namespace devlink
