#include "devlink_host.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "platform.hpp"
#include "wire.hpp"

namespace devlink {

namespace {

uint32_t get32(const unsigned char* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

// Same atomic-replace dance as every other devkit file: the editor's own
// reader may have the file open between our two steps (Windows refuses to
// rename over an open file), so retry briefly instead of losing the report.
bool writeAtomic(const std::filesystem::path& target, const std::string& bytes) {
    namespace fs = std::filesystem;
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(bytes.data(), (std::streamsize)bytes.size());
        if (!f) return false;
    }
    std::error_code ec;
    for (int attempt = 0; attempt < 6; ++attempt) {
        fs::rename(tmp, target, ec);
        if (!ec) return true;
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (!ec) return true;
        platform::sleepMs(4);
    }
    fs::remove(tmp, ec);
    return false;
}

}  // namespace

uint32_t fileId(const std::string& name) {
    uint32_t h = 2166136261U;
    for (unsigned char c : name) h = (h ^ c) * 16777619U;
    return h ? h : 1U;
}

bool Reassembler::feed(const unsigned char* data, int len) {
    if (len < 16 || get32(data) != magic_) return false;
    const uint32_t seq = get32(data + 4);
    const uint32_t ci = get32(data + 8);
    const uint32_t count = ci >> 16, index = ci & 0xFFFFU;
    const uint32_t total = get32(data + 12);
    if (total == 0 || total > (uint32_t)kReportMax || count == 0 || index >= count ||
        count != (total + kChunk - 1) / kChunk)
        return false;
    // No "older/newer" ordering: a rebooted game numbers from 1 again, and on
    // a LAN reports do not overtake each other. A different seq than the one
    // in progress starts over; the one just completed is a duplicate.
    if (seq == doneSeq_ && !active_) return false;
    if (!active_ || seq != seq_) {
        active_ = true;
        seq_ = seq;
        total_ = total;
        count_ = count;
        have_ = 0;
        buf_.assign(total, '\0');
        got_.assign(count, '\0');
    }
    if (total != total_ || count != count_) return false;
    const uint32_t off = index * (uint32_t)kChunk;
    const uint32_t want = total - off < (uint32_t)kChunk ? total - off : (uint32_t)kChunk;
    if ((uint32_t)len - 16 != want || got_[index]) return false;
    std::memcpy(&buf_[off], data + 16, want);
    got_[index] = 1;
    if (++have_ < count_) return false;
    done_.swap(buf_);
    doneSeq_ = seq_;
    active_ = false;
    return true;
}

bool FileAssembler::feed(const std::string& segment) {
    if (segment.size() < 16) return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(segment.data());
    const uint32_t id = get32(p), seq = get32(p + 4), off = get32(p + 8),
                   total = get32(p + 12);
    const size_t n = segment.size() - 16;
    if (total == 0 || total > (64u << 20) || (size_t)off + n > total) return false;
    if (id != id_ || seq != seq_ || buf_.size() != total) {
        id_ = id;
        seq_ = seq;
        have_ = 0;
        buf_.assign(total, '\0');
    }
    // Segments are disjoint and arrive in order (one slot at a time), so a
    // byte count is enough to know the file is whole.
    std::memcpy(&buf_[off], p + 16, n);
    have_ += n;
    if (have_ < total) return false;
    done_.swap(buf_);
    doneId_ = id_;
    id_ = 0;
    return true;
}

Receiver::~Receiver() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
}

void Receiver::serve(const std::string& binDir, const std::string& consoleIp) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (binDir == binDir_ && consoleIp == ip_) return;
        binDir_ = binDir;
        ip_ = consoleIp;
    }
    if (consoleIp.empty()) {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
        return;
    }
    if (!worker_.joinable()) {
        stop_ = false;
        worker_ = std::thread([this] { run(); });
    }
}

