#include "gdbstub.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "elfsym.hpp"
#include "platform.hpp"
#include "scriptdbg.hpp"
#include "wire.hpp"

namespace fs = std::filesystem;

namespace gdbstub {

namespace {

using Clock = std::chrono::steady_clock;

int msSince(Clock::time_point t) {
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t)
        .count();
}

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i)));
}

std::string hexBytes(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

std::string hex64le(uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = (uint8_t)(v >> (8 * i));
    return hexBytes(b, 8);
}

bool parseHexBytes(const std::string& s, std::vector<uint8_t>& out) {
    if (s.size() % 2) return false;
    out.clear();
    for (size_t i = 0; i < s.size(); i += 2) {
        unsigned v = 0;
        if (std::sscanf(s.c_str() + i, "%2x", &v) != 1) return false;
        out.push_back((uint8_t)v);
    }
    return true;
}

uint64_t le64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

uint32_t parseHex(const std::string& s) { return (uint32_t)std::strtoull(s.c_str(), nullptr, 16); }

// The address GDB names, as the EE's 32-bit space: an n32 GDB sign-extends
// kernel-segment addresses to 64 bits.
uint32_t guestAddr(const std::string& s) { return (uint32_t)(std::strtoull(s.c_str(), nullptr, 16) & 0xFFFFFFFFull); }

// RAM (and its uncached / accelerated mirrors) and the scratchpad. Anything
// else is hardware registers PINE reads back as zero - telling GDB "error"
// beats letting it believe a zero.
bool readable(uint32_t a, uint32_t n) {
    auto in = [&](uint32_t lo, uint32_t hi) { return a >= lo && a + n <= hi; };
    return in(0x00000000u, 0x02000000u) || in(0x20000000u, 0x22000000u) ||
           in(0x30000000u, 0x32000000u) || in(0x70000000u, 0x70004000u);
}

std::string checksum(const std::string& s) {
    unsigned sum = 0;
    for (unsigned char c : s) sum += c;
    char b[3];
    std::snprintf(b, sizeof(b), "%02x", sum & 0xFF);
    return b;
}

// ps2sdk's EE_RegFrame through dvbm: 32 quadword GPRs + 17 words.
constexpr uint32_t kFrameBytes = 32 * 16 + 17 * 4;
constexpr uint32_t kLine = 256;  // memory cache granule

/** GDB remote serial protocol framing over one stream. */
class Rsp {
public:
    explicit Rsp(wire::Stream& s) : s_(s) {}

    /** Next byte within timeoutMs: >= 0 a byte, -1 none yet, -2 gone. */
    int byte(int timeoutMs) {
        if (pos_ >= buf_.size()) {
            buf_.resize(4096);
            const int got = s_.recv(buf_.data(), buf_.size(), timeoutMs);
            if (got < 0) return -2;
            if (got == 0) {
                buf_.clear();
                pos_ = 0;
                return -1;
            }
            buf_.resize((size_t)got);
            pos_ = 0;
        }
        return buf_[pos_++];
    }

    /** Puts the last byte() back. */
    void unget() {
        if (pos_ > 0) --pos_;
    }

    /** A whole packet ("\x03" for an interrupt). false = GDB left / quit. */
    bool packet(std::string& out, const std::atomic<bool>& quit) {
        int c;
        for (;;) {
            if (quit) return false;
            c = byte(200);
            if (c == -2) return false;
            if (c == 0x03) {
                out = "\x03";
                return true;
            }
            if (c == '$') break;
        }
        out.clear();
        for (;;) {
            c = byte(2000);
            if (c < 0) return false;
            if (c == '#') break;
            out += (char)c;
        }
        if (byte(2000) < 0 || byte(2000) < 0) return false;  // checksum, not checked
        return noAck_ || s_.sendAll("+", 1);
    }

    bool send(const std::string& payload) {
        const std::string out = "$" + payload + "#" + checksum(payload);
        if (noAck_) return s_.sendAll(out.data(), out.size());
        for (int tries = 0; tries < 5; ++tries) {
            if (!s_.sendAll(out.data(), out.size())) return false;
            for (;;) {
                const int c = byte(3000);
                if (c == '+') return true;
                if (c == '-') break;  // resend
                if (c < 0) return false;
            }
        }
        return false;
    }

    /** QStartNoAckMode: the transport is reliable, so neither side waits for
     * a '+' per packet - one round trip less on every exchange. */
    void noAck() { noAck_ = true; }

private:
    wire::Stream& s_;
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    bool noAck_ = false;
};

}  // namespace

// ---------------------------------------------------------------- PINE ----

PineClient::~PineClient() = default;

