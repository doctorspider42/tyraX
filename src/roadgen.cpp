#include "roadgen.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "meshstrip.hpp"  // the run contract the strip emitter keeps

// The tessellator (docs/roads.md). TWIN NOTICE: the generated runtime carries
// this arithmetic as a raw string in templates.cpp (buildRoads) - change one
// and change both, the vehiclesim rule.
namespace roadgen {

namespace {

// Catmull-Rom through P1..P2 with neighbours P0/P3, standard 0.5 tension.
inline float cr(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

struct P {
    float x, z;
};

inline P pointAt(const std::vector<float>& pts, int i) {
    const int n = (int)(pts.size() / 2);
    if (i < 0) i = 0;
    if (i > n - 1) i = n - 1;  // clamped ends: the spline hits both endpoints
    return {pts[(size_t)i * 2], pts[(size_t)i * 2 + 1]};
}

inline P sample(const std::vector<float>& pts, int seg, float t) {
    const P p0 = pointAt(pts, seg - 1), p1 = pointAt(pts, seg);
    const P p2 = pointAt(pts, seg + 1), p3 = pointAt(pts, seg + 2);
    return {cr(p0.x, p1.x, p2.x, p3.x, t), cr(p0.z, p1.z, p2.z, p3.z, t)};
}

// Sample the whole spline: a terrain-projected row per station, the right
// vector from the local tangent, V from the running arc. The lateral
// subdivisions matter as much as the longitudinal ones: an edge-only strip is
// one plane across the full width and terrain can pierce it between the
// shoulders.
//
// SHARED BY BOTH EMITTERS. The list and the strip differ only in the order
// their vertices leave in, never in the surface, so the sampling - and with it
// every height query, the texture arc length and the lift - happens once here
// and neither emitter can drift from the other.
float buildRows(const std::vector<float>& pointsXZ, float width,
                const HeightFn& height,
                std::vector<std::vector<Vertex>>& rows, int* crossStepsOut) {
    rows.clear();
    *crossStepsOut = 1;
    const int n = (int)(pointsXZ.size() / 2);
    if (n < 2) return 0.0f;
    const float hw = 0.5f * (width > 0.1f ? width : 0.1f);

    const int crossSteps = std::max(
        1, (int)std::ceil((hw * 2.0f) / kCrossSampleStep));
    *crossStepsOut = crossSteps;
    float arc = 0.0f;
    P prev = sample(pointsXZ, 0, 0.0f);
    for (int seg = 0; seg < n - 1; ++seg) {
        const P a = pointAt(pointsXZ, seg), b = pointAt(pointsXZ, seg + 1);
        const float segLen = std::sqrt((b.x - a.x) * (b.x - a.x) +
                                       (b.z - a.z) * (b.z - a.z));
        const int steps =
            segLen > kSampleStep ? (int)(segLen / kSampleStep) + 1 : 1;
        for (int k = (seg == 0 ? 0 : 1); k <= steps; ++k) {
            const float t = (float)k / (float)steps;
            const P c = sample(pointsXZ, seg, t);
            // One-sided tangent at the two ends, forward elsewhere. Sampling
            // seg + 1 past the final segment clamps every control point to the
            // endpoint and produces a zero vector; the old world-axis fallback
            // then rotated the last road row abruptly and made a flared/twisted
            // end cap.
            P tangentFrom = c, tangentTo = c;
            if (t + 0.05f <= 1.0f || seg + 1 < n - 1) {
                tangentTo = t + 0.05f <= 1.0f
                                ? sample(pointsXZ, seg, t + 0.05f)
                                : sample(pointsXZ, seg + 1, 0.05f);
            } else {
                tangentFrom = sample(pointsXZ, seg, std::max(0.0f, t - 0.05f));
            }
            float tx = tangentTo.x - tangentFrom.x;
            float tz = tangentTo.z - tangentFrom.z;
            const float tl = std::sqrt(tx * tx + tz * tz);
            if (tl > 1e-6f) {
                tx /= tl;
                tz /= tl;
            } else {
                tx = 0.0f;
                tz = 1.0f;
            }
            // Right of travel: (tz, -tx) for +Y up.
            const float rx = tz * hw, rz = -tx * hw;
            arc += std::sqrt((c.x - prev.x) * (c.x - prev.x) +
                             (c.z - prev.z) * (c.z - prev.z));
            prev = c;
            const float v = arc / kTexLen;
            std::vector<Vertex> row;
            row.reserve((size_t)crossSteps + 1);
            for (int j = 0; j <= crossSteps; ++j) {
                const float u = (float)j / (float)crossSteps;
                const float side = u * 2.0f - 1.0f;
                Vertex q;
                q.x = c.x + rx * side;
                q.z = c.z + rz * side;
                q.y = (height ? height(q.x, q.z) : 0.0f) + kLift;
                q.u = u;
                q.v = v;
                row.push_back(q);
            }
            rows.push_back(std::move(row));
        }
    }
    return arc;
}

// The lateral step this station pair is stitched with: `crossSteps` for one
// full-width quad, 1 for the dense mesh. This is the exact planar-span
// reduction docs/roads.md describes and the oracle pins - it decides the
// SURFACE, so both emitters ask it rather than deciding for themselves.
int spanStride(const std::vector<std::vector<Vertex>>& rows, size_t i,
               int crossSteps) {
    // A full-width quad is exact when every dense sample lies in its
    // plane.  That includes level asphalt, but also roads over a sloped
    // terrain triangle.  Checking only the shoulders would turn a crown
    // or saddle into a plane, so test every sampled point against the
    // proposed quad's 3-D plane first.
    const Vertex& a = rows[i][0];
    const Vertex& b = rows[i][(size_t)crossSteps];
    const Vertex& d = rows[i + 1][0];
    const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const float vx = d.x - a.x, vy = d.y - a.y, vz = d.z - a.z;
    const float nx = uy * vz - uz * vy;
    const float ny = uz * vx - ux * vz;
    const float nz = ux * vy - uy * vx;
    const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    // Retain the established horizontal path, including curved flat
    // spans.  Its wide-triangle UV interpolation is the shipped look.
    bool flat = true;
    const float flatY = rows[i][0].y;
    for (int r = 0; r < 2; ++r)
        for (int j = 0; j <= crossSteps; ++j)
            if (std::fabs(rows[i + (size_t)r][(size_t)j].y - flatY) >
                0.00001f) { flat = false; break; }
    // The two triangles interpolate ST like an affine parallelogram. A
    // curved station pair can be coplanar but still map U/V differently
    // from its dense lateral cells, so it is deliberately left dense.
    const Vertex& c = rows[i + 1][(size_t)crossSteps];
    const float ax = (b.x - a.x) - (c.x - d.x);
    const float ay = (b.y - a.y) - (c.y - d.y);
    const float az = (b.z - a.z) - (c.z - d.z);
    bool planar = nl > 1e-6f &&
                  std::sqrt(ax * ax + ay * ay + az * az) <= 0.00001f;
    for (int r = 0; planar && r < 2; ++r)
        for (int j = 0; j <= crossSteps; ++j) {
            const Vertex& q = rows[i + (size_t)r][(size_t)j];
            const float dist = std::fabs(nx * (q.x - a.x) +
                                         ny * (q.y - a.y) +
                                         nz * (q.z - a.z)) / nl;
            if (dist > 0.00001f) { planar = false; break; }
        }
    const int stride = (flat || planar) ? crossSteps : 1;
    return stride;
}

}  // namespace

float tessellate(const std::vector<float>& pointsXZ, float width,
                 const HeightFn& height, std::vector<Vertex>& out,
                 const std::vector<float>& lifts) {
    (void)lifts;  // legacy project field; roads are always terrain-projected
    out.clear();
    std::vector<std::vector<Vertex>> rows;
    int crossSteps = 1;
    const float arc = buildRows(pointsXZ, width, height, rows, &crossSteps);

    // Stitch every lateral cell. Wound counter-clockwise seen from above
    // (+Y), the terrain's own convention.
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        const int stride = spanStride(rows, i, crossSteps);
        for (int j = 0; j < crossSteps; j += stride) {
            out.push_back(rows[i][(size_t)j]);
            out.push_back(rows[i][(size_t)j + stride]);
            out.push_back(rows[i + 1][(size_t)j + stride]);
            out.push_back(rows[i][(size_t)j]);
            out.push_back(rows[i + 1][(size_t)j + stride]);
            out.push_back(rows[i + 1][(size_t)j]);
        }
    }
    return arc;
}

// The strip emitter. TWIN NOTICE: buildRoads in templates.cpp carries this
// packing verbatim, chunk policy included - change one and change both.
float tessellateStrips(const std::vector<float>& pointsXZ, float width,
                       const HeightFn& height, std::vector<Vertex>& out,
                       std::vector<int>* chunkSizes) {
    static_assert(kStripRun == (int)meshstrip::kRun,
                  "the road run must be the run the .tmdl bake uses - both are "
                  "the smallest package a static program class derives");
    out.clear();
    if (chunkSizes != nullptr) chunkSizes->clear();
    std::vector<std::vector<Vertex>> rows;
    int crossSteps = 1;
    const float arc = buildRows(pointsXZ, width, height, rows, &crossSteps);

    // Run and chunk bookkeeping. A chunk is a bag; a run is one VU1 package
    // inside it, so the LAST run of a chunk owes only the multiple of 3 the
    // vertex loops need while every other run is exactly kStripRun - end one
    // short and StaPipCore's next slice starts mid-strip and fuses two
    // unrelated vertices into one triangle.
    size_t chunkStart = 0, runStart = 0;
    int spansInChunk = 0;
    bool open = false;
    auto runLen = [&]() { return out.size() - runStart; };
    // One vertex into the open run. A run that fills MID-STRIP carries the
    // two-vertex overlap into the next one, or the triangle across the cut is
    // lost - meshstrip's rule, and the only place a run ever ends early.
    auto pushRaw = [&](const Vertex& v) {
        if (runLen() == (size_t)kStripRun) {
            const Vertex a = out[out.size() - 2];
            const Vertex b = out[out.size() - 1];
            runStart = out.size();
            out.push_back(a);
            out.push_back(b);
        }
        out.push_back(v);
    };
    // Begin an unrelated strip in the same run: repeat the run's last vertex
    // and the incoming strip's first. Four zero-area triangles, and the fifth
    // is the incoming strip's own first real one.
    auto startStrip = [&](const Vertex& v) {
        if (runLen() > 0) {
            const Vertex last = out.back();
            pushRaw(last);
            pushRaw(v);
        }
        pushRaw(v);
    };
    auto closeChunk = [&]() {
        if (!open) return;
        const size_t target = ((runLen() + 2) / 3) * 3;
        while (runLen() < target) out.push_back(out.back());
        if (chunkSizes != nullptr)
            chunkSizes->push_back((int)(out.size() - chunkStart));
        chunkStart = runStart = out.size();
        spansInChunk = 0;
        open = false;
    };

    // A collapsed span is ONE full-width quad, so a road of them is a grid one
    // cell WIDE and many stations LONG - and a strip has to run along the long
    // axis or it buys nothing. Taken laterally, a collapsed span is 4 vertices
    // plus a 2-vertex join against the list's 6: exactly break-even on the EE
    // and 3x the GS primitives, two thirds of them degenerate. Taken
    // longitudinally it is 2 vertices per STATION, the same 0.345x the dense
    // spans reach. So the emitter follows the grid: dense spans strip ACROSS
    // the road, consecutive collapsed spans strip ALONG it.
    bool alongOpen = false;
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        const int stride = spanStride(rows, i, crossSteps);
        const bool collapsed = stride == crossSteps;
        const size_t cost =
            collapsed ? (alongOpen ? 2u : 4u)
                      : (size_t)(2 * (crossSteps / stride + 1) + 2);
        if (!open || spansInChunk >= kChunkSpans ||
            (out.size() - chunkStart) + cost > (size_t)kChunkBudget) {
            closeChunk();
            alongOpen = false;
            open = true;
        }
        if (collapsed) {
            // ... P[w], P[0], N[w], N[0] ... - successive triples are this
            // span's two triangles, cut along P[0]-N[w], which is the cut the
            // list stitch makes. The other interleaving takes the other
            // diagonal and silently reshapes every non-planar quad.
            if (!alongOpen) {
                startStrip(rows[i][(size_t)crossSteps]);
                pushRaw(rows[i][0]);
                alongOpen = true;
            }
            pushRaw(rows[i + 1][(size_t)crossSteps]);
            pushRaw(rows[i + 1][0]);
        } else {
            // N[0], P[0], N[s], P[s], ... - same argument, same diagonal
            // P[j]-N[j+s], walked across the road instead.
            alongOpen = false;
            startStrip(rows[i + 1][0]);
            pushRaw(rows[i][0]);
            for (int j = stride; j <= crossSteps; j += stride) {
                pushRaw(rows[i + 1][(size_t)j]);
                pushRaw(rows[i][(size_t)j]);
            }
        }
        ++spansInChunk;
    }
    closeChunk();
    return arc;
}

void splineAt(const std::vector<float>& pointsXZ, float t, float* x, float* z) {
    const int n = (int)(pointsXZ.size() / 2);
    if (n < 1) {
        *x = 0.0f;
        *z = 0.0f;
        return;
    }
    if (n == 1 || t <= 0.0f) {
        const P p = pointAt(pointsXZ, t <= 0.0f ? 0 : 0);
        *x = p.x;
        *z = p.z;
        if (n == 1) return;
    }
    if (t >= 1.0f) {
        const P p = pointAt(pointsXZ, n - 1);
        *x = p.x;
        *z = p.z;
        return;
    }
    const float ft = t * (float)(n - 1);
    const int seg = (int)ft;
    const P p = sample(pointsXZ, seg, ft - (float)seg);
    *x = p.x;
    *z = p.z;
}

}  // namespace roadgen
