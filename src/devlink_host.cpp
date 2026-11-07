#include "devlink_host.hpp"

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

bool Reassembler::feed(const unsigned char* data, int len) {
    if (len < 16 || get32(data) != kOutMagic) return false;
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
    Reassembler r;
    std::string lastIp;
    unsigned char buf[16 + kChunk + 64];
    while (!stop_) {
        std::string from;
        const int n = wire::recvDatagram(buf, sizeof(buf), 100, &from);
        if (n <= 0) continue;
        std::string dir, ip;
        {
            std::lock_guard<std::mutex> lock(mu_);
            dir = binDir_;
            ip = ip_;
        }
        if (from != ip) continue;  // only the console we deploy to
        if (ip != lastIp) {  // a different console: its numbering starts over
            r = Reassembler();
            lastIp = ip;
        }
        if (!r.feed(buf, n)) continue;
        if (writeAtomic(std::filesystem::path(dir) / "livedbg.bin", r.report()))
            ++reports_;
    }
}

}  // namespace devlink
