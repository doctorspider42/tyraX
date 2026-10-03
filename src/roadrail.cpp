#include "roadrail.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

// Rails and tram tracks (docs/roads.md "Rails and tram tracks"). Host-only,
// like the kerbs: the codegen bakes the strips and the console uploads them
// unchanged with the kerb chunks, so there is no EE twin to keep in step.
namespace roadrail {

using roadgen::CrossingPlan;
using roadgen::CrossingRoad;
using roadgen::HeightFn;
using roadgen::KerbVertex;
using roadgen::Surface;
using roadgen::Vertex;

void shadeRgb(float shade, float rgb[3]) {
    if (shade >= 1.5f) {
        const int k = std::clamp((int)shade - 2, 0, kPaletteCount - 1);
        for (int c = 0; c < 3; ++c) rgb[c] = kPalette[k][c];
        return;
    }
    // The kerb's concrete: light grey, a hair warm.
    rgb[0] = shade;
    rgb[1] = shade * 0.98f;
    rgb[2] = shade * 0.94f;
}

bool anyRails(const std::vector<CrossingRoad>& roads) {
    for (const CrossingRoad& r : roads)
        if ((r.kind == kRail || r.kind == kTram) && r.points.size() >= 4) return true;
    return false;
}

namespace {

float scaleOf(const CrossingRoad& r) {
    return std::clamp(r.railGauge, 0.3f, 3.0f) / kStandardGauge;
}

std::vector<float> trackCentres(const CrossingRoad& r) {
    if (std::clamp(r.tracks, 1, 2) == 1) return {0.0f};
    const float half =
        0.5f * (r.kind == kRail ? kTrackSpacingRail : kTrackSpacingTram) * scaleOf(r);
    return {-half, half};
}

// One station of the road: its centre and unit right vector.
struct Station {
    float x, z, rx, rz;
};

// The road's OWN stations, exactly the rows its surface is built from. The
// row builder is private to roadgen.cpp, but tessellate() over a FLAT height
// emits it verbatim: with no height every row is level, so each station pair
// collapses to the one full-width quad (spanCuts' flat path), six vertices
// whose first two are the row's two edges. A 2-unit width puts those edges
// exactly one unit either side of the centre, along the right vector. This
// reads them back instead of copying the Catmull-Rom sampler a third time.
std::vector<Station> stationsOf(const CrossingRoad& r) {
    std::vector<Vertex> tris;
    roadgen::tessellate(r.points, 2.0f, HeightFn{}, tris, {}, r.sampleStep);
    std::vector<Station> st;
    auto push = [&](const Vertex& l, const Vertex& rt) {
        float rx = 0.5f * (rt.x - l.x), rz = 0.5f * (rt.z - l.z);
        const float len = std::hypot(rx, rz);
        if (len > 1e-6f) rx /= len, rz /= len;
        st.push_back({0.5f * (l.x + rt.x), 0.5f * (l.z + rt.z), rx, rz});
    };
    for (size_t i = 0; i + 5 < tris.size(); i += 6) {
        push(tris[i], tris[i + 1]);
        if (i + 6 >= tris.size()) push(tris[i + 5], tris[i + 2]);
    }
    return st;
}

// Even-odd point in an XZ ring (x0, z0, x1, z1, ...).
bool insideOutline(const std::vector<float>& o, float x, float z) {
    bool in = false;
    const size_t n = o.size() / 2;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const float ax = o[i * 2], az = o[i * 2 + 1], bx = o[j * 2], bz = o[j * 2 + 1];
        if ((az > z) != (bz > z)) {
            const float xi = ax + (z - az) * (bx - ax) / (bz - az);
            if (x < xi) in = !in;
        }
    }
    return in;
}

// Where a railway is ON ANOTHER ROAD: the footprint of every road that is not
// a railway, plus every drawn node patch one of them takes part in. A patch
// of railways alone (a fork's bed) is still the railway's own ground.
struct Footprint {
    Surface roads;
    std::vector<const std::vector<float>*> patches;
    bool empty = true;