std::string PineClient::connect(int slot) {
    stream_.reset();
    std::string err;
#ifdef _WIN32
    stream_ = wire::connectStream("127.0.0.1", (uint16_t)slot, 1000, &err);
#else
    const std::string name =
        slot == 28011 ? std::string("pcsx2.sock") : "pcsx2.sock." + std::to_string(slot);
    std::vector<std::string> dirs;
#ifdef __APPLE__
    if (const char* t = std::getenv("TMPDIR")) dirs.push_back(t);
#else
    if (const char* x = std::getenv("XDG_RUNTIME_DIR")) {
        dirs.push_back(x);
        dirs.push_back(std::string(x) + "/app/net.pcsx2.PCSX2");  // the flatpak
    }
#endif
    dirs.push_back("/tmp");
    for (const std::string& d : dirs) {
        stream_ = wire::connectUnixStream(d + "/" + name, &err);
        if (stream_) break;
    }
#endif
    return stream_ ? "" : err;
}

bool PineClient::call(const std::vector<uint8_t>& body, std::vector<uint8_t>& reply) {
    if (!stream_) return false;
    std::vector<uint8_t> msg;
    put32(msg, (uint32_t)body.size() + 4);
    msg.insert(msg.end(), body.begin(), body.end());
    if (!stream_->sendAll(msg.data(), msg.size())) {
        stream_.reset();
        return false;
    }
    auto readN = [&](uint8_t* p, size_t n) {
        while (n > 0) {
            const int got = stream_->recv(p, n, 3000);
            if (got <= 0) return false;
            p += got;
            n -= (size_t)got;
        }
        return true;
    };
    uint8_t head[4];
    if (!readN(head, 4)) {
        stream_.reset();
        return false;
    }
    const uint32_t size = le32(head);
    if (size < 5 || size > (1u << 20)) {
        stream_.reset();
        return false;
    }
    reply.resize(size - 4);
    if (!readN(reply.data(), reply.size())) {
        stream_.reset();
        return false;
    }
    return reply[0] == 0;  // 0 = PINE OK
}

bool PineClient::batch(const std::vector<Op>& ops, std::vector<uint8_t>& reads) {
    reads.clear();
    // PCSX2 caps a request and its answer (MAX_IPC_SIZE / MAX_IPC_RETURN_SIZE);
    // 4096 commands is far below both.
    for (size_t first = 0; first < ops.size(); first += 4096) {
        const size_t last = std::min(ops.size(), first + 4096);
        std::vector<uint8_t> body;
        size_t want = 0;
        for (size_t i = first; i < last; ++i) {
            const Op& o = ops[i];
            body.push_back(o.code);
            put32(body, o.addr);
            switch (o.code) {
                case 0: want += 1; break;
                case 2: want += 4; break;
                case 3: want += 8; break;
                case 4: body.push_back((uint8_t)o.value); break;
                case 6: put32(body, o.value); break;
                default: return false;
            }
        }
        std::vector<uint8_t> r;
        if (!call(body, r) || r.size() < 1 + want) return false;
        reads.insert(reads.end(), r.begin() + 1, r.begin() + 1 + (long)want);
    }
    return true;
}

bool PineClient::readBlock(uint32_t addr, uint32_t n, uint8_t* out) {
    std::vector<Op> ops;
    for (uint32_t i = 0; i < n;) {
        const uint32_t a = addr + i;
        if (a % 8 == 0 && n - i >= 8) {
            ops.push_back({3, a});
            i += 8;
        } else if (a % 4 == 0 && n - i >= 4) {
            ops.push_back({2, a});
            i += 4;
        } else {
            ops.push_back({0, a});
            ++i;
        }
    }
    std::vector<uint8_t> r;
    if (!batch(ops, r) || r.size() != n) return false;
    std::memcpy(out, r.data(), n);
    return true;
}

int PineClient::status() {
    std::vector<uint8_t> r;
    if (!call({15}, r) || r.size() < 5) return -1;  // MsgStatus
    return (int)le32(&r[1]);
}

bool PineClient::read8(uint32_t addr, uint8_t& v) { return readBlock(addr, 1, &v); }

bool PineClient::read32(uint32_t addr, uint32_t& v) {
    uint8_t b[4];
    if (!readBlock(addr & ~3u, 4, b)) return false;
    v = le32(b);
    return true;
}

bool PineClient::read64(uint32_t addr, uint64_t& v) {
    uint8_t b[8];
    if (!readBlock(addr, 8, b)) return false;
    v = le64(b);
    return true;
}

bool PineClient::write8(uint32_t addr, uint8_t v) {
    std::vector<uint8_t> r;
    return batch({{4, addr, v}}, r);
}

bool PineClient::write32(uint32_t addr, uint32_t v) {
    std::vector<uint8_t> r;
    return batch({{6, addr, v}}, r);
}

// ----------------------------------------------------------- stepping ----

