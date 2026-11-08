// Host harness for devlink::Reassembler (src/devlink_host.cpp, docs/devlink.md).
//
//   g++ -std=c++20 -Isrc tools/verify-devlink-reassembler.cpp src/devlink_host.cpp \
//       -o verify-devlink-reassembler
//
// The Reassembler is pure; the Receiver's two outside calls are stubbed below
// so the harness links without sockets, GLFW or the rest of the editor.
//
// Feeds TXDO chunks the way the IOP sends them - in order, shuffled, with a
// chunk lost, duplicated, and across a game reboot - and checks what comes
// out. Exit 0 = every case passed.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "devlink_host.hpp"

namespace wire {
int recvDatagram(void*, size_t, int, std::string*) { return -1; }
std::string sendDatagram(const std::string&, uint16_t, const void*, size_t) { return ""; }
}
namespace platform {
void sleepMs(int) {}
}

using devlink::Reassembler;

static std::vector<std::vector<unsigned char>> chunksOf(uint32_t seq, const std::string& rep) {
    std::vector<std::vector<unsigned char>> out;
    const uint32_t total = (uint32_t)rep.size();
    const uint32_t count = (total + devlink::kChunk - 1) / devlink::kChunk;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t off = i * devlink::kChunk;
        const uint32_t n = std::min<uint32_t>(devlink::kChunk, total - off);
        std::vector<unsigned char> d(16 + n);
        const uint32_t h[4] = {devlink::kOutMagic, seq, (count << 16) | i, total};
        std::memcpy(d.data(), h, 16);
        std::memcpy(d.data() + 16, rep.data() + off, n);
        out.push_back(std::move(d));
    }
    return out;
}

static std::string payload(int size, int salt) {
    std::string s((size_t)size, '\0');
    for (int i = 0; i < size; ++i) s[(size_t)i] = (char)((i * 31 + salt) & 0xFF);
    return s;
}

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    {
        Reassembler r;
        const std::string rep = payload(5000, 1);
        int completed = 0;
        for (auto& c : chunksOf(1, rep)) completed += r.feed(c.data(), (int)c.size());
        check(completed == 1 && r.report() == rep, "in order: one report, bytes intact");
    }
    {
        Reassembler r;
        const std::string rep = payload(9000, 2);
        auto cs = chunksOf(7, rep);
        std::shuffle(cs.begin(), cs.end(), std::mt19937(42));
        int completed = 0;
        for (auto& c : cs) completed += r.feed(c.data(), (int)c.size());
        check(completed == 1 && r.report() == rep, "shuffled: reassembled");
    }
    {
        Reassembler r;
        auto a = chunksOf(3, payload(4000, 3));
        a.erase(a.begin() + 1);  // lost
        int completed = 0;
        for (auto& c : a) completed += r.feed(c.data(), (int)c.size());
        const std::string b = payload(3000, 4);
        for (auto& c : chunksOf(4, b)) completed += r.feed(c.data(), (int)c.size());
        check(completed == 1 && r.report() == b && r.lastSeq() == 4,
              "lost chunk: that report dropped, the next one arrives");
    }
    {
        Reassembler r;
        const std::string a = payload(1500, 5);
        int completed = 0;
        for (auto& c : chunksOf(9, a)) completed += r.feed(c.data(), (int)c.size());
        for (auto& c : chunksOf(9, a)) completed += r.feed(c.data(), (int)c.size());
        check(completed == 1, "duplicate of the report just completed: ignored");
    }
    {
        Reassembler r;
        int completed = 0;
        for (auto& c : chunksOf(500, payload(2000, 6))) completed += r.feed(c.data(), (int)c.size());
        const std::string b = payload(2500, 7);
        for (auto& c : chunksOf(1, b)) completed += r.feed(c.data(), (int)c.size());
        check(completed == 2 && r.report() == b, "game rebooted (seq back to 1): accepted");
    }
    {
        Reassembler r;
        std::vector<unsigned char> junk(40, 0xAB);
        auto c = chunksOf(2, payload(100, 8))[0];
        c[12] = 0xFF;  // total no longer matches the chunk count
        check(!r.feed(junk.data(), (int)junk.size()) && !r.feed(c.data(), (int)c.size()),
              "foreign / inconsistent datagrams: rejected");
    }
    // Phase 3: a file the game writes, sent as segments (TXDF reports).
    auto segment = [](uint32_t id, uint32_t seq, uint32_t off, const std::string& file,
                      size_t n) {
        std::string s(16 + n, '\0');
        const uint32_t h[4] = {id, seq, off, (uint32_t)file.size()};
        std::memcpy(&s[0], h, 16);
        std::memcpy(&s[16], file.data() + off, n);
        return s;
    };
    {
        devlink::FileAssembler fa;
        const std::string file = payload(70000, 9);  // three segments
        const size_t seg = devlink::kReportMax - 16;
        int done = 0;
        for (size_t off = 0; off < file.size(); off += seg)
            done += fa.feed(segment(42, 1, (uint32_t)off, file,
                                    std::min(seg, file.size() - off)));
        check(done == 1 && fa.bytes() == file && fa.id() == 42,
              "file segments: whole file, right id");
    }
    {
        devlink::FileAssembler fa;
        const std::string a = payload(40000, 10), b = payload(40000, 11);
        const size_t seg = devlink::kReportMax - 16;
        int done = fa.feed(segment(7, 1, 0, a, seg));  // a's first half only
        done += fa.feed(segment(7, 2, 0, b, seg));     // a newer version starts
        done += fa.feed(segment(7, 2, (uint32_t)seg, b, b.size() - seg));
        check(done == 1 && fa.bytes() == b, "file segments: newer version wins");
    }
    check(devlink::fileId("livelink.bin") != devlink::fileId("livelogic.bin") &&
              devlink::fileId("livetime.bin") != 0,
          "file ids: distinct, never 0");
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
