#include "meshstrip.hpp"

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace meshstrip {

int dbgStripCount = 0;
int dbgStripMax = 0;
float dbgStripAvg = 0.0f;
size_t dbgPadding = 0;

namespace {

// Two corners may share one strip vertex only when EVERY attribute the GS
// receives for them is identical - position, normal and UV - because a strip
// vertex is submitted once and read by up to three triangles. So the key is
// the raw 32 bytes, compared exactly rather than with a tolerance: these
// floats came out of one bake from one source value, so a genuinely shared
// corner is bit-identical, and anything that merely rounds to the same place
// is a different corner that must stay separate (a hard normal crease and a UV
// seam are exactly that, and welding them is what makes a model look melted).
struct Key {
    float v[8];
    bool operator==(const Key& o) const {
        return std::memcmp(v, o.v, sizeof(v)) == 0;
    }
};

struct KeyHash {
    size_t operator()(const Key& k) const {
        uint32_t h = 2166136261u;  // FNV-1a over the bytes
        uint32_t words[8];
        std::memcpy(words, k.v, sizeof(words));
        for (uint32_t w : words) {
            h ^= w;
            h *= 16777619u;
        }
        return h;
    }
};

struct Tri {
    uint32_t a = 0, b = 0, c = 0;
    bool used = false;
};

// (min,max) index pair -> the triangles sharing it. Two is the normal case;
// a non-manifold edge with more is handled by taking the first unused one.
struct EdgeHash {
    size_t operator()(const uint64_t& e) const {
        return (size_t)(e ^ (e >> 29) * 0x9E3779B97F4A7C15ull);
    }
};

uint64_t edgeKey(uint32_t x, uint32_t y) {
    return x < y ? ((uint64_t)x << 32) | y : ((uint64_t)y << 32) | x;
}

// The third vertex of triangle t, given two of its corners.
uint32_t oppositeVertex(const Tri& t, uint32_t x, uint32_t y) {
    if (t.a != x && t.a != y) return t.a;
    if (t.b != x && t.b != y) return t.b;
    return t.c;
}

bool triHasEdge(const Tri& t, uint32_t x, uint32_t y) {
    const bool hx = t.a == x || t.b == x || t.c == x;
    const bool hy = t.a == y || t.b == y || t.c == y;
    return hx && hy;
}

}  // namespace

bool build(const std::vector<float>& verts,
           const std::vector<unsigned char>& ao, unsigned run,
           std::vector<float>& outVerts, std::vector<unsigned char>& outAo) {
    if (run < 6 || run % 3 != 0) return false;
    const size_t corners = verts.size() / 8;
    if (corners < 3 || corners % 3 != 0 || verts.size() % 8 != 0) return false;
    const bool hasAo = ao.size() == corners;

    // 1. Weld identical corners. `unique` keeps ONE source corner index per
    //    distinct vertex, which is what the AO table is re-indexed through.
    std::unordered_map<Key, uint32_t, KeyHash> seen;
    seen.reserve(corners * 2);
    std::vector<uint32_t> unique;   // unique id -> source corner index
    std::vector<uint32_t> cornerId(corners);
    unique.reserve(corners);
    for (size_t c = 0; c < corners; ++c) {
        Key k;
        std::memcpy(k.v, &verts[c * 8], sizeof(k.v));
        auto it = seen.find(k);
        if (it == seen.end()) {
            const uint32_t id = (uint32_t)unique.size();
            unique.push_back((uint32_t)c);
            seen.emplace(k, id);
            cornerId[c] = id;
        } else {
            cornerId[c] = it->second;
        }
    }

    // 2. Triangles over welded ids. A triangle that welds to a degenerate one
    //    (two corners of one source triangle were identical) draws nothing and
    //    is dropped rather than carried through the walk.
    std::vector<Tri> tris;
    tris.reserve(corners / 3);
    for (size_t t = 0; t + 2 < corners; t += 3) {
        Tri tri{cornerId[t], cornerId[t + 1], cornerId[t + 2], false};
        if (tri.a == tri.b || tri.b == tri.c || tri.a == tri.c) continue;
        tris.push_back(tri);
    }
    if (tris.empty()) return false;

    // 3. Edge adjacency.
    std::unordered_map<uint64_t, std::vector<uint32_t>, EdgeHash> edges;
    edges.reserve(tris.size() * 3);
    for (uint32_t i = 0; i < (uint32_t)tris.size(); ++i) {
        const Tri& t = tris[i];
        edges[edgeKey(t.a, t.b)].push_back(i);
        edges[edgeKey(t.b, t.c)].push_back(i);
        edges[edgeKey(t.c, t.a)].push_back(i);
    }

    auto neighbourOf = [&](uint32_t x, uint32_t y, uint32_t exclude) -> int {
        auto it = edges.find(edgeKey(x, y));
        if (it == edges.end()) return -1;
        for (uint32_t cand : it->second)
            if (cand != exclude && !tris[cand].used && triHasEdge(tris[cand], x, y))
                return (int)cand;
        return -1;
    };

    // 4. Greedy walk. Start anywhere unused, extend off the strip's trailing
    //    edge for as long as a neighbour exists. Winding is not maintained,
    //    which is legal here (nothing backface-culls) and roughly doubles how
    //    far a walk gets before it has to stop.
    //
    //    WHERE a walk starts and WHICH WAY it faces decide almost everything,
    //    and getting either by accident is the difference between a strip and
    //    a pile of quads. On the regular grid every road, terrain chunk and
    //    wall is made of, all but one way out of the seed dead-ends after a
    //    single extension. Measured on one 200-cell row: seeding in index
    //    order with three orientations gives 201 strips of mean length 4 -
    //    no saving at all, the whole mesh refused - while the two rules below
    //    give ONE strip of 402, a 0.345x vertex count.
    std::vector<std::vector<uint32_t>> strips;
    std::vector<uint32_t> touched, bestTouched;
    std::vector<uint32_t> strip, bestStrip;
    // Six seed orientations, not three. A strip's trailing pair is ORDERED -
    // after [.., x, y] + z the next edge is (y, z), never (z, y) - so the
    // three cyclic rotations reach only half the ways out of the seed
    // triangle. The other half is the reversed triangle, which is free here
    // because nothing backface-culls. Measured on a single 200-cell COLUMN of
    // the same grid a 200-cell row strips perfectly: three orientations give
    // 200 strips of length 4, six give one strip of 402.
    auto walk = [&](uint32_t start, int orient) {
        strip.clear();
        touched.clear();
        const Tri& t0 = tris[start];
        const uint32_t v[3] = {orient < 3 ? t0.a : t0.c, t0.b,
                               orient < 3 ? t0.c : t0.a};
        const int rot = orient % 3;
        strip.push_back(v[rot]);
        strip.push_back(v[(rot + 1) % 3]);
        strip.push_back(v[(rot + 2) % 3]);
        tris[start].used = true;
        touched.push_back(start);
        uint32_t last = start;
        for (;;) {
            const uint32_t x = strip[strip.size() - 2];
            const uint32_t y = strip.back();
            const int next = neighbourOf(x, y, last);
            if (next < 0) break;
            tris[next].used = true;
            touched.push_back((uint32_t)next);
            strip.push_back(oppositeVertex(tris[next], x, y));
            last = (uint32_t)next;
        }
    };
    // Seed order matters as much as the rotation, and for the same reason. A
    // triangle in the middle of a sheet has three ways out, so starting there
    // eats a neighbour the long chain needed and strands the rest; a triangle
    // on a BORDER has one or two, and a walk that begins at the edge runs the
    // whole way across. Seeds are therefore taken fewest-neighbours-first.
    // Measured on a single 200-cell row: index order gives 201 strips of mean
    // length 4 (and no saving at all), this gives one strip of 402. It is not
    // sufficient on its own - see the six orientations above.
    std::vector<uint32_t> order(tris.size());
    {
        std::vector<uint32_t> degree(tris.size(), 0);
        for (const auto& e : edges)
            if (e.second.size() > 1)
                for (uint32_t t : e.second) ++degree[t];
        for (uint32_t i = 0; i < (uint32_t)tris.size(); ++i) order[i] = i;
        // Counting sort over 0..3 - the only degrees a triangle can have.
        std::vector<uint32_t> sorted;
        sorted.reserve(tris.size());
        for (uint32_t d = 0; d <= 3; ++d)
            for (uint32_t i : order)
                if (degree[i] == d) sorted.push_back(i);
        for (uint32_t i : order)
            if (degree[i] > 3) sorted.push_back(i);  // non-manifold, last
        order.swap(sorted);
    }

    for (uint32_t start : order) {
        if (tris[start].used) continue;
        bestStrip.clear();
        for (int orient = 0; orient < 6; ++orient) {
            walk(start, orient);
            if (strip.size() > bestStrip.size()) {
                bestStrip = strip;
                bestTouched = touched;
            }
            for (uint32_t t : touched) tris[t].used = false;
        }
        for (uint32_t t : bestTouched) tris[t].used = true;
        strips.push_back(bestStrip);
    }

    // 5. Pack the strips into runs.
    //
    // EVERY run but the last is exactly `run` vertices long, and that is not
    // tidiness - it is the contract. StaPipCore slices a stripped bag's array
    // at multiples of the pinned package size, so a run shorter than that
    // would put a package boundary in the middle of the NEXT run and splice
    // two unrelated vertices into one triangle. closeRun therefore pads with
    // repeats of the last vertex (degenerate triangles, rasterised to
    // nothing), and the packer keeps a run open across strips so the padding
    // is the few vertices at the end rather than a third of the array.
    std::vector<uint32_t> out;  // welded ids, run-concatenated
    out.reserve(corners);
    size_t runStart = 0;
    auto runLen = [&]() { return out.size() - runStart; };
    auto closeRun = [&](bool last) {
        if (runLen() == 0) return;
        // The last run only owes the multiple of 3 the VU1 loops need; every
        // other one owes the full package.
        const size_t target = last ? ((runLen() + 2) / 3) * 3 : run;
        while (runLen() < target) out.push_back(out.back());
        runStart = out.size();
    };

    for (const std::vector<uint32_t>& strip : strips) {
        size_t at = 0;
        while (strip.size() - at >= 3) {
            const bool empty = runLen() == 0;
            const size_t join = empty ? 0 : 2;
            // Keep room for the join and for a whole triangle. (With an empty
            // run this is 3 against a run of at least 6, so the close below
            // can never spin: it only ever fires on a non-empty run.)
            if (runLen() + join + 3 > run) {
                closeRun(false);
                continue;
            }
            const size_t room = run - runLen() - join;
            const size_t left = strip.size() - at;
            const size_t take = left < room ? left : room;
            if (!empty) {
                // Degenerate join: repeat the run's last vertex and the
                // incoming strip's first. Four zero-area triangles, and the
                // fifth is the incoming strip's own first real one.
                out.push_back(out.back());
                out.push_back(strip[at]);
            }
            for (size_t k = 0; k < take; ++k) out.push_back(strip[at + k]);
            if (take == left) break;
            // The run filled up mid-strip: carry the two-vertex overlap into
            // the next one, or the triangle across the cut is lost.
            at += take - 2;
            closeRun(false);
        }
    }
    closeRun(true);
    if (out.empty()) return false;

    dbgStripCount = (int)strips.size();
    dbgStripMax = 0;
    size_t total = 0;
    for (const std::vector<uint32_t>& st : strips) {
        total += st.size();
        if ((int)st.size() > dbgStripMax) dbgStripMax = (int)st.size();
    }
    dbgStripAvg = strips.empty() ? 0.0f : (float)total / (float)strips.size();
    dbgPadding = out.size() > total ? out.size() - total : 0;

    // A strip that is not smaller than the list it replaces is not worth the
    // second copy in the .tmdl, nor the extra route through the engine.
    if (out.size() >= corners) return false;

    outVerts.clear();
    outVerts.resize(out.size() * 8);
    outAo.clear();
    if (hasAo) outAo.resize(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        const uint32_t src = unique[out[i]];
        std::memcpy(&outVerts[i * 8], &verts[(size_t)src * 8], 8 * sizeof(float));
        if (hasAo) outAo[i] = ao[src];
    }
    return true;
}

}  // namespace meshstrip
