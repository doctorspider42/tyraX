#include "roadbridge.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>

#include "project.hpp"

// Bridges (docs/roads.md "Bridges"). See roadbridge.hpp for the model; this
// file is the deck, the structure and the console tables.
namespace roadbridge {

namespace {

inline float cr(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

// Control index -> stored index, roadgen's pointAt rule (clamped ends, a
// closed loop wraps over its repeated endpoint).
inline int wrapIndex(const std::vector<float>& pts, int i) {
    const int n = (int)(pts.size() / 2);
    if (roadgen::isClosed(pts)) return (i % (n - 1) + n - 1) % (n - 1);
    return std::clamp(i, 0, n - 1);
}

struct XZ {
    float x, z;
};
inline XZ pointAt(const std::vector<float>& pts, int i) {
    const int k = wrapIndex(pts, i);
    return {pts[(size_t)k * 2], pts[(size_t)k * 2 + 1]};
}
inline XZ sample(const std::vector<float>& pts, int seg, float t) {
    const XZ p0 = pointAt(pts, seg - 1), p1 = pointAt(pts, seg);
    const XZ p2 = pointAt(pts, seg + 1), p3 = pointAt(pts, seg + 2);
    return {cr(p0.x, p1.x, p2.x, p3.x, t), cr(p0.z, p1.z, p2.z, p3.z, t)};
}

// Concrete greys, baked: a fixed sun so the faces of a parapet read apart.
float shade(float nx, float ny, float nz) {
    const float lx = 0.45f, ly = 0.8f, lz = 0.35f;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    const float d = std::max(0.0f, (nx * lx + ny * ly + nz * lz) / ll);
    if (ny < -0.5f) return 0.30f;  // the underside is in its own shadow
    return std::clamp(0.40f + 0.48f * d, 0.0f, 1.0f);
}

void quad(std::vector<roadgen::KerbVertex>& out, const float* a, const float* b, const float* c,
          const float* d, float s) {
    const roadgen::KerbVertex A{a[0], a[1], a[2], s}, B{b[0], b[1], b[2], s};
    const roadgen::KerbVertex C{c[0], c[1], c[2], s}, D{d[0], d[1], d[2], s};
    out.insert(out.end(), {A, B, C, A, C, D});
}

}  // namespace

float Deck::glued(float x, float z) const { return (ground ? ground(x, z) : 0.0f) + lift; }

float Deck::deckAt(int i, float x, float z) const {
    return std::max(glued(x, z), st[(size_t)i].y);
}

float Deck::elevationAt(float x, float z) const {
    if (st.size() < 2) return 0.0f;
    float best = 1e30f, y = 0.0f;
    for (size_t i = 0; i + 1 < st.size(); ++i) {
        const Station& a = st[i];
        const Station& b = st[i + 1];
        const float dx = b.x - a.x, dz = b.z - a.z;
        const float ll = dx * dx + dz * dz;
        float f = ll > 1e-12f ? ((x - a.x) * dx + (z - a.z) * dz) / ll : 0.0f;
        f = std::clamp(f, 0.0f, 1.0f);
        const float px = a.x + dx * f - x, pz = a.z + dz * f - z;
        const float d2 = px * px + pz * pz;
        if (d2 < best) {
            best = d2;
            y = a.y + (b.y - a.y) * f;
        }
    }
    const float reach = halfWidth + kParapetWidth + 0.5f;
    if (best > reach * reach) return 0.0f;
    return std::max(0.0f, y - glued(x, z));
}

Deck buildDeck(const std::vector<float>& pts, const std::vector<float>& heights, float width,
               float sampleStep, int rank, const roadgen::HeightFn& ground) {
    Deck d;
    d.ground = ground;
    d.lift = roadgen::kLift + roadgen::rankLift(rank);
    const int n = (int)(pts.size() / 2);
    if (n < 2) return d;
    const float hw = 0.5f * (width > 0.1f ? width : 0.1f);
    d.halfWidth = hw;
    d.crossSteps = std::max(1, (int)std::ceil((hw * 2.0f) / roadgen::kCrossSampleStep));
    // The deck height at each control point: the glued road there + its height.
    auto controlY = [&](int i) {
        const XZ p = pointAt(pts, i);
        const int k = wrapIndex(pts, i);
        const float h = k < (int)heights.size() ? std::clamp(heights[(size_t)k], 0.0f, kMaxHeight)
                                                : 0.0f;
        return d.glued(p.x, p.z) + h;
    };
    // MIRRORS roadgen.cpp's buildRows station sampling (spacing, the 0.05
    // tangent, the 1-unit texture arc), so a bridge on the ground is exactly
    // where its glued self would be and joins its nodes the same way.
    float arc = 0.0f;
    XZ prev = sample(pts, 0, 0.0f);
    for (int seg = 0; seg < n - 1; ++seg) {
        const XZ a = pointAt(pts, seg), b = pointAt(pts, seg + 1);
        const float segLen = std::hypot(b.x - a.x, b.z - a.z);
        const float spacing = std::clamp(sampleStep, 1.0f, 2.0f);
        const int steps = segLen > spacing ? (int)(segLen / spacing) + 1 : 1;
        const float y0 = controlY(seg - 1), y1 = controlY(seg), y2 = controlY(seg + 1),
                    y3 = controlY(seg + 2);
        for (int k = (seg == 0 ? 0 : 1); k <= steps; ++k) {
            const float t = (float)k / (float)steps;
            const XZ c = sample(pts, seg, t);
            XZ from = c, to = c;
            if (roadgen::isClosed(pts) && seg == n - 2 && t == 1.0f) {
                to = sample(pts, 0, 0.05f);
            } else if (t + 0.05f <= 1.0f || seg + 1 < n - 1) {
                to = t + 0.05f <= 1.0f ? sample(pts, seg, t + 0.05f) : sample(pts, seg + 1, 0.05f);
            } else {
                from = sample(pts, seg, std::max(0.0f, t - 0.05f));
            }
            float tx = to.x - from.x, tz = to.z - from.z;
            const float tl = std::hypot(tx, tz);
            if (tl > 1e-6f) {
                tx /= tl;
                tz /= tl;
            } else {
                tx = 0.0f;
                tz = 1.0f;
            }
            const float prevT = k > 0 ? (float)(k - 1) / (float)steps : 0.0f;
            const int arcSteps =
                k > 0 ? std::max(1, (int)std::ceil((t - prevT) * segLen / roadgen::kArcSampleStep))
                      : 0;
            for (int ak = 1; ak <= arcSteps; ++ak) {
                const XZ ap = sample(pts, seg, prevT + (t - prevT) * ((float)ak / (float)arcSteps));
                arc += std::hypot(ap.x - prev.x, ap.z - prev.z);
                prev = ap;
            }
            prev = c;
            Station s;
            s.x = c.x;
            s.z = c.z;
            s.tx = tx;
            s.tz = tz;
            s.v = arc / roadgen::kTexLen;
            // The profile: Catmull-Rom of the control heights, clamped between
            // the segment's own two so a plateau never bulges and a ramp
            // never dips below its foot.
            const float y = cr(y0, y1, y2, y3, t);
            s.y = std::clamp(y, std::min(y1, y2), std::max(y1, y2));
            d.st.push_back(s);
        }
    }
    return d;
}

Deck buildDeck(const SceneObject& o, const roadgen::HeightFn& ground) {
    return buildDeck(o.roadPoints, o.roadHeights, o.roadWidth, o.roadSampleStep, o.roadRank,
                     ground);
}

void tessellateDeck(const Deck& d, std::vector<roadgen::Vertex>& out) {
    out.clear();
    if (d.empty()) return;
    const int cs = d.crossSteps;
    const float hw = d.halfWidth;
    std::vector<std::vector<roadgen::Vertex>> rows(d.st.size());
    std::vector<char> up(d.st.size(), 1);
    for (size_t i = 0; i < d.st.size(); ++i) {
        const Station& s = d.st[i];
        const float rx = s.tz * hw, rz = -s.tx * hw;
        rows[i].resize((size_t)cs + 1);
        for (int j = 0; j <= cs; ++j) {
            const float u = (float)j / (float)cs;
            const float side = u * 2.0f - 1.0f;
            roadgen::Vertex& q = rows[i][(size_t)j];
            q.x = s.x + rx * side;
            q.z = s.z + rz * side;
            const float g = d.glued(q.x, q.z);
            q.y = std::max(g, s.y);
            if (!(s.y > g)) up[i] = 0;
            q.u = u;
            q.v = s.v;
        }
    }
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        const std::vector<roadgen::Vertex>& P = rows[i];
        const std::vector<roadgen::Vertex>& N = rows[i + 1];
        bool flat = true;
        for (int j = 0; j <= cs && flat; ++j)
            flat = std::fabs(P[(size_t)j].y - P[0].y) <= 1e-5f &&
                   std::fabs(N[(size_t)j].y - N[0].y) <= 1e-5f;
        // Wholly in the air (flat across by construction) or flat ground: one
        // full-width quad, cut P[0]-N[w] like roadgen's stitch.
        if ((up[i] && up[i + 1]) || flat) {
            out.insert(out.end(), {P[0], P[(size_t)cs], N[(size_t)cs], P[0], N[(size_t)cs], N[0]});
            continue;
        }
        for (int j = 0; j < cs; ++j) {
            const roadgen::Vertex &A = P[(size_t)j], &B = P[(size_t)j + 1];
            const roadgen::Vertex &C = N[(size_t)j + 1], &D = N[(size_t)j];
            out.insert(out.end(), {A, B, C, A, C, D});
        }
    }
}

void drawnRoad(const SceneObject& o, const roadgen::HeightFn& ground,
               std::vector<roadgen::Vertex>& out) {
    out.clear();
    if (o.roadPoints.size() < 4) return;
    if (o.roadBridge) {
        tessellateDeck(buildDeck(o, ground), out);
        return;
    }
    const float lift = roadgen::rankLift(o.roadRank);
    roadgen::tessellate(
        o.roadPoints, o.roadWidth,
        [&](float x, float z) { return (ground ? ground(x, z) : 0.0f) + lift; }, out, {},
        o.roadSampleStep);
}

void wallBoxes(float ax, float az, float bx, float bz, float thick, float ylo, float yhi,
               std::vector<CollisionBox>& out) {
    const float dx = bx - ax, dz = bz - az, len = std::hypot(dx, dz);
    if (len < 1e-4f || !(yhi > ylo)) return;
    const float ux = dx / len, uz = dz / len;
    // Local z runs along the wall: (ys, yc) = (ux, uz) in the object boxes'
    // frame, so local x is across it.
    const float nx = -uz * 0.5f * thick, nz = ux * 0.5f * thick;
    CollisionBox b;
    b.mn[0] = std::min({ax + nx, ax - nx, bx + nx, bx - nx});
    b.mx[0] = std::max({ax + nx, ax - nx, bx + nx, bx - nx});
    b.mn[2] = std::min({az + nz, az - nz, bz + nz, bz - nz});
    b.mx[2] = std::max({az + nz, az - nz, bz + nz, bz - nz});
    b.mn[1] = ylo;
    b.mx[1] = yhi;
    b.hx = 0.5f * thick;
    b.hz = 0.5f * len;
    b.ys = ux;
    b.yc = uz;
    out.push_back(b);
}

void buildStructure(const Deck& d, const std::vector<roadgen::CrossingRoad>& roads, int self,
                    Structure& out) {
    out = Structure{};
    if (d.empty()) return;
    const size_t n = d.st.size();
    const float hw = d.halfWidth;
    const float T = kParapetWidth, H = kParapetHeight;
    auto bare = [&](float x, float z) { return d.ground ? d.ground(x, z) : 0.0f; };
    // Per station: the side vectors and how far the deck clears the glued road.
    std::vector<float> rx(n), rz(n), arcAt(n), clear(n), gapMax(n);
    for (size_t i = 0; i < n; ++i) {
        const Station& s = d.st[i];
        rx[i] = s.tz;
        rz[i] = -s.tx;
        arcAt[i] = s.v * roadgen::kTexLen;
        float g = d.glued(s.x, s.z), b = bare(s.x, s.z);
        for (float side : {-1.0f, 1.0f}) {
            const float ex = s.x + rx[i] * (hw + T) * side, ez = s.z + rz[i] * (hw + T) * side;
            g = std::max(g, d.glued(ex, ez));
            b = std::max(b, bare(ex, ez));
        }
        clear[i] = s.y - g;
        gapMax[i] = s.y - kDeckDepth - b;  // the open height under the deck
    }
    auto P3 = [&](size_t i, float lateral, float y, float* o3) {
        o3[0] = d.st[i].x + rx[i] * lateral;
        o3[1] = y;
        o3[2] = d.st[i].z + rz[i] * lateral;
    };
    // The other roads, sampled, for the piers to keep off.
    struct Other {
        std::vector<XZ> p;
        float h;
    };
    std::vector<Other> others;
    for (int r = 0; r < (int)roads.size(); ++r) {
        if (r == self || roads[(size_t)r].points.size() < 4) continue;
        Other o;
        o.h = 0.5f * roads[(size_t)r].width;
        const int samples = std::max(16, roadgen::controlCount(roads[(size_t)r].points) * 24);
        for (int k = 0; k <= samples; ++k) {
            XZ q;
            roadgen::splineAt(roads[(size_t)r].points, (float)k / (float)samples, &q.x, &q.z);
            o.p.push_back(q);
        }
        others.push_back(std::move(o));
    }
    auto nearOtherRoad = [&](float x, float z, float radius) {
        for (const Other& o : others)
            for (size_t k = 0; k + 1 < o.p.size(); ++k) {
                const float dx = o.p[k + 1].x - o.p[k].x, dz = o.p[k + 1].z - o.p[k].z;
                const float ll = dx * dx + dz * dz;
                float f = ll > 1e-12f ? ((x - o.p[k].x) * dx + (z - o.p[k].z) * dz) / ll : 0.0f;
                f = std::clamp(f, 0.0f, 1.0f);
                const float ex = o.p[k].x + dx * f - x, ez = o.p[k].z + dz * f - z;
                if (std::hypot(ex, ez) < o.h + radius + 1.0f) return true;
            }
        return false;
    };
    // A vertical box (piers, abutments): corners in plan, a top per corner.
    auto box = [&](const std::array<XZ, 4>& c, const std::array<float, 4>& top) {
        float lo = 1e30f;
        for (const XZ& q : c) lo = std::min(lo, bare(q.x, q.z));
        lo -= kSink;
        for (int k = 0; k < 4; ++k) {
            const XZ& a = c[(size_t)k];
            const XZ& b = c[(size_t)((k + 1) % 4)];
            float nx = b.z - a.z, nz = -(b.x - a.x);
            const float nl = std::hypot(nx, nz);
            if (nl > 1e-6f) nx /= nl, nz /= nl;
            // Outward: away from the box centre.
            const float mx = 0.25f * (c[0].x + c[1].x + c[2].x + c[3].x);
            const float mz = 0.25f * (c[0].z + c[1].z + c[2].z + c[3].z);
            if ((0.5f * (a.x + b.x) - mx) * nx + (0.5f * (a.z + b.z) - mz) * nz < 0.0f)
                nx = -nx, nz = -nz;
            const float A[3] = {a.x, lo, a.z}, B[3] = {b.x, lo, b.z};
            const float C[3] = {b.x, top[(size_t)((k + 1) % 4)], b.z};
            const float D[3] = {a.x, top[(size_t)k], a.z};
            quad(out.tris, A, B, C, D, shade(nx, 0.0f, nz));
        }
    };

    // Runs of stations in the air.
    size_t i = 0;
    while (i < n) {
        if (!(clear[i] > kStructureMin)) {
            ++i;
            continue;
        }
        size_t i0 = i;
        while (i + 1 < n && clear[i + 1] > kStructureMin) ++i;
        size_t i1 = i;
        ++i;
        if (i1 == i0) continue;
        ++out.spans;
        // Kept stations: chords within kMergeTolerance of every edge point.
        std::vector<size_t> keep{i0};
        size_t a = i0;
        while (a < i1) {
            size_t best = a + 1;
            for (size_t b = a + 2; b <= i1; ++b) {
                if (arcAt[b] - arcAt[a] > kMaxChord) break;
                bool ok = true;
                for (size_t k = a + 1; k < b && ok; ++k) {
                    ok = rx[k] * rx[a] + rz[k] * rz[a] > 0.995f;
                    for (float side : {-1.0f, 1.0f}) {
                        const float ax = d.st[a].x + rx[a] * hw * side,
                                    az = d.st[a].z + rz[a] * hw * side;
                        const float bx = d.st[b].x + rx[b] * hw * side,
                                    bz = d.st[b].z + rz[b] * hw * side;
                        const float kx = d.st[k].x + rx[k] * hw * side,
                                    kz = d.st[k].z + rz[k] * hw * side;
                        const float cx = bx - ax, cz = bz - az, cl = std::hypot(cx, cz);
                        if (cl < 1e-5f) continue;
                        const float f = ((kx - ax) * cx + (kz - az) * cz) / (cl * cl);
                        const float lat = std::fabs((kx - ax) * cz - (kz - az) * cx) / cl;
                        const float dy = std::fabs(d.st[k].y -
                                                   (d.st[a].y + (d.st[b].y - d.st[a].y) * f));
                        ok = ok && lat <= kMergeTolerance && dy <= kMergeTolerance;
                    }
                }
                if (!ok) break;
                best = b;
            }
            keep.push_back(best);
            a = best;
        }
        // The parapet's inner face stands a tolerance ON the deck, so a chord
        // on the inside of a bend never opens a gap at the deck edge.
        const float inner = hw - kMergeTolerance, outer = hw + T;
        for (size_t k = 0; k + 1 < keep.size(); ++k) {
            const size_t p = keep[k], q = keep[k + 1];
            const float yp = d.st[p].y, yq = d.st[q].y;
            for (float side : {-1.0f, 1.0f}) {
                const float nx = (rx[p] + rx[q]) * 0.5f * side, nz = (rz[p] + rz[q]) * 0.5f * side;
                float a0[3], a1[3], b0[3], b1[3];
                // Inner face: from just under the deck to the parapet top.
                P3(p, inner * side, yp - 0.05f, a0), P3(q, inner * side, yq - 0.05f, b0);
                P3(p, inner * side, yp + H, a1), P3(q, inner * side, yq + H, b1);
                quad(out.tris, a0, b0, b1, a1, shade(-nx, 0.0f, -nz));
                // Top.
                float c0[3], c1[3];
                P3(p, outer * side, yp + H, c0), P3(q, outer * side, yq + H, c1);
                quad(out.tris, a1, b1, c1, c0, shade(0.0f, 1.0f, 0.0f));
                // Outer face, down to the underside: the deck's edge beam.
                float e0[3], e1[3];
                P3(p, outer * side, yp - kDeckDepth, e0), P3(q, outer * side, yq - kDeckDepth, e1);
                quad(out.tris, c0, c1, e1, e0, shade(nx, 0.0f, nz));
                // The parapet as a wall to collide with: deck top (the lower
                // end of a ramp chord) to the parapet top (its higher end).
                const float mid = 0.5f * (inner + outer) * side;
                float w0[3], w1[3];
                P3(p, mid, 0.0f, w0), P3(q, mid, 0.0f, w1);
                wallBoxes(w0[0], w0[2], w1[0], w1[2], outer - inner, std::min(yp, yq) - 0.05f,
                          std::max(yp, yq) + H, out.boxes);
            }
            float l0[3], l1[3], r0[3], r1[3];
            P3(p, -outer, yp - kDeckDepth, l0), P3(q, -outer, yq - kDeckDepth, l1);
            P3(p, outer, yp - kDeckDepth, r0), P3(q, outer, yq - kDeckDepth, r1);
            quad(out.tris, l0, l1, r1, r0, shade(0.0f, -1.0f, 0.0f));
        }
        // End caps: the parapets and the slab, at both ends of the run.
        for (size_t e : {i0, i1}) {
            const float dir = e == i0 ? -1.0f : 1.0f;
            const float tx = d.st[e].tx * dir, tz = d.st[e].tz * dir;
            const float y = d.st[e].y;
            for (float side : {-1.0f, 1.0f}) {
                float a0[3], a1[3], b1[3], b0[3];
                P3(e, inner * side, y - 0.05f, a0), P3(e, inner * side, y + H, a1);
                P3(e, outer * side, y + H, b1), P3(e, outer * side, y - kDeckDepth, b0);
                quad(out.tris, a0, a1, b1, b0, shade(tx, 0.0f, tz));
            }
            float s0[3], s1[3], s2[3], s3[3];
            P3(e, -outer, y - kDeckDepth, s0), P3(e, outer, y - kDeckDepth, s1);
            P3(e, outer, y, s2), P3(e, -outer, y, s3);
            quad(out.tris, s0, s1, s2, s3, shade(tx, 0.0f, tz));
        }
        // Abutments: where the gap under the deck first opens, from each end.
        auto findOpen = [&](size_t from, int step) -> long {
            for (long k = (long)from; k >= (long)i0 && k <= (long)i1; k += step)
                if (gapMax[(size_t)k] > kAbutmentGap) return k;
            return -1;
        };
        const long ia = findOpen(i0, 1), ib = findOpen(i1, -1);
        if (ia < 0 || ib < 0 || ib <= ia) continue;
        auto abutment = [&](size_t at, int towardEnd) {
            size_t j = at;
            while ((long)j + towardEnd >= (long)i0 && (long)j + towardEnd <= (long)i1 &&
                   std::fabs(arcAt[j] - arcAt[at]) < kAbutmentLength)
                j = (size_t)((long)j + towardEnd);
            if (j == at) return;
            std::array<XZ, 4> c;
            std::array<float, 4> top;
            const size_t s0 = std::min(at, j), s1 = std::max(at, j);
            float t3[3];
            P3(s0, -outer, 0.0f, t3), c[0] = {t3[0], t3[2]};
            P3(s1, -outer, 0.0f, t3), c[1] = {t3[0], t3[2]};
            P3(s1, outer, 0.0f, t3), c[2] = {t3[0], t3[2]};
            P3(s0, outer, 0.0f, t3), c[3] = {t3[0], t3[2]};
            top = {d.st[s0].y - kDeckDepth + 0.05f, d.st[s1].y - kDeckDepth + 0.05f,
                   d.st[s1].y - kDeckDepth + 0.05f, d.st[s0].y - kDeckDepth + 0.05f};
            box(c, top);
            ++out.abutments;
        };
        abutment((size_t)ia, -1);
        abutment((size_t)ib, 1);
        // Piers, evenly between the abutments, never on another road.
        const float span = arcAt[(size_t)ib] - arcAt[(size_t)ia];
        const int count = (int)std::ceil(span / kPierSpacing) - 1;
        const float pw = std::clamp(0.3f * hw, 0.8f, 4.0f);  // half the pier's width
        const float radius = std::hypot(0.5f * kPierLength, pw);
        for (int k = 1; k <= count; ++k) {
            const float target = arcAt[(size_t)ia] + span * (float)k / (float)(count + 1);
            size_t at = (size_t)ia;
            while (at < (size_t)ib && arcAt[at] < target) ++at;
            // Try the station, then its neighbours either way, up to a
            // quarter of the spacing.
            long chosen = -1;
            for (int off = 0; chosen < 0; ++off) {
                bool any = false;
                for (int sgn : {1, -1}) {
                    const long s = (long)at + sgn * off;
                    if (s <= ia || s >= ib) continue;
                    if (std::fabs(arcAt[(size_t)s] - target) > 0.25f * kPierSpacing) continue;
                    any = true;
                    if (gapMax[(size_t)s] > kPierMinGap &&
                        !nearOtherRoad(d.st[(size_t)s].x, d.st[(size_t)s].z, radius)) {
                        chosen = s;
                        break;
                    }
                    if (off == 0) break;
                }
                if (!any && off > 0) break;
            }
            if (chosen < 0) continue;
            const Station& s = d.st[(size_t)chosen];
            const float ax = s.tx * 0.5f * kPierLength, az = s.tz * 0.5f * kPierLength;
            const float bx = rx[(size_t)chosen] * pw, bz = rz[(size_t)chosen] * pw;
            const std::array<XZ, 4> c = {XZ{s.x - ax - bx, s.z - az - bz},
                                         XZ{s.x + ax - bx, s.z + az - bz},
                                         XZ{s.x + ax + bx, s.z + az + bz},
                                         XZ{s.x - ax + bx, s.z - az + bz}};
            const float top = s.y - kDeckDepth + 0.1f;
            box(c, {top, top, top, top});
            ++out.piers;
            // The pier as a wall across the span (what a car or walker under
            // the deck runs into).
            float lo = 1e30f;
            for (const XZ& q : c) lo = std::min(lo, bare(q.x, q.z));
            wallBoxes(0.5f * (c[0].x + c[1].x), 0.5f * (c[0].z + c[1].z),
                      0.5f * (c[3].x + c[2].x), 0.5f * (c[3].z + c[2].z), kPierLength,
                      lo - kSink, top, out.boxes);
        }
    }
}

std::function<float(float, float)> elevationFn(const SceneObject& o,
                                               const roadgen::HeightFn& ground) {
    auto deck = std::make_shared<Deck>(buildDeck(o, ground));
    // A bounding box first: the planner asks about points all over the scene.
    float mnx = 1e30f, mnz = 1e30f, mxx = -1e30f, mxz = -1e30f;
    for (const Station& s : deck->st) {
        mnx = std::min(mnx, s.x), mxx = std::max(mxx, s.x);
        mnz = std::min(mnz, s.z), mxz = std::max(mxz, s.z);
    }
    const float pad = deck->halfWidth + kParapetWidth + 1.0f;
    return [deck, mnx, mnz, mxx, mxz, pad](float x, float z) {
        if (x < mnx - pad || x > mxx + pad || z < mnz - pad || z > mxz + pad) return 0.0f;
        return deck->elevationAt(x, z);
    };
}

float heightOf(const SceneObject& o, int control) {
    return control >= 0 && control < (int)o.roadHeights.size() ? o.roadHeights[(size_t)control]
                                                                : 0.0f;
}

void onPointsReshaped(SceneObject& o) {
    if (!o.roadBridge) {
        o.roadHeights.clear();
        return;
    }
    o.roadHeights.resize((size_t)std::max(0, roadgen::controlCount(o.roadPoints)), 0.0f);
}

void onPointInserted(SceneObject& o, int at) {
    if (!o.roadBridge) {
        o.roadHeights.clear();
        return;
    }
    const int count = roadgen::controlCount(o.roadPoints);  // after the insert
    std::vector<float>& h = o.roadHeights;
    h.resize((size_t)std::max(0, count - 1), 0.0f);
    at = std::clamp(at, 0, (int)h.size());
    // Midway between its neighbours (or the end's own height past the end).
    const float before = at > 0 ? h[(size_t)at - 1] : (h.empty() ? 0.0f : h[0]);
    const float after = at < (int)h.size() ? h[(size_t)at] : before;
    h.insert(h.begin() + at, 0.5f * (before + after));
    h.resize((size_t)count, 0.0f);
}

void onPointRemoved(SceneObject& o, int at) {
    if (!o.roadBridge) {
        o.roadHeights.clear();
        return;
    }
    std::vector<float>& h = o.roadHeights;
    if (at >= 0 && at < (int)h.size()) h.erase(h.begin() + at);
    h.resize((size_t)std::max(0, roadgen::controlCount(o.roadPoints)), 0.0f);
}

// --- codegen ---------------------------------------------------------------

void chunkStructure(const Structure& s, std::vector<float>& verts, std::vector<ChunkRow>& rows) {
    std::map<std::pair<int, int>, std::vector<roadgen::KerbVertex>> cells;
    for (size_t i = 0; i + 2 < s.tris.size(); i += 3) {
        const float cx = (s.tris[i].x + s.tris[i + 1].x + s.tris[i + 2].x) / 3.0f;
        const float cz = (s.tris[i].z + s.tris[i + 1].z + s.tris[i + 2].z) / 3.0f;
        auto& v = cells[{(int)std::floor(cx / kCell), (int)std::floor(cz / kCell)}];
        v.insert(v.end(), s.tris.begin() + (long)i, s.tris.begin() + (long)i + 3);
    }
    for (const auto& [key, tris] : cells) {
        (void)key;
        for (size_t at = 0; at < tris.size(); at += (size_t)kChunkBudget) {
            const size_t count = std::min(tris.size() - at, (size_t)kChunkBudget);
            rows.push_back({(int)(verts.size() / 4), (int)count});
            for (size_t k = 0; k < count; ++k) {
                const roadgen::KerbVertex& v = tris[at + k];
                verts.insert(verts.end(), {v.x, v.y, v.z, v.shade});
            }
        }
    }
}

void chunkDeck(std::vector<roadgen::Vertex>& tris, std::vector<int>& rowSizes) {
    rowSizes.clear();
    for (size_t at = 0; at < tris.size(); at += (size_t)kChunkBudget) {
        const size_t count = std::min(tris.size() - at, (size_t)kChunkBudget);
        float lo = 1e30f;
        for (size_t k = 0; k < count; ++k) lo = std::min(lo, tris[at + k].v);
        const float base = std::floor(lo);
        for (size_t k = 0; k < count; ++k) tris[at + k].v -= base;
        rowSizes.push_back((int)count);
    }
}

std::string tablesSource(const std::vector<SceneChunk>& rows, const std::vector<float>& verts,
                         const std::string& notes, const std::vector<float>& boxes,
                         bool embedVerts) {
    std::ostringstream out;
    auto lit = [](float v) {
        char b[48];
        std::snprintf(b, sizeof(b), "%.7g", (double)v);
        std::string s = b;
        if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
        return s + "F";
    };
    out << "// Bridges (docs/roads.md \"Bridges\"): parapets, deck edges and underside, piers\n"
           "// and abutments, host-baked triangle lists, one row per cell chunk; x, y, z,\n"
           "// shade per vertex. Uploaded unchanged at scene load (the decks are\n"
           "// ROAD_JUNCTIONS rows).\n"
        << notes << "constexpr int ROAD_BRIDGE_COUNT = " << rows.size() << ";\n"
        << "struct RoadBridgeRt { int scene; int first; int count; };\n";
    // Collision (parapets, piers): scene, min xyz, max xyz, then the box's
    // own frame - half x, half z, yaw cos, yaw sin - per box.
    out << "constexpr int ROAD_BRIDGE_BOX_COUNT = " << boxes.size() / 11 << ";\n";
    if (boxes.empty()) {
        out << "constexpr float ROAD_BRIDGE_BOXES[1] = {};\n";
    } else {
        out << "constexpr float ROAD_BRIDGE_BOXES[" << boxes.size() << "] = {\n";
        for (size_t k = 0; k + 10 < boxes.size(); k += 11) {
            out << "   ";
            for (size_t j = 0; j < 11; ++j) out << " " << lit(boxes[k + j]) << ",";
            out << "\n";
        }
        out << "};\n";
    }
    if (rows.empty()) {
        out << "constexpr RoadBridgeRt ROAD_BRIDGES[1] = {};\n"
            << "constexpr float ROAD_BRIDGE_VERTS[1] = {};\n";
        return out.str();
    }
    out << "constexpr RoadBridgeRt ROAD_BRIDGES[" << rows.size() << "] = {\n";
    for (const SceneChunk& r : rows)
        out << "    {" << r.scene << ", " << r.first << ", " << r.count << "},\n";
    // Tables on disk (docs/roads.md "Tables on disk"): the vertices are in
    // bin/roadfile/roads.bin; the codegen's road file builder wrote them.
    if (!embedVerts) {
        out << "};\n// ROAD_BRIDGE_VERTS: in bin/roadfile/roads.bin.\n";
        return out.str();
    }
    out << "};\nconstexpr float ROAD_BRIDGE_VERTS[" << verts.size() << "] = {\n";
    for (size_t k = 0; k + 3 < verts.size(); k += 4)
        out << "    " << lit(verts[k]) << ", " << lit(verts[k + 1]) << ", " << lit(verts[k + 2])
            << ", " << lit(verts[k + 3]) << ",\n";
    out << "};\n";
    return out.str();
}

std::string uploadSource() {
    return R"(  // BRIDGES (docs/roads.md "Bridges"): parapets, deck edges and underside,
  // piers and abutments - host-baked triangle lists, one ROAD_BRIDGES row per
  // cell chunk, uploaded unchanged. The deck itself is a ROAD_JUNCTIONS row
  // (owner -3: the road height index and every wheel read it). Owner -5, not
  // -4: renderProcChunks draws these (frustum reject, occlusion) but the road
  // height index does not read them, so nothing stands on an underside or a
  // parapet top. Untextured: the baked shade is the vertex colour.
  for (size_t i = procChunks.size(); i > 0; --i)
    if (procChunks[i - 1].owner == -5)
      procChunks.erase(procChunks.begin() + (i - 1));
  {
    int bridgeChunks = 0, bridgeVertices = 0;
    for (int bi = 0; bi < ROAD_BRIDGE_COUNT; ++bi) {
      const RoadBridgeRt& br = ROAD_BRIDGES[bi];
      if (br.scene != scene || br.count < 3) continue;
      any = true;
      procChunks.push_back(ProcChunk());
      ProcChunk& c = procChunks.back();
      c.owner = -5;
      c.stripRun = 0;
      const float* base = &ROAD_BRIDGE_VERTS[(size_t)br.first * 4];
      for (int k = 0; k < br.count; ++k) {
        const float* v = base + (size_t)k * 4;
        c.vertices.push_back(Tyra::Vec4(v[0], v[1], v[2], 1.0F));
        const float g = v[3] * 128.0F;  // concrete, a hair warm
        c.colors.push_back(Tyra::Color(g, g * 0.99F, g * 0.96F, 128.0F));
      }
      ++bridgeChunks;
      bridgeVertices += br.count;
    }
    if (bridgeChunks > 0)
      TYRA_LOG("ROADBRIDGE scene ", scene, " chunks ", bridgeChunks, " vertices ",
               bridgeVertices);
    // The parapets and piers as walls (procColliders: the walker and every
    // car collide with them). Owner -5, like the drawn structure.
    for (int i = (int)procColliders.size() - 1; i >= 0; --i)
      if (procColliders[(size_t)i].owner == -5)
        procColliders.erase(procColliders.begin() + i);
    int bridgeBoxes = 0;
    for (int bi = 0; bi < ROAD_BRIDGE_BOX_COUNT; ++bi) {
      const float* b = &ROAD_BRIDGE_BOXES[(size_t)bi * 11];
      if ((int)b[0] != scene) continue;
      StaticBox sb;
      for (int a = 0; a < 3; ++a) {
        sb.mn[a] = b[1 + a];
        sb.mx[a] = b[4 + a];
      }
      sb.lhx = b[7];
      sb.lhz = b[8];
      sb.yc = b[9];
      sb.ys = b[10];
      sb.owner = -5;
      sb.instance = -1;
      procColliders.push_back(sb);
      ++bridgeBoxes;
    }
    if (bridgeBoxes > 0) TYRA_LOG("ROADBRIDGE scene ", scene, " walls ", bridgeBoxes);
  }
)";
}