    Footprint(const std::vector<CrossingRoad>& rs, const CrossingPlan& plan, bool want) {
        if (!want) return;
        for (const CrossingRoad& r : rs) {
            if (r.kind == kRail || r.points.size() < 4) continue;
            std::vector<Vertex> tris;
            roadgen::tessellate(r.points, r.width, HeightFn{}, tris, {}, r.sampleStep);
            roads.add(tris);
            empty = false;
        }
        roads.build();
        for (const roadgen::Crossing& c : plan.crossings) {
            if (c.kind != roadgen::kCrossPatch || c.patchDuplicate) continue;
            if (c.shape.outline.size() < 6) continue;
            bool road = false;
            for (int ri : c.roads) road |= rs[(size_t)ri].kind != kRail;
            if (road) patches.push_back(&c.shape.outline);
        }
    }
    bool at(float x, float z) const {
        if (!empty && roads.at(x, z) != Surface::kNone) return true;
        for (const std::vector<float>* o : patches)
            if (insideOutline(*o, x, z)) return true;
        return false;
    }
};

// A point of a rail line before heights: where it is, the road's right vector
// there, and where its height is read (a cut point reads a hair inside its
// own run, so the two runs meeting there each stand on their own surface).
struct LinePt {
    float x, z, rx, rz, px, pz;
};

LinePt lerpPt(const LinePt& a, const LinePt& b, float f) {
    LinePt q{a.x + (b.x - a.x) * f, a.z + (b.z - a.z) * f, a.rx + (b.rx - a.rx) * f,
             a.rz + (b.rz - a.rz) * f, 0.0f, 0.0f};
    const float l = std::hypot(q.rx, q.rz);
    if (l > 1e-6f) q.rx /= l, q.rz /= l;
    q.px = q.x, q.pz = q.z;
    return q;
}

// Heights onto the drawn surface, then drop every point a chord already
// represents within kRailTolerance (sideways and in height).
void finishPiece(RailPiece piece, const std::vector<LinePt>& in, const HeightFn& surface,
                 const HeightFn& ground, float fallbackLift, std::vector<RailPiece>& out) {
    if (in.size() < 2) return;
    float len = 0.0f;
    for (size_t i = 1; i < in.size(); ++i)
        len += std::hypot(in[i].x - in[i - 1].x, in[i].z - in[i - 1].z);
    if (len < 0.05f) return;
    std::vector<float> y(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        float h = surface ? surface(in[i].px, in[i].pz) : Surface::kNone;
        if (!(h > -1.0e29f))
            h = (ground ? ground(in[i].x, in[i].z) : 0.0f) + roadgen::kLift + fallbackLift;
        y[i] = h;
    }
    std::vector<size_t> keep{0};
    size_t a = 0;
    while (a + 1 < in.size()) {
        size_t best = a + 1;
        for (size_t j = a + 2; j < in.size(); ++j) {
            const float cx = in[j].x - in[a].x, cz = in[j].z - in[a].z;
            const float cl = std::hypot(cx, cz);
            if (cl > kRailMaxRun || cl < 1e-5f) break;
            bool ok = true;
            for (size_t k = a + 1; k < j && ok; ++k) {
                const float px = in[k].x - in[a].x, pz = in[k].z - in[a].z;
                const float t = (px * cx + pz * cz) / (cl * cl);
                const float lat = std::fabs(px * cz - pz * cx) / cl;
                const float dy = std::fabs(y[k] - (y[a] + (y[j] - y[a]) * t));
                ok = t > 0.0f && t < 1.0f && lat <= kRailTolerance && dy <= kRailTolerance &&
                     in[k].rx * in[a].rx + in[k].rz * in[a].rz > 0.9995f;
            }
            if (!ok) break;
            best = j;
        }
        keep.push_back(best);
        a = best;
    }
    piece.pts.clear();
    for (size_t k : keep)
        piece.pts.insert(piece.pts.end(), {in[k].x, y[k], in[k].z, in[k].rx, in[k].rz});
    out.push_back(std::move(piece));
}

// The profile's bands: lateral offsets o0..o1 and heights y0..y1 above the
// line, one strip each.
struct Band {
    float o0, y0, o1, y1, shade;
};

int bandsOf(const RailPiece& p, Band out[3]) {
    const float s = p.scale;
    const float hw = 0.5f * kRailHeadWidth * s;
    switch (p.profile) {
        case kProfileRaised: {
            const float h = kRailHeight * s, sk = -kRailSink * s;
            out[0] = {-hw, sk, -hw, h, (float)kShadeRailSide};
            out[1] = {-hw, h, hw, h, (float)kShadeRailTop};
            out[2] = {hw, h, hw, sk, (float)kShadeRailSide};
            return 3;
        }
        case kProfileFlush: {
            const float sg = p.inner >= 0.0f ? 1.0f : -1.0f;
            out[0] = {-hw, kFlushLift, hw, kFlushLift, (float)kShadeRailTop};
            out[1] = {sg * hw, kFlushLift, sg * (hw + kGrooveWidth * s), kFlushLift,
                      (float)kShadeGroove};
            return 2;
        }
        default: {
            const float ph = (0.5f * kStandardGauge + kRailHeadWidth + kPanelOverhang) * s;
            out[0] = {-ph, kPanelLift, ph, kPanelLift, (float)kShadePanel};
            return 1;
        }
    }
}

KerbVertex at(const RailPiece& p, int i, float o, float dy, float shade) {
    const float* q = &p.pts[(size_t)i * 5];
    return {q[0] + q[3] * o, q[1] + dy, q[2] + q[4] * o, shade};
}

}  // namespace