std::vector<uint32_t> nextPcs(uint32_t pc, uint32_t insn,
                              const std::function<uint64_t(int)>& reg) {
    const uint32_t op = insn >> 26, rs = (insn >> 21) & 31, rt = (insn >> 16) & 31;
    const int32_t simm = (int16_t)(insn & 0xFFFF);
    const uint32_t taken = pc + 4 + (uint32_t)(simm * 4);
    const uint32_t over = pc + 8;  // past the delay slot
    if (op == 0 && ((insn & 0x3F) == 8 || (insn & 0x3F) == 9))  // jr / jalr
        return {(uint32_t)reg((int)rs)};
    if (op == 2 || op == 3)  // j / jal
        return {((pc + 4) & 0xF0000000u) | ((insn & 0x03FFFFFFu) << 2)};
    if (op == 1 && (rt <= 3 || (rt >= 16 && rt <= 19)))  // REGIMM branches
        return {taken, over};
    if ((op >= 4 && op <= 7) || (op >= 20 && op <= 23))  // beq..bgtz, + likely
        return {taken, over};
    if (op >= 16 && op <= 18 && rs == 8)  // bc0x / bc1x / bc2x
        return {taken, over};
    return {pc + 4};
}

bool hasDelaySlot(uint32_t insn) {
    const uint32_t op = insn >> 26, rs = (insn >> 21) & 31, rt = (insn >> 16) & 31;
    return (op == 0 && ((insn & 0x3F) == 8 || (insn & 0x3F) == 9)) || op == 2 || op == 3 ||
           (op == 1 && (rt <= 3 || (rt >= 16 && rt <= 19))) || (op >= 4 && op <= 7) ||
           (op >= 20 && op <= 23) || (op >= 16 && op <= 18 && rs == 8);
}

// ------------------------------------------------------------- server ----

const char* stateName(State s) {
    switch (s) {
        case State::WaitingForGame: return "waiting for the game";
        case State::WaitingForGdb: return "waiting for GDB";
        case State::Running: return "attached, running";
        case State::Stopped: return "attached, stopped";
        case State::Failed: return "failed";
    }
    return "?";
}

Server::Server(Config cfg) : cfg_(std::move(cfg)) {
    if (const char* t = std::getenv("TYRAX_GDB_TRACE"); t && *t) trace_ = std::fopen(t, "w");
    worker_ = std::thread([this] { run(); });
}

Server::~Server() {
    quit_ = true;
    if (worker_.joinable()) worker_.join();
    if (relay_) relay_->kill();
    if (trace_) std::fclose(trace_);
    std::error_code ec;
    if (!cfg_.sessionFile.empty()) fs::remove(cfg_.sessionFile, ec);
}

void Server::trace(const char* fmt, ...) {
    if (!trace_) return;
    static const Clock::time_point t0 = Clock::now();
    std::fprintf(trace_, "%8.1f ",
                 std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(trace_, fmt, ap);
    va_end(ap);
    std::fputc('\n', trace_);
    std::fflush(trace_);
}

std::string Server::message() const {
    std::lock_guard<std::mutex> lock(msgMutex_);
    return msg_;
}

void Server::setState(State s, const std::string& msg) {
    const bool changed = state_.exchange(s) != s;
    {
        std::lock_guard<std::mutex> lock(msgMutex_);
        msg_ = msg;
    }
    if (changed) writeSessionFile();
}

void Server::writeSessionFile() {
    if (cfg_.sessionFile.empty()) return;
    const State s = state_.load();
    if (s == State::WaitingForGame || s == State::Failed) {
        std::error_code ec;
        fs::remove(cfg_.sessionFile, ec);
        return;
    }
    // Atomic replace: the extension may read it at any moment.
    const std::string tmp = cfg_.sessionFile + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << "{\"state\": \"" << stateName(s) << "\", \"port\": " << cfg_.port
            << ", \"pid\": " << platform::processId() << "}\n";
    }
    std::error_code ec;
    fs::rename(tmp, cfg_.sessionFile, ec);
}

bool Server::mb32(uint32_t off, uint32_t& v) { return pine_.read32(mailbox_ + off, v); }

bool Server::stopped() {
    uint32_t v = 0;
    return mb32(scriptdbg::kOffState, v) && v == 1;
}