// --- --vehicle-check --------------------------------------------------------

void check(void (*verdict)(bool, const char*)) {
    std::printf("-- road bridges --\n");
    const roadgen::HeightFn flat = [](float, float) { return 0.0f; };
    const float glued = roadgen::kLift;  // rank 1

    // 1. The profile: a raised middle point, ramps either side, no overshoot.
    {
        const Deck d = buildDeck({0, 0, 0, 50, 0, 100}, {0, 6, 0}, 8, 1, 1, flat);
        float peak = -1e30f, atMid = 0.0f, worstDip = 0.0f;
        bool monotone = true;
        for (size_t i = 0; i < d.st.size(); ++i) {
            peak = std::max(peak, d.st[i].y);
            if (std::fabs(d.st[i].z - 50.0f) < 0.6f) atMid = d.st[i].y;
            if (i > 0 && d.st[i].z <= 50.0f && d.st[i].y < d.st[i - 1].y - 1e-5f) monotone = false;
            worstDip = std::min(worstDip, d.st[i].y - glued);
        }
        std::vector<roadgen::Vertex> tris;
        tessellateDeck(d, tris);
        std::printf("  profile: deck %.3f at the raised point (peak %.3f), rising monotone %s, "
                    "lowest above the glued road %.3f, %zu deck vertices for 100 units\n",
                    atMid, peak, monotone ? "yes" : "NO", worstDip, tris.size());
        verdict(std::fabs(atMid - (glued + 6.0f)) < 0.05f && peak <= glued + 6.0f + 1e-4f &&
                    monotone && worstDip > -1e-4f,
                "the deck passes through terrain + height at a control point, no overshoot");
        verdict(tris.size() < 100 * 6 + 200,
                "an elevated deck is one quad per station pair (no lateral cells in the air)");
    }
    // 2. A valley spanned by itself: every height 0, the ground dips 5.
    {
        const roadgen::HeightFn valley = [](float, float z) {
            const float k = (z - 50.0f) / 18.0f;
            return -5.0f * std::exp(-k * k);
        };
        const Deck d = buildDeck({0, 0, 0, 100}, {}, 8, 1, 1, valley);
        float midDeck = 0.0f, nearest = 1e30f;
        for (const Station& s : d.st)
            if (std::fabs(s.z - 50.0f) < nearest) nearest = std::fabs(s.z - 50.0f), midDeck = s.y;
        const float elev = d.elevationAt(1.0f, 50.0f);
        std::printf("  valley: deck %.3f over ground %.3f, elevation %.3f\n", midDeck,
                    valley(0, 50), elev);
        verdict(std::fabs(midDeck - (valley(0, 0) + glued)) < 0.05f && elev > 4.5f,
                "an all-zero bridge spans a dip straight from point to point");
    }
    // 3. An overpass makes no node; the same road on the ground does.
    roadgen::CrossingRoad lower;
    lower.id = "lower";
    lower.points = {-50, 50, 50, 50};
    lower.width = 8;
    lower.intersection = "res/materials/x.mtl";
    lower.kerb = true;
    const std::vector<float> bridgePts = {0, 0, 0, 50, 0, 100};
    const Deck deck = buildDeck(bridgePts, {0, 6, 0}, 8, 1, 1, flat);
    roadgen::CrossingRoad bridge;
    bridge.id = "bridge";
    bridge.points = bridgePts;
    bridge.width = 8;
    bridge.intersection = "res/materials/x.mtl";
    {
        auto dp = std::make_shared<Deck>(deck);
        bridge.elevation = [dp](float x, float z) { return dp->elevationAt(x, z); };
    }
    roadgen::CrossingRoad onGround = bridge;
    onGround.elevation = nullptr;
    const std::vector<roadgen::CrossingRoad> over = {lower, bridge};
    const roadgen::CrossingPlan pOver = roadgen::planCrossings(over, {});
    const roadgen::CrossingPlan pGround = roadgen::planCrossings({lower, onGround}, {});
    std::printf("  overpass: %zu node(s) with the deck 6 up, %zu on the ground\n",
                pOver.crossings.size(), pGround.crossings.size());
    verdict(pOver.crossings.empty() && pGround.crossings.size() == 1,
            "a deck passing over a road is an overpass, not a junction");
    // 4. The lower road's kerbs are not cut by the deck overhead.
    {
        roadgen::Surface s;
        std::vector<roadgen::Vertex> tris;
        roadgen::tessellate(lower.points, lower.width, flat, tris);
        s.add(tris);
        s.build();
        const std::vector<roadgen::KerbPiece> k = roadgen::planKerbs(
            over, pOver, [&](float x, float z) { return s.at(x, z); }, flat);
        float hi = -1e30f;
        for (const roadgen::KerbPiece& p : k)
            for (int i = 0; i < p.points(); ++i) hi = std::max(hi, p.pts[(size_t)i * 5 + 1]);
        std::printf("  kerbs under the deck: %zu line(s), highest base %.3f\n", k.size(), hi);
        verdict(k.size() == 2 && hi < 0.5f, "the lower road keeps both kerbs, uncut, at its own height");
    }
    // 5. The lower road's wheel query stays on the lower road.
    {
        roadgen::Surface s;
        std::vector<roadgen::Vertex> tris;
        roadgen::tessellate(lower.points, lower.width, flat, tris);
        s.add(tris);
        tessellateDeck(deck, tris);
        s.add(tris);
        s.build();
        const float top = s.at(1.0f, 50.0f);
        const float wheel = s.at(1.0f, 50.0f, nullptr, nullptr, glued + kVehicleStepUp);
        const float onDeck = s.at(1.0f, 50.0f, nullptr, nullptr, top + kVehicleStepUp);
        std::printf("  under the deck: highest %.3f, a car on the lower road reads %.3f, one on "
                    "the deck %.3f\n", top, wheel, onDeck);
        verdict(std::fabs(wheel - glued) < 0.01f && std::fabs(onDeck - top) < 1e-4f &&
                    top > 5.0f,
                "a wheel under a bridge stays on its own road; one on the deck stays on the deck");
    }
    // 6. The structure: piers reach the ground and keep off the lower road.
    {
        Structure st;
        buildStructure(deck, over, 1, st);
        float lowest = 1e30f, onLower = 1e30f;
        for (const roadgen::KerbVertex& v : st.tris) {
            lowest = std::min(lowest, v.y);
            if (std::fabs(v.z - 50.0f) < 4.0f + 0.5f && std::fabs(v.x) < 4.5f)
                onLower = std::min(onLower, v.y);
        }
        std::printf("  structure: %d span(s), %d pier(s), %d abutment(s), %zu vertices, lowest "
                    "%.3f, lowest over the lower road %.3f\n",
                    st.spans, st.piers, st.abutments, st.tris.size(), lowest, onLower);
        verdict(st.piers >= 2 && st.abutments == 2 && lowest < -0.2f,
                "piers and abutments stand on (and into) the ground");
        verdict(onLower > 4.0f, "no pier stands on the road under the bridge");
        verdict(st.tris.size() < 2000, "a 100-unit bridge's structure stays under 2000 vertices");
    }
    // 7. Collision: a DIAGONAL bridge's parapets and piers as boxes that wall
    // the deck in without walling the deck itself.
    {
        const Deck dg = buildDeck({0, 0, 40, 40, 80, 80}, {0, 6, 0}, 8, 1, 1, flat);
        Structure st;
        buildStructure(dg, {}, -1, st);
        // How near each parapet box's own inner face comes to the centre
        // line x = z, in plan: |centre offset| - its half thickness.
        float nearest = 1e30f;
        int high = 0;
        bool alongWall = true;
        for (const CollisionBox& b : st.boxes) {
            // Only the parapets (boxes above the deck's lowest point) bound
            // the deck; a pier box crosses the centre line under it.
            if (!(b.mn[1] > 0.5f)) continue;
            ++high;
            const float cx = 0.5f * (b.mn[0] + b.mx[0]), cz = 0.5f * (b.mn[2] + b.mx[2]);
            nearest = std::min(nearest, std::fabs(cx - cz) * 0.70710678f - b.hx);
            // The frame runs along the 45-degree wall.
            alongWall &= std::fabs(std::fabs(b.ys) - 0.70710678f) < 0.01f &&
                         std::fabs(std::fabs(b.yc) - 0.70710678f) < 0.01f;
        }
        std::printf("  collision: %zu boxes (%d parapet pieces) on a 45-degree bridge, nearest "
                    "parapet box %.3f from the centre line (deck half-width 4)\n",
                    st.boxes.size(), high, nearest);
        verdict(high > 0 && alongWall && nearest > 4.0f - 0.05f,
                "oriented parapet boxes wall a diagonal deck in without reaching onto it");
        verdict((int)st.boxes.size() > high, "piers are walls too");
    }
}

}  // namespace roadbridge