std::vector<float> railOffsets(const CrossingRoad& r) {
    std::vector<float> out;
    const float half = 0.5f * (std::clamp(r.railGauge, 0.3f, 3.0f) + kRailHeadWidth * scaleOf(r));
    for (float c : trackCentres(r)) out.insert(out.end(), {c - half, c + half});
    return out;
}

std::vector<RailPiece> planRails(const std::vector<CrossingRoad>& roads,
                                 const CrossingPlan& plan, const HeightFn& surface,
                                 const HeightFn& ground) {
    std::vector<RailPiece> out;
    if (!anyRails(roads)) return out;
    bool anyRailway = false;
    for (const CrossingRoad& r : roads) anyRailway |= r.kind == kRail;
    // Only a railway asks "am I on another road?" - a tram's rails are flush
    // everywhere.
    const Footprint footprint(roads, plan, anyRailway);
    const Footprint* foot = &footprint;

    for (int ri = 0; ri < (int)roads.size(); ++ri) {
        const CrossingRoad& R = roads[(size_t)ri];
        if ((R.kind != kRail && R.kind != kTram) || R.points.size() < 4) continue;
        const std::vector<Station> st = stationsOf(R);
        if (st.size() < 2) continue;
        const float s = scaleOf(R);
        const float lift = roadgen::rankLift(R.rank);
        // A line at a lateral offset, split into runs on / off another road.
        auto sweep = [&](float off, bool panel, float inner) {
            std::vector<LinePt> pts;
            for (const Station& q : st) {
                const float x = q.x + q.rx * off, z = q.z + q.rz * off;
                pts.push_back({x, z, q.rx, q.rz, x, z});
            }
            const bool tram = R.kind == kTram;
            std::vector<char> on(pts.size(), tram ? 1 : 0);
            if (!tram)
                for (size_t i = 0; i < pts.size(); ++i) on[i] = foot->at(pts[i].x, pts[i].z);
            auto emit = [&](std::vector<LinePt>& run, bool flush) {
                if (panel && !flush) {
                    run.clear();
                    return;
                }
                RailPiece piece;
                piece.road = ri;
                piece.profile = panel ? kProfilePanel : (flush ? kProfileFlush : kProfileRaised);
                piece.scale = s;
                piece.inner = inner;
                finishPiece(piece, run, surface, ground, lift, out);
                run.clear();
            };
            std::vector<LinePt> cur;
            for (size_t i = 0; i < pts.size(); ++i) {
                if (i > 0 && on[i] != on[i - 1]) {
                    // The cut, by bisection: the last point still like i - 1.
                    float lo = 0.0f, hi = 1.0f;
                    for (int it = 0; it < 14; ++it) {
                        const float mid = 0.5f * (lo + hi);
                        const LinePt m = lerpPt(pts[i - 1], pts[i], mid);
                        if ((char)foot->at(m.x, m.z) == on[i - 1]) lo = mid; else hi = mid;
                    }
                    LinePt q = lerpPt(pts[i - 1], pts[i], lo);
                    float dx = pts[i].x - pts[i - 1].x, dz = pts[i].z - pts[i - 1].z;
                    const float dl = std::hypot(dx, dz);
                    if (dl > 1e-6f) dx /= dl, dz /= dl;
                    LinePt end = q, start = q;
                    end.px = q.x - dx * 0.03f, end.pz = q.z - dz * 0.03f;
                    start.px = q.x + dx * 0.03f, start.pz = q.z + dz * 0.03f;
                    cur.push_back(end);
                    emit(cur, on[i - 1] != 0);
                    cur.push_back(start);
                }
                cur.push_back(pts[i]);
            }
            emit(cur, on.back() != 0);
        };
        const std::vector<float> offs = railOffsets(R);
        for (size_t k = 0; k < offs.size(); ++k) sweep(offs[k], false, k % 2 == 0 ? 1.0f : -1.0f);
        // Level-crossing panels under each track where a railway crosses a road.
        if (R.kind == kRail)
            for (float c : trackCentres(R)) sweep(c, true, 0.0f);
    }
    // Panels first: in a chunk they draw before the rails that lie on them.
    std::stable_sort(out.begin(), out.end(), [](const RailPiece& a, const RailPiece& b) {
        return (a.profile == kProfilePanel) > (b.profile == kProfilePanel);
    });
    return out;
}