bool Server::pause() {
    if (stopped()) return true;
    pine_.write32(mailbox_ + scriptdbg::kOffPause, 1);
    const auto t0 = Clock::now();
    int polls = 0;
    while (!quit_ && pine_.connected()) {
        if (stopped()) return true;
        // The poll runs from the object-script driver: a game sitting in a
        // pause menu does not tick scripts and cannot answer yet - and a
        // PCSX2 that paused ITSELF (Esc, focus loss, a pad dropping off the
        // bus) runs no frames at all.
        std::string why = "pausing the game";
        if (msSince(t0) > 2000) {
            why = "waiting for the game to run a frame (paused in a menu?)";
            if (++polls % 100 == 0 && pine_.status() == 1)
                why = "PCSX2 itself is paused - resume it to attach";
        }
        setState(state_.load(), why);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void Server::invalidate() {
    snapValid_ = false;
    lines_.clear();
}

bool Server::snapshot() {
    using namespace scriptdbg;
    if (snapValid_) return true;
    mbox_.assign(kOffPatches, 0);
    if (!pine_.readBlock(mailbox_, kOffPatches, mbox_.data())) return false;
    frameAddr_ = le32(&mbox_[kOffFrame]);
    frame_.assign(kFrameBytes, 0);
    if (frameAddr_ && !pine_.readBlock(frameAddr_, kFrameBytes, frame_.data())) return false;
    snapValid_ = true;
    return true;
}

bool Server::resume() {
    using namespace scriptdbg;
    snapshot();
    stopsSeen_ = mbox_.empty() ? 0 : le32(&mbox_[kOffStops]);
    // One request: the patches, their count, then the go flag - PINE runs a
    // batch in order, so the game sees a complete queue when it wakes.
    const size_t n = std::min<size_t>(pending_.size(), kMaxPatches);
    std::vector<PineClient::Op> ops;
    for (size_t i = 0; i < n; ++i) {
        ops.push_back({6, mailbox_ + kOffPatches + (uint32_t)i * 8, pending_[i].first});
        ops.push_back({6, mailbox_ + kOffPatches + (uint32_t)i * 8 + 4, pending_[i].second});
    }
    ops.push_back({6, mailbox_ + kOffPatchCount, (uint32_t)n});
    ops.push_back({6, mailbox_ + kOffResume, 1});
    pending_.erase(pending_.begin(), pending_.begin() + (long)n);
    invalidate();
    std::vector<uint8_t> unused;
    return pine_.batch(ops, unused);
}

bool Server::regAt(int n, uint8_t*& at, uint32_t& addr, int& size) {
    using namespace scriptdbg;
    if (!snapshot()) return false;
    size = 4;
    uint32_t off;
    bool inFrame = true;
    if (n >= 0 && n < 32) {
        off = kFrameGpr + (uint32_t)n * 16;
        size = 8;
    } else {
        switch (n) {  // GDB's MIPS numbering: sr, lo, hi, bad, cause, pc
            case 32: off = kFrameStatus; break;
            case 33: off = kFrameLo; size = 8; break;
            case 34: off = kFrameHi; size = 8; break;
            case 35: off = kFrameBadVAddr; break;
            case 36: off = kFrameCause; break;
            case 37: off = kFrameEpc; break;
            case 70: off = kOffFcr31; inFrame = false; break;
            default:
                if (n < 38 || n >= 70) return false;
                off = kOffFpr + (uint32_t)(n - 38) * 4;  // the FPRs the handler saved
                inFrame = false;
        }
    }
    if (inFrame) {
        at = &frame_[off];
        addr = frameAddr_ + off;
    } else {
        at = &mbox_[off];
        addr = mailbox_ + off;
    }
    return true;
}

uint64_t Server::getReg(int n) {
    uint8_t* at = nullptr;
    uint32_t addr = 0;
    int size = 0;
    if (n == 0 || !regAt(n, at, addr, size)) return 0;
    if (size == 8) return le64(at);
    const uint32_t v = le32(at);
    // The 32-bit CP0 words and the pc read sign-extended, as the CPU does;
    // FPRs and FCR31 are raw bit patterns.
    if (n < 38 && (v & 0x80000000u)) return 0xFFFFFFFF00000000ull | v;
    return v;
}

void Server::setReg(int n, uint64_t v) {
    uint8_t* at = nullptr;
    uint32_t addr = 0;
    int size = 0;
    if (n == 0 || !regAt(n, at, addr, size)) return;
    std::vector<PineClient::Op> ops{{6, addr, (uint32_t)v}};
    if (size == 8) ops.push_back({6, addr + 4, (uint32_t)(v >> 32)});
    std::vector<uint8_t> unused;
    pine_.batch(ops, unused);
    for (int i = 0; i < size; ++i) at[i] = (uint8_t)(v >> (8 * i));
}

bool Server::readMem(uint32_t a, uint32_t n, std::vector<uint8_t>& out) {
    out.clear();
    if (n == 0) return true;
    // The lines this range touches, the missing ones fetched in one batch.
    const uint32_t first = a / kLine * kLine, last = (a + n - 1) / kLine * kLine;
    std::vector<uint32_t> missing;
    for (uint32_t l = first; l <= last; l += kLine)
        if (!lines_.count(l)) missing.push_back(l);
    if (!missing.empty()) {
        std::vector<PineClient::Op> ops;
        for (uint32_t l : missing)
            for (uint32_t o = 0; o < kLine; o += 8) ops.push_back({3, l + o});
        std::vector<uint8_t> r;
        if (!pine_.batch(ops, r) || r.size() != missing.size() * kLine) return false;
        for (size_t i = 0; i < missing.size(); ++i)
            lines_[missing[i]].assign(r.begin() + (long)(i * kLine),
                                      r.begin() + (long)((i + 1) * kLine));
    }
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t x = a + i;
        out.push_back(lines_[x / kLine * kLine][x % kLine]);
    }
    // GDB sees the real code, never one of our traps.
    for (auto it = origCode_.lower_bound(a >= 3 ? a - 3 : 0);
         it != origCode_.end() && it->first < a + n; ++it)
        for (uint32_t k = 0; k < 4; ++k)
            if (it->first + k >= a && it->first + k < a + n)
                out[it->first + k - a] = (uint8_t)(it->second >> (8 * k));
    return true;
}

bool Server::writeMem(uint32_t a, const std::vector<uint8_t>& b) {
    std::vector<PineClient::Op> ops;
    for (size_t i = 0; i < b.size();) {
        if ((a + i) % 4 == 0 && b.size() - i >= 4) {
            ops.push_back({6, a + (uint32_t)i, le32(&b[i])});
            i += 4;
        } else {
            ops.push_back({4, a + (uint32_t)i, b[i]});
            ++i;
        }
    }
    std::vector<uint8_t> unused;
    if (!pine_.batch(ops, unused)) return false;
    for (size_t i = 0; i < b.size(); ++i) {
        const uint32_t x = a + (uint32_t)i;
        auto it = lines_.find(x / kLine * kLine);
        if (it != lines_.end()) it->second[x % kLine] = b[i];
    }
    return true;
}

uint32_t Server::codeAt(uint32_t addr) {
    auto it = origCode_.find(addr);
    if (it != origCode_.end()) return it->second;
    std::vector<uint8_t> b;
    const uint32_t v = readMem(addr, 4, b) ? le32(b.data()) : 0;
    origCode_[addr] = v;
    return v;
}

void Server::addTrap(uint32_t addr) {
    if (bps_.count(addr) || steps_.count(addr) || !readable(addr, 4)) return;
    codeAt(addr);
    steps_[addr] = true;
    pending_.push_back({addr, scriptdbg::kBreak});
}

void Server::dropTraps() {
    // The handler has parked; these are only bytes in the queue until the next
    // resume writes them.
    for (const auto& [a, unused] : steps_) {
        (void)unused;
        if (!bps_.count(a)) pending_.push_back({a, origCode_[a]});
    }
    steps_.clear();
}

std::string Server::stopReply() {
    const uint32_t pc = (uint32_t)getReg(37);
    std::vector<uint8_t> b;
    const bool pollTrap = !origCode_.count(pc) && readMem(pc, 4, b) &&
                          le32(b.data()) == scriptdbg::kPollBreak;
    return pollTrap ? "S02" : "T05swbreak:;";
}

std::string Server::handle(const std::string& pkt, Action& a) {
    using namespace scriptdbg;
    constexpr int kRegs = 72;
    a = Action{};
    if (pkt.empty()) return "";
    if (pkt.rfind("qSupported", 0) == 0) return "PacketSize=4000;swbreak+;QStartNoAckMode+";
    if (pkt == "?") return stopReply();
    if (pkt == "qAttached") return "1";
    if (pkt == "qC") return "QC1";
    if (pkt == "qfThreadInfo") return "m1";
    if (pkt == "qsThreadInfo") return "l";
    if (pkt[0] == 'H' || pkt.rfind("qSymbol", 0) == 0 || pkt[0] == 'T') return "OK";
    // Range stepping: GDB hands over a whole source line, so a `next` costs
    // a stop or two instead of one round trip per instruction.
    if (pkt == "vCont?") return "vCont;c;C;s;S;r";
    if (pkt.rfind("vCont;", 0) == 0) {
        std::string first = pkt.substr(6);
        first = first.substr(0, first.find(';'));
        first = first.substr(0, first.find(':'));
        if (first.empty()) return "E01";
        if (first[0] == 'c' || first[0] == 'C') a.act = Act::Continue;
        else if (first[0] == 's' || first[0] == 'S') a.act = Act::Step;
        else if (first[0] == 'r') {
            const size_t comma = first.find(',');
            if (comma == std::string::npos) return "E01";
            a.act = Act::Range;
            a.start = guestAddr(first.substr(1, comma - 1));
            a.end = guestAddr(first.substr(comma + 1));
        } else {
            return "E01";
        }
        return "";
    }
    if (pkt == "g") {
        std::string out;
        for (int n = 0; n < kRegs; ++n) out += hex64le(getReg(n));
        return out;
    }
    if (pkt[0] == 'G') {
        std::vector<uint8_t> b;
        if (!parseHexBytes(pkt.substr(1), b)) return "E01";
        for (int n = 0; n < kRegs && (size_t)(n + 1) * 8 <= b.size(); ++n) {
            const uint64_t v = le64(b.data() + n * 8);
            if (v != getReg(n)) setReg(n, v);
        }
        return "OK";
    }
    if (pkt[0] == 'p') return hex64le(getReg((int)parseHex(pkt.substr(1))));
    if (pkt[0] == 'P') {
        const size_t eq = pkt.find('=');
        std::vector<uint8_t> b;
        if (eq == std::string::npos || !parseHexBytes(pkt.substr(eq + 1), b)) return "E01";
        b.resize(8, 0);
        setReg((int)parseHex(pkt.substr(1, eq - 1)), le64(b.data()));
        return "OK";
    }
    if (pkt[0] == 'm') {
        const size_t comma = pkt.find(',');
        if (comma == std::string::npos) return "E01";
        const uint32_t at = guestAddr(pkt.substr(1, comma - 1));
        const uint32_t n = std::min<uint32_t>(parseHex(pkt.substr(comma + 1)), 2048);
        std::vector<uint8_t> data;
        if (!readable(at, n) || !readMem(at, n, data)) return "E01";
        return hexBytes(data.data(), data.size());
    }
    if (pkt[0] == 'M') {
        const size_t comma = pkt.find(','), colon = pkt.find(':');
        std::vector<uint8_t> b;
        if (comma == std::string::npos || colon == std::string::npos ||
            !parseHexBytes(pkt.substr(colon + 1), b))
            return "E01";
        const uint32_t at = guestAddr(pkt.substr(1, comma - 1));
        if (!readable(at, (uint32_t)b.size()) || !writeMem(at, b)) return "E01";
        return "OK";
    }
    if (pkt.rfind("Z0,", 0) == 0 || pkt.rfind("z0,", 0) == 0) {
        const bool insert = pkt[0] == 'Z';
        const uint32_t at = guestAddr(pkt.substr(3, pkt.find(',', 3) - 3));
        if (at % 4 || !readable(at, 4)) return "E01";
        const uint32_t orig = codeAt(at);
        if (insert && !bps_.count(at)) {
            bps_[at] = true;
            if (!steps_.count(at)) pending_.push_back({at, kBreak});
        } else if (!insert && bps_.count(at)) {
            bps_.erase(at);
            if (!steps_.count(at)) pending_.push_back({at, orig});
        }
        return "OK";
    }
    if (pkt[0] == 'c' || pkt[0] == 'C') {
        a.act = Act::Continue;
        return "";
    }
    if (pkt[0] == 's' || pkt[0] == 'S') {
        a.act = Act::Step;
        return "";
    }
    if (pkt == "k" || pkt[0] == 'D') {
        a.act = Act::Quit;
        return pkt[0] == 'D' ? "OK" : "";
    }
    return "";  // unsupported: GDB falls back (X -> M, Z1 -> Z0, ...)
}

bool Server::attachGame() {
    if (!pine_.connected()) {
        verified_ = false;
        const std::string err = pine_.connect(cfg_.pineSlot);
        if (!err.empty()) {
            setState(State::WaitingForGame,
                     "no PINE on slot " + std::to_string(cfg_.pineSlot) + " yet (" + err + ")");
            return false;
        }
    }
    uint32_t magic = 0, installed = 0;
    if (!mb32(scriptdbg::kOffMagic, magic)) {
        // A memory read fails while PCSX2 is still booting (no VM yet) - and,
        // with no answer at all, when another client holds the socket: PCSX2
        // serves one PINE client at a time.
        const int vm = pine_.connected() ? pine_.status() : -1;
        setState(State::WaitingForGame,
                 vm == 1   ? std::string("PCSX2 itself is paused - resume it")
                 : vm >= 0 ? std::string("PCSX2 is starting the game")
                           : "PINE slot " + std::to_string(cfg_.pineSlot) +
                                 " does not answer (another tool connected to it?)");
        return false;
    }
    if (magic != scriptdbg::kMagic || !mb32(scriptdbg::kOffInstalled, installed) ||
        !installed) {
        verified_ = false;
        setState(State::WaitingForGame, "waiting for the game's first frame");
        return false;
    }
    if (!verified_ && !textSamples_.empty()) {
        std::vector<PineClient::Op> ops;
        for (const auto& [addr, word] : textSamples_) ops.push_back({2, addr});
        std::vector<uint8_t> r;
        if (!pine_.batch(ops, r) || r.size() != textSamples_.size() * 4) return false;
        int differ = 0;
        for (size_t i = 0; i < textSamples_.size(); ++i)
            differ += le32(&r[i * 4]) != textSamples_[i].second &&
                      le32(&r[i * 4]) != scriptdbg::kBreak;  // a trap left behind
        if (differ) {
            setState(State::WaitingForGame,
                     "the game on PINE slot " + std::to_string(cfg_.pineSlot) +
                         " is not this build (another PCSX2's game, or an old build still "
                         "running)");
            return false;
        }
        verified_ = true;
    }
    return true;
}

void Server::serve(wire::Stream& gdb) {
    using namespace scriptdbg;
    Rsp rsp(gdb);
    // Through the WSL relay the connection exists before GDB does (the relay
    // takes ours first, then waits for GDB's): pause the game only once GDB
    // has said something, or it would freeze while nobody is looking.
    for (;;) {
        const int c = rsp.byte(250);
        if (c >= 0) {
            rsp.unget();
            break;
        }
        uint32_t magic = 0;
        if (c == -2 || quit_ || !mb32(kOffMagic, magic)) return;
    }
    setState(State::Stopped, "pausing the game for GDB");
    invalidate();
    if (!pause()) return;
    pc_ = (uint32_t)getReg(37);
    setState(State::Stopped, "stopped");

    // Release the game and wait for its next stop. 0 = stopped, 1 = paused by
    // GDB (Ctrl-C), 2 = gone (GDB left, PCSX2 closed, quitting).
    auto run = [&]() -> int {
        resume();
        setState(State::Running, "running");
        // A step's trap is usually hit at once, so poll hard first: a PINE read
        // is ~0.1 ms, while any socket wait rounds up to the OS timer tick
        // (~15 ms on Windows) - which was most of a step's cost.
        const auto started = Clock::now();
        const auto spinUntil = started + std::chrono::milliseconds(30);
        int polls = 0;
        for (;;) {
            if (quit_) return 2;
            ++polls;
            // State and stop counter in one read: a step trap re-stops before
            // a "running" state could ever be seen, so the counter is the edge.
            uint8_t b[16];
            if (!pine_.readBlock(mailbox_ + kOffState, 16, b)) {
                rsp.send("X09");  // the emulator went away under the session
                return 2;
            }
            if (le32(b + (kOffStops - kOffState)) != stopsSeen_ && le32(b) == 1) {
                trace("   ran %.2f ms, %d polls",
                      std::chrono::duration<double, std::milli>(Clock::now() - started).count(),
                      polls);
                return 0;
            }
            const int c = rsp.byte(Clock::now() < spinUntil ? 0 : 1);
            if (c == -2) return 2;
            if (c == 0x03) {
                pause();
                return 1;
            }
        }
    };

    // Step while start <= pc < end. Instead of one trap per instruction, trap
    // every way OUT of the range: branch and jump targets outside it, the
    // fall-through past its end, and - since their target is a register - each
    // jr/jalr, which is then single-stepped with the target known. A line of
    // straight code and its loops cost one stop; a call costs the stop at the
    // callee (GDB then steps over it with a breakpoint at the return).
    auto rangeStep = [&](uint32_t start, uint32_t end) -> int {
        if (end <= start || end - start > 64 * 1024) end = start + 4;
        for (int guard = 0; guard < 10000; ++guard) {
            invalidate();
            const uint32_t pc = (uint32_t)getReg(37);
            if (pc < start || pc >= end) return 0;
            if (guard > 0 && bps_.count(pc)) return 0;  // a breakpoint on the line
            std::vector<uint8_t> code;
            if (!readMem(start, end - start, code)) return 2;
            std::vector<uint32_t> exits;
            bool atJump = false;
            for (uint32_t at = start; at + 4 <= end; at += 4) {
                const uint32_t insn = le32(&code[at - start]);
                const uint32_t op = insn >> 26;
                const bool regJump = op == 0 && ((insn & 0x3F) == 8 || (insn & 0x3F) == 9);
                if (regJump) {
                    if (at == pc) atJump = true;
                    else exits.push_back(at);  // stop before it, then step it
                } else if (hasDelaySlot(insn)) {
                    for (uint32_t t : nextPcs(at, insn, [](int) { return 0; }))
                        if (t < start || t >= end) exits.push_back(t);
                }
            }
            // The fall-through past the end - unless the line's last
            // instruction is a branch, whose delay slot `end` then is: a trap
            // there would report the branch and resuming would run the branch
            // again. The instruction after the slot is that branch's
            // fall-through; its taken side is an exit already.
            exits.push_back(hasDelaySlot(le32(&code[end - start - 4])) ? end + 4 : end);
            if (atJump)
                exits = nextPcs(pc, le32(&code[pc - start]),
                                [this](int r) { return getReg(r); });
            for (uint32_t t : exits)
                if (t != pc) addTrap(t);
            const int r = run();
            dropTraps();
            if (r != 0) return r;
        }
        return 0;
    };

    std::string pkt;
    while (!quit_ && rsp.packet(pkt, quit_)) {
        if (!pine_.connected()) break;
        if (pkt == "\x03") continue;  // already stopped
        if (pkt == "QStartNoAckMode") {
            rsp.send("OK");
            rsp.noAck();
            continue;
        }
        const auto t0 = Clock::now();
        Action act;
        const std::string reply = handle(pkt, act);
        if (act.act == Act::Quit) {
            if (!reply.empty()) rsp.send(reply);
            break;
        }
        if (act.act == Act::Reply) {
            trace("<- %.60s -> %.40s [%d ms]", pkt.c_str(), reply.c_str(), msSince(t0));
            if (!rsp.send(reply)) break;
            continue;
        }
        int r = 0;
        if (act.act == Act::Range) {
            r = rangeStep(act.start, act.end);
        } else {
            if (act.act == Act::Step) {
                invalidate();
                const uint32_t at = (uint32_t)getReg(37);
                for (uint32_t t : nextPcs(at, codeAt(at), [this](int n) { return getReg(n); }))
                    addTrap(t);
            }
            r = run();
            dropTraps();
        }
        if (r == 2) break;
        invalidate();
        pc_ = (uint32_t)getReg(37);
        setState(State::Stopped, "stopped");
        const std::string stop = stopReply();
        trace("<- %.60s -> stop %08x %s [%d ms]", pkt.c_str(), (unsigned)pc_.load(), stop.c_str(),
              msSince(t0));
        if (!rsp.send(stop)) break;
    }
    // GDB is gone: take every trap out and let the game run on its own.
    if (pine_.connected()) {
        for (const auto& [at, unused] : bps_) {
            (void)unused;
            pending_.push_back({at, origCode_[at]});
        }
        bps_.clear();
        dropTraps();
        if (stopped())
            resume();
        else if (!pending_.empty() && pause())
            resume();
    }
    pending_.clear();
    invalidate();
}

void Server::run() {
    // The mailbox is a symbol of the unstripped ELF the debug build keeps.
    const elfsym::Image img = elfsym::read(cfg_.elfPath);
    if (const elfsym::Symbol* s = img.symbol(scriptdbg::kMailboxSymbol)) mailbox_ = s->addr;
    if (!mailbox_) {
        setState(State::Failed, img.loaded ? "no script debugger in " + cfg_.elfPath +
                                                 " - rebuild with it on"
                                           : "cannot read " + cfg_.elfPath);
        return;
    }
    // 1024 words spread over .text, as the file has them: what attachGame
    // compares with the running game before serving it.
    if (const elfsym::Section* text = img.section(".text"); text && text->size >= 4096) {
        const std::vector<unsigned char> bytes = elfsym::sectionBytes(cfg_.elfPath, *text);
        if (bytes.size() >= text->size) {
            const uint32_t step = (text->size / 1024) & ~3u;
            for (uint32_t off = 0; off + 4 <= text->size && textSamples_.size() < 1024; off += step)
                textSamples_.push_back({text->addr + off, le32(&bytes[off])});
        }
    }

    std::unique_ptr<wire::StreamListener> listener;
    if (cfg_.relayCommand.empty()) {
        std::string err;
        listener = wire::listenStream("127.0.0.1", (uint16_t)cfg_.port, &err);
        if (!listener) {
            setState(State::Failed, err);
            return;
        }
    } else {
        relay_ = platform::Process::start(cfg_.relayCommand);
        if (!relay_) {
            setState(State::Failed, "could not start the WSL relay: " + cfg_.relayCommand);
            return;
        }
    }

    while (!quit_) {
        if (!attachGame()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        setState(State::WaitingForGdb, "GDB on 127.0.0.1:" + std::to_string(cfg_.port));
        std::unique_ptr<wire::Stream> gdb;
        while (!quit_ && !gdb) {
            if (listener) {
                gdb = listener->accept(250);
            } else {
                // The relay answers this connect at once and pairs it with the
                // next GDB; until then the reads below just time out.
                std::string err;
                gdb = wire::connectStream("127.0.0.1", (uint16_t)(cfg_.port + 1), 500, &err);
                if (!gdb) {
                    if (relay_ && !relay_->running()) {
                        setState(State::Failed, "the WSL relay exited (is WSL's g++ there?)");
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
            }
            // A game that went away while nobody was attached.
            uint32_t magic = 0;
            if (!gdb && !mb32(scriptdbg::kOffMagic, magic)) break;
        }
        if (!gdb) continue;
        serve(*gdb);
        if (!quit_) setState(pine_.connected() ? State::WaitingForGdb : State::WaitingForGame,
                             "GDB detached");
    }
}

}  // namespace gdbstub