void Receiver::run() {
    Reassembler reports(kOutMagic), segments(kOutFileMagic), logs(kLogMagic);
    FileAssembler files;
    std::string lastIp;
    unsigned char buf[16 + kChunk + 64];
    auto nextHello = std::chrono::steady_clock::now();
    bool silenceReported = false;
    while (!stop_) {
        std::string dir, ip;
        {
            std::lock_guard<std::mutex> lock(mu_);
            dir = binDir_;
            ip = ip_;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextHello && !ip.empty()) {
            nextHello = now + std::chrono::milliseconds(kHelloMs);
            const uint32_t hi[4] = {kHelloMagic, 0, 0, 0};
            (void)wire::sendDatagram(ip, 18198, hi, sizeof(hi));
        }
        std::string from;
        const int n = wire::recvDatagram(buf, sizeof(buf), 100, &from);
        if (n <= 0) continue;
        if (from != ip) continue;  // only the console we deploy to
        if (ip != lastIp) {  // a different console: its numbering starts over
            reports = Reassembler(kOutMagic);
            segments = Reassembler(kOutFileMagic);
            logs = Reassembler(kLogMagic);
            files = FileAssembler();
            lastIp = ip;
        }
        // The IOP repeats its watchdog every 2 s for as long as the game stays
        // silent; say it once per silence, and re-arm on the next sign of life.
        if (n == 16 && get32(buf) == kWatchdogMagic) {
            if (!silenceReported && onWatchdog) onWatchdog(get32(buf + 4));
            silenceReported = true;
            continue;
        }
        silenceReported = false;
        if (logs.feed(buf, n)) {
            if (onLine) {
                const std::string& text = logs.report();
                size_t start = 0;
                while (start < text.size()) {
                    size_t end = text.find('\n', start);
                    if (end == std::string::npos) end = text.size();
                    std::string line = text.substr(start, end - start);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) onLine(line);
                    start = end + 1;
                }
            }
            continue;
        }
        if (reports.feed(buf, n)) {
            if (writeAtomic(std::filesystem::path(dir) / "livedbg.bin", reports.report()))
                ++reports_;
            continue;
        }
        if (!segments.feed(buf, n) || !files.feed(segments.report())) continue;
        for (const char* name : kGameWrittenFiles)
            if (fileId(name) == files.id())
                writeAtomic(std::filesystem::path(dir) / name, files.bytes());
    }
}

void FilePusher::tick(const std::string& binDir, const std::string& consoleIp,
                      double now) {
    namespace fs = std::filesystem;
    if (consoleIp.empty() || binDir.empty()) {
        files_.clear();
        return;
    }
    if (binDir != dir_ || consoleIp != ip_ || files_.empty()) {
        dir_ = binDir;
        ip_ = consoleIp;
        files_.clear();
        for (const char* name : kMirroredFiles) {
            File f;
            f.name = name;
            f.id = fileId(name);
            files_.push_back(f);
        }
        nextStat_ = nextRepush_ = 0.0;
    }
    const bool repushAll = now >= nextRepush_;
    if (repushAll) nextRepush_ = now + 2.0;
    if (now >= nextStat_ || repushAll) {
        nextStat_ = now + 0.1;
        for (File& f : files_) {
            std::error_code ec;
            const fs::path path = fs::path(dir_) / f.name;
            long long stamp = -1;
            const auto size = fs::file_size(path, ec);
            if (!ec) {
                const auto t = fs::last_write_time(path, ec);
                stamp = (long long)size ^
                        (long long)t.time_since_epoch().count();
            }
            if (stamp == f.stamp && !repushAll) continue;
            f.stamp = stamp;
            if (stamp == -1) {
                f.bytes.clear();
                f.seq = kDeletedSeq;
                f.count = 1;  // one header-only datagram
            } else {
                std::ifstream in(path, std::ios::binary);
                std::string bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
                if (bytes.empty()) continue;  // caught mid-replace; next stat
                f.bytes.swap(bytes);
                f.seq = fileId(f.bytes);  // content hash - same bytes, same seq
                if (f.seq == kDeletedSeq) f.seq = 1;
                f.count = (uint32_t)((f.bytes.size() + kChunk - 1) / kChunk);
            }
            f.next = 0;
            f.sending = true;
        }
    }
    // Paced: up to 24 chunks a frame across all files.
    int budget = 24;
    unsigned char dg[kFilePushHeader + kChunk];
    for (File& f : files_) {
        while (f.sending && budget > 0) {
            const uint32_t total = (uint32_t)f.bytes.size();
            const uint32_t off = f.next * (uint32_t)kChunk;
            const uint32_t n = total == 0 ? 0
                               : total - off < (uint32_t)kChunk ? total - off
                                                                : (uint32_t)kChunk;
            const uint32_t h[6] = {kFilePushMagic, f.id,   f.seq,
                                   total,          f.next, total == 0 ? 1U : f.count};
            std::memcpy(dg, h, sizeof(h));
            if (n) std::memcpy(dg + kFilePushHeader, f.bytes.data() + off, n);
            (void)wire::sendDatagram(ip_, 18198, dg, kFilePushHeader + n);
            --budget;
            if (++f.next >= f.count) f.sending = false;
        }
    }
}

}  // namespace devlink