void railTriangles(const RailPiece& p, std::vector<KerbVertex>& out) {
    Band bands[3];
    const int nb = bandsOf(p, bands);
    for (int b = 0; b < nb; ++b) {
        const Band& d = bands[b];
        for (int i = 0; i + 1 < p.points(); ++i) {
            // The strip's own triangles: A0 B0 A1, B0 A1 B1.
            const KerbVertex a0 = at(p, i, d.o0, d.y0, d.shade), b0 = at(p, i, d.o1, d.y1, d.shade);
            const KerbVertex a1 = at(p, i + 1, d.o0, d.y0, d.shade),
                             b1 = at(p, i + 1, d.o1, d.y1, d.shade);
            out.insert(out.end(), {a0, b0, a1, b0, a1, b1});
        }
    }
}

void addRailsToSurface(Surface& s, const std::vector<CrossingRoad>& roads,
                       const CrossingPlan& plan, const HeightFn& ground) {
    if (!anyRails(roads)) return;
    s.build();
    const std::vector<RailPiece> pieces =
        planRails(roads, plan, [&](float x, float z) { return s.at(x, z); }, ground);
    std::vector<KerbVertex> kv;
    for (const RailPiece& p : pieces) railTriangles(p, kv);
    std::vector<Vertex> tris;
    for (size_t i = 0; i + 2 < kv.size(); i += 3) {
        const KerbVertex &a = kv[i], &b = kv[i + 1], &c = kv[i + 2];
        // A side face has no area in XZ: the runtime's barycentric test skips it.
        const float area = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        if (std::fabs(area) < 1e-6f) continue;
        for (const KerbVertex* v : {&a, &b, &c}) tris.push_back({v->x, v->y, v->z, 0.0f, 0.0f});
    }
    s.add(tris, 1.0f);
}

