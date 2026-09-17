// Host property test for meshstrip::Weld::kNoNormal, on the REAL baked wheels.
//
// The recipe is the one in the tyra-testing skill, "Triangle strips: a host
// property test, then one knob in PCSX2": src/meshstrip.cpp has no GL, no
// ImGui and no Project, so this links in seconds and checks the thing a
// screenshot cannot - that the strip draws the SAME TRIANGLES.
//
//   g++ -std=gnu++20 -O1 -I src -o stripcheck stripcheck-wheels.cpp src/meshstrip.cpp
//   ./stripcheck <dir with veh-*-wheel.tmdl>
//
// What "the same triangles" MEANS is the part this file exists to pin down.
// kNoNormal welds on position and UV, so a strip vertex may carry a different
// normal from the corner a given triangle originally had - by construction,
// and legally, because the bag that draws it is unlit. So the multiset is
// taken over (position, UV) and the normal is deliberately excluded. Taking it
// over all eight floats would fail on a correct stripper, which is exactly the
// kind of check that gets "fixed" the wrong way.
//
// Winding is canonicalised away (nothing in this engine backface-culls) and
// degenerate triangles are dropped, because a strip deliberately makes them at
// every seam and at the tail padding.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "meshstrip.hpp"

namespace {

struct Part {
    std::string name;
    std::vector<float> verts;
};

// The .tmdl layout is documented in src/tmdl.hpp. Only what this check needs.
bool readParts(const std::filesystem::path& p, std::vector<Part>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    size_t o = 0;
    auto u32 = [&]() { uint32_t v; std::memcpy(&v, &b[o], 4); o += 4; return v; };
    if (b.size() < 8 || std::memcmp(&b[0], "TMDL", 4) != 0) return false;
    o = 4;
    const uint32_t ver = u32();
    o += 24;  // min[3], max[3]
    const uint32_t pc = u32();
    for (uint32_t i = 0; i < pc; ++i) {
        Part part;
        part.name.assign(&b[o], strnlen(&b[o], 32));
        o += 32 + 64 + 64 + 12;            // name, texture, refl, kd
        if (ver >= 2) o += 12;             // ke
        o += 4 + 4;                        // reflStrength, flags
        const uint32_t vc = u32();
        part.verts.resize((size_t)vc * 8);
        std::memcpy(part.verts.data(), &b[o], (size_t)vc * 8 * 4);
        o += (size_t)vc * 8 * 4;
        o += 4 + u32() * 0;                // aoCount handled below
        o -= 4;
        const uint32_t aoc = u32();
        o += aoc;
        const uint32_t lc = u32();
        for (uint32_t l = 0; l < lc; ++l) {
            const uint32_t lvc = u32();
            o += (size_t)lvc * 8 * 4;
            const uint32_t lao = u32();
            o += lao;
        }
        if (ver >= 4) {
            u32();                          // stripRun
            for (uint32_t m = 0; m < 1 + lc; ++m) {
                const uint32_t mvc = u32();
                o += (size_t)mvc * 8 * 4;
                const uint32_t mao = u32();
                o += mao;
            }
        }
        out.push_back(std::move(part));
    }
    return true;
}

// A corner as the GS sees it for an UNLIT, single-colour bag: position and UV.
using Corner = std::array<float, 5>;
Corner corner(const float* v) { return {v[0], v[1], v[2], v[6], v[7]}; }

using Tri = std::array<Corner, 3>;
Tri canonical(Corner a, Corner b, Corner c) {
    Tri t{a, b, c};
    std::sort(t.begin(), t.end());
    return t;
}
bool degenerate(const Tri& t) {
    return t[0] == t[1] || t[1] == t[2];
}

int fails = 0;
void check(bool ok, const char* what, const std::string& where) {
    if (ok) return;
    std::printf("  FAIL  %s  (%s)\n", what, where.c_str());
    ++fails;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: stripcheck <dir with veh-*-wheel.tmdl>\n");
        return 2;
    }
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(argv[1])) {
        const std::string n = e.path().filename().string();
        if (n.size() < 11 || n.compare(n.size() - 11, 11, "-wheel.tmdl") != 0)
            continue;
        std::vector<Part> parts;
        if (!readParts(e.path(), parts)) {
            std::printf("  FAIL  unreadable: %s\n", n.c_str());
            ++fails;
            continue;
        }
        ++files;
        for (const Part& part : parts) {
            const std::string where = n + " / " + part.name;
            std::vector<float> outV;
            std::vector<unsigned char> outAo;
            const bool built =
                meshstrip::build(part.verts, {}, meshstrip::kRun, outV, outAo,
                                 meshstrip::Weld::kNoNormal);
            const size_t listVerts = part.verts.size() / 8;
            if (!built) {
                std::printf("  refused (kept the list): %s, %zu verts\n",
                            where.c_str(), listVerts);
                continue;
            }
            const size_t sv = outV.size() / 8;

            // --- structural invariants -----------------------------------
            check(sv < listVerts, "strip is not smaller than the list", where);
            check(sv % 3 == 0, "total is not a multiple of 3", where);
            const size_t whole = sv / meshstrip::kRun;
            const size_t tailLen = sv % meshstrip::kRun;
            check(tailLen % 3 == 0, "tail run is not a multiple of 3", where);
            (void)whole;

            // --- the property: the SAME triangles ------------------------
            std::map<Tri, int> want, got;
            for (size_t i = 0; i + 2 < listVerts; i += 3) {
                const Tri t = canonical(corner(&part.verts[i * 8]),
                                        corner(&part.verts[(i + 1) * 8]),
                                        corner(&part.verts[(i + 2) * 8]));
                if (!degenerate(t)) ++want[t];
            }
            // Triangle i of a run is v[i], v[i+1], v[i+2] - NEVER across a run
            // boundary, which is the invariant the packages rely on.
            for (size_t r = 0; r < sv; r += meshstrip::kRun) {
                const size_t len = std::min<size_t>(meshstrip::kRun, sv - r);
                for (size_t i = 0; i + 3 <= len; ++i) {
                    const Tri t = canonical(corner(&outV[(r + i) * 8]),
                                            corner(&outV[(r + i + 1) * 8]),
                                            corner(&outV[(r + i + 2) * 8]));
                    if (!degenerate(t)) ++got[t];
                }
            }
            std::vector<Tri> missing, extra;
            for (const auto& [t, c] : want)
                if (!got.count(t)) missing.push_back(t);
            for (const auto& [t, c] : got)
                if (!want.count(t)) extra.push_back(t);
            check(missing.empty(), "the strip LOST a surface triangle", where);
            check(extra.empty(), "the strip INVENTED a triangle", where);

            // --- what the wheel batch then does with it ------------------
            // Each of the four wheels is a fixed block, rounded up to a whole
            // number of runs so a VU1 package cannot span two wheels.
            const size_t block = (sv + meshstrip::kRun - 1) / meshstrip::kRun *
                                 meshstrip::kRun;
            check(block % meshstrip::kRun == 0, "block is not whole runs", where);
            std::printf("  ok    %-46s list %5zu -> strip %5zu (%.3fx), "
                        "%zu distinct tris, block %zu = %zu pkgs\n",
                        where.c_str(), listVerts, sv,
                        (double)sv / (double)listVerts, want.size(), block,
                        block / meshstrip::kRun);
        }
    }
    if (files == 0) {
        std::printf("no *-wheel.tmdl found in %s\n", argv[1]);
        return 2;
    }
    std::printf(fails ? "\n%d FAILURES\n" : "\nall %d checks passed\n",
                fails ? fails : files);
    return fails ? 1 : 0;
}