void railStrips(const std::vector<RailPiece>& pieces, std::vector<KerbVertex>& out,
                std::vector<int>& chunkSizes) {
    out.clear();
    chunkSizes.clear();
    // Cut into kKerbCell cells by segment midpoint, as the kerbs are: one
    // cell's pieces share chunks, so the frustum and the draw distance drop
    // rails a block at a time.
    struct Sub {
        size_t piece;
        int first, last;  // point range, inclusive
    };
    std::map<std::pair<int, int>, std::vector<Sub>> cells;
    for (size_t pi = 0; pi < pieces.size(); ++pi) {
        const RailPiece& p = pieces[pi];
        const int n = p.points();
        if (n < 2) continue;
        auto cellOf = [&](int seg) {
            const float* a = &p.pts[(size_t)seg * 5];
            const float* b = &p.pts[(size_t)(seg + 1) * 5];
            return std::make_pair((int)std::floor(0.5f * (a[0] + b[0]) / roadgen::kKerbCell),
                                  (int)std::floor(0.5f * (a[2] + b[2]) / roadgen::kKerbCell));
        };
        int first = 0;
        std::pair<int, int> cell = cellOf(0);
        for (int s = 1; s < n - 1; ++s) {
            const std::pair<int, int> c = cellOf(s);
            if (c == cell) continue;
            cells[cell].push_back({pi, first, s});
            first = s;
            cell = c;
        }
        cells[cell].push_back({pi, first, n - 1});
    }

    // roadgen::kerbStrips' run contract, verbatim: runs of exactly kStripRun,
    // a full run carries its last two vertices over, unrelated strips join by
    // repeating a vertex either side, a chunk's last run pads to a multiple of 3.
    const size_t run = (size_t)roadgen::kStripRun;
    size_t chunkStart = 0, runStart = 0;
    bool open = false;
    auto runLen = [&]() { return out.size() - runStart; };
    auto pushRaw = [&](const KerbVertex& v) {
        if (runLen() == run) {
            const KerbVertex a = out[out.size() - 2];
            const KerbVertex b = out[out.size() - 1];
            runStart = out.size();
            out.push_back(a);
            out.push_back(b);
        }
        out.push_back(v);
    };
    auto startStrip = [&](const KerbVertex& v) {
        if (runLen() > 0) {
            const KerbVertex last = out.back();
            pushRaw(last);
            pushRaw(v);
        }
        pushRaw(v);
    };
    auto closeChunk = [&]() {
        if (!open) return;
        const size_t target = ((runLen() + 2) / 3) * 3;
        while (runLen() < target) out.push_back(out.back());
        chunkSizes.push_back((int)(out.size() - chunkStart));
        chunkStart = runStart = out.size();
        open = false;
    };
    for (const auto& [cell, subs] : cells) {
        (void)cell;
        closeChunk();
        for (const Sub& sb : subs) {
            const RailPiece& p = pieces[sb.piece];
            Band bands[3];
            const int nb = bandsOf(p, bands);
            const int m = sb.last - sb.first + 1;
            const size_t strip = (size_t)(2 * m + 2);
            const size_t cost = strip * (size_t)nb + strip * (size_t)nb / (run - 2) * 2 + 2;
            if (open && (out.size() - chunkStart) + cost > (size_t)roadgen::kChunkBudget)
                closeChunk();
            open = true;
            for (int b = 0; b < nb; ++b) {
                const Band& d = bands[b];
                for (int i = sb.first; i <= sb.last; ++i) {
                    const KerbVertex a = at(p, i, d.o0, d.y0, d.shade);
                    if (i == sb.first) startStrip(a); else pushRaw(a);
                    pushRaw(at(p, i, d.o1, d.y1, d.shade));
                }
            }
        }
        closeChunk();
    }
}

}  // namespace roadrail
