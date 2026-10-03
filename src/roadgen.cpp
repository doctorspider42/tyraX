#include "roadgen.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <cmath>
#include <limits>
#include <map>
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
    if (isClosed(pts)) {
        i = (i % (n - 1) + n - 1) % (n - 1);
    } else {
        if (i < 0) i = 0;
        if (i > n - 1) i = n - 1;
    }
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
                const HeightFn& height, float sampleStep,
                std::vector<std::vector<Vertex>>& rows, int* crossStepsOut,
                float uInset = 0.0f) {
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
        const float spacing = std::clamp(sampleStep, 1.0f, 2.0f);
        const int steps = segLen > spacing ? (int)(segLen / spacing) + 1 : 1;
        for (int k = (seg == 0 ? 0 : 1); k <= steps; ++k) {
            const float t = (float)k / (float)steps;
            const P c = sample(pointsXZ, seg, t);
            // One-sided tangent at the two ends, forward elsewhere. Sampling
            // seg + 1 past the final segment clamps every control point to the
            // endpoint and produces a zero vector; the old world-axis fallback
            // then rotated the last road row abruptly and made a flared/twisted
            // end cap.
            P tangentFrom = c, tangentTo = c;
            if (isClosed(pointsXZ) && seg == n - 2 && t == 1.0f) {
                tangentTo = sample(pointsXZ, 0, 0.05f);
            } else if (t + 0.05f <= 1.0f || seg + 1 < n - 1) {
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
            // Geometry may use a coarser authored spacing, but texture V is
            // always integrated at the original one-unit cadence. Changing
            // road detail must not make lane markings slide along the street.
            const float prevT = k > 0 ? (float)(k - 1) / (float)steps : 0.0f;
            const int arcSteps = k > 0
                                     ? std::max(1, (int)std::ceil(
                                                       (t - prevT) * segLen /
                                                       kArcSampleStep))
                                     : 0;
            for (int ak = 1; ak <= arcSteps; ++ak) {
                const float at = prevT + (t - prevT) *
                                             ((float)ak / (float)arcSteps);
                const P ap = sample(pointsXZ, seg, at);
                arc += std::sqrt((ap.x - prev.x) * (ap.x - prev.x) +
                                 (ap.z - prev.z) * (ap.z - prev.z));
                prev = ap;
            }
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
                q.u = uInset + (1.0f - 2.0f * uInset) * u;
                q.v = v;
                row.push_back(q);
            }
            rows.push_back(std::move(row));
        }
    }
    return arc;
}

// Is the quad rows[i..i+1][j0..j1] EXACTLY the dense mesh it would replace?
// Two conditions, both of them the ones the full-width test has always used,
// only asked of a sub-span:
//   - every dense sample of both rows lies in the quad's 3-D plane, so the
//     surface is unchanged (a crown, a crest or a saddle fails this and stays
//     dense), and
//   - the quad is an affine parallelogram, so its two triangles interpolate
//     ST exactly as the lateral cells it replaces do. `u` is j / crossSteps
//     and a row's samples are uniformly spaced along it, so a parallelogram's
//     linear interpolation reproduces every interior sample's U as well.
bool spanIsExact(const std::vector<std::vector<Vertex>>& rows, size_t i,
                 int j0, int j1) {
    const Vertex& a = rows[i][(size_t)j0];
    const Vertex& b = rows[i][(size_t)j1];
    const Vertex& d = rows[i + 1][(size_t)j0];
    const Vertex& c = rows[i + 1][(size_t)j1];
    const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const float vx = d.x - a.x, vy = d.y - a.y, vz = d.z - a.z;
    const float nx = uy * vz - uz * vy;
    const float ny = uz * vx - ux * vz;
    const float nz = ux * vy - uy * vx;
    const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!(nl > 1e-6f)) return false;
    const float ax = (b.x - a.x) - (c.x - d.x);
    const float ay = (b.y - a.y) - (c.y - d.y);
    const float az = (b.z - a.z) - (c.z - d.z);
    if (std::sqrt(ax * ax + ay * ay + az * az) > kSpanShear) return false;
    for (int r = 0; r < 2; ++r)
        for (int j = j0; j <= j1; ++j) {
            const Vertex& q = rows[i + (size_t)r][(size_t)j];
            const float dist = std::fabs(nx * (q.x - a.x) + ny * (q.y - a.y) +
                                         nz * (q.z - a.z)) / nl;
            if (dist > kSpanFlatness) return false;
        }
    return true;
}

// Where this station pair is CUT laterally: always 0 and crossSteps, plus
// every interior boundary the exactness test above refuses to cross. This is
// the exact planar-span reduction docs/roads.md describes and the oracle pins
// - it decides the SURFACE, so both emitters ask it rather than deciding for
// themselves.
//
// It used to be all-or-nothing: one full-width quad, or all crossSteps dense
// cells. That is the wrong shape for a road that is a DECAL on a heightfield.
// The district's terrain cell is 4 world units and the road samples across at
// 0.5, so a 13-unit street is 26 lateral cells laid over three or four terrain
// triangles: the full width is almost never one plane, and every one of the
// 26 cells was therefore retained even though runs of eight of them sit inside
// a single terrain triangle and are exactly coplanar. Merging maximal runs
// instead keeps the same surface to the same tolerance and is what takes the
// district's roads from 31 050 triangles to a third of that.
void spanCuts(const std::vector<std::vector<Vertex>>& rows, size_t i,
              int crossSteps, std::vector<int>& cuts) {
    cuts.clear();
    // Retain the established horizontal path, including curved flat spans.
    // Its wide-triangle UV interpolation is the shipped look, and it is the
    // one case that does NOT owe the affine check.
    bool flat = true;
    const float flatY = rows[i][0].y;
    for (int r = 0; r < 2 && flat; ++r)
        for (int j = 0; j <= crossSteps; ++j)
            if (std::fabs(rows[i + (size_t)r][(size_t)j].y - flatY) >
                0.00001f) { flat = false; break; }
    cuts.push_back(0);
    if (flat) {
        cuts.push_back(crossSteps);
        return;
    }
    // Greedy maximal runs, extended one cell at a time. A single cell is the
    // fallback and is never tested, so this can only ever remove vertices
    // from what the dense mesh would have emitted - the previous behaviour is
    // its lower bound, and the full-width collapse its upper one.
    int j0 = 0;
    while (j0 < crossSteps) {
        int j1 = j0 + 1;
        while (j1 < crossSteps && spanIsExact(rows, i, j0, j1 + 1)) ++j1;
        cuts.push_back(j1);
        j0 = j1;
    }
}

std::vector<P> centreLine(const std::vector<float>& pts) {
    std::vector<P> out;
    const int n = (int)(pts.size() / 2);
    if (n < 2) return out;
    for (int seg = 0; seg < n - 1; ++seg) {
        const P a = pointAt(pts, seg), b = pointAt(pts, seg + 1);
        const float len = std::hypot(b.x - a.x, b.z - a.z);
        const int steps = len > kSampleStep ? (int)(len / kSampleStep) + 1 : 1;
        for (int k = seg == 0 ? 0 : 1; k <= steps; ++k)
            out.push_back(sample(pts, seg, (float)k / (float)steps));
    }
    return out;
}

}  // namespace

float tessellate(const std::vector<float>& pointsXZ, float width,
                 const HeightFn& height, std::vector<Vertex>& out,
                 const std::vector<float>& lifts, float sampleStep, float uInset) {
    (void)lifts;  // legacy project field; roads are always terrain-projected
    out.clear();
    std::vector<std::vector<Vertex>> rows;
    int crossSteps = 1;
    const float arc =
        buildRows(pointsXZ, width, height, sampleStep, rows, &crossSteps, uInset);

    // Stitch every lateral cell. Wound counter-clockwise seen from above
    // (+Y), the terrain's own convention.
    std::vector<int> cuts;
    int spansInChunk = 0;
    size_t verticesInChunk = 0;
    float chunkVBase = 0.0f;
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        spanCuts(rows, i, crossSteps, cuts);
        const size_t cost = (cuts.size() - 1) * 6;
        if (spansInChunk == 0 || spansInChunk >= kChunkSpans ||
            rows[i + 1][0].v - chunkVBase >= kChunkTexRange ||
            verticesInChunk + cost > (size_t)kChunkBudget) {
            // The GS repeats the texture every integer V. Rebase each runtime
            // bag to a nearby integer so a long road never feeds a large ST
            // coordinate into the physical PS2's fixed-precision path.
            chunkVBase = std::floor(rows[i][0].v);
            spansInChunk = 0;
            verticesInChunk = 0;
        }
        auto local = [&](Vertex v) {
            v.v -= chunkVBase;
            return v;
        };
        for (size_t k = 0; k + 1 < cuts.size(); ++k) {
            const int j = cuts[k], j2 = cuts[k + 1];
            out.push_back(local(rows[i][(size_t)j]));
            out.push_back(local(rows[i][(size_t)j2]));
            out.push_back(local(rows[i + 1][(size_t)j2]));
            out.push_back(local(rows[i][(size_t)j]));
            out.push_back(local(rows[i + 1][(size_t)j2]));
            out.push_back(local(rows[i + 1][(size_t)j]));
        }
        ++spansInChunk;
        verticesInChunk += cost;
    }
    return arc;
}

// The strip emitter. TWIN NOTICE: buildRoads in templates.cpp carries this
// packing verbatim, chunk policy included - change one and change both.
float tessellateStrips(const std::vector<float>& pointsXZ, float width,
                       const HeightFn& height, std::vector<Vertex>& out,
                       std::vector<int>* chunkSizes, float sampleStep,
                       float uInset) {
    static_assert(kStripRun == (int)meshstrip::kRun,
                  "the road run must be the run the .tmdl bake uses - both are "
                  "the smallest package a static program class derives");
    out.clear();
    if (chunkSizes != nullptr) chunkSizes->clear();
    std::vector<std::vector<Vertex>> rows;
    int crossSteps = 1;
    const float arc =
        buildRows(pointsXZ, width, height, sampleStep, rows, &crossSteps, uInset);

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
    float chunkVBase = 0.0f;
    auto local = [&](Vertex v) {
        v.v -= chunkVBase;
        return v;
    };
    std::vector<int> cuts;
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        spanCuts(rows, i, crossSteps, cuts);
        const bool collapsed = cuts.size() == 2;
        const size_t cost =
            collapsed ? (alongOpen ? 2u : 4u) : (2 * cuts.size() + 2);
        if (!open || spansInChunk >= kChunkSpans ||
            rows[i + 1][0].v - chunkVBase >= kChunkTexRange ||
            (out.size() - chunkStart) + cost > (size_t)kChunkBudget) {
            closeChunk();
            alongOpen = false;
            open = true;
            chunkVBase = std::floor(rows[i][0].v);
        }
        if (collapsed) {
            // ... P[w], P[0], N[w], N[0] ... - successive triples are this
            // span's two triangles, cut along P[0]-N[w], which is the cut the
            // list stitch makes. The other interleaving takes the other
            // diagonal and silently reshapes every non-planar quad.
            if (!alongOpen) {
                startStrip(local(rows[i][(size_t)crossSteps]));
                pushRaw(local(rows[i][0]));
                alongOpen = true;
            }
            pushRaw(local(rows[i + 1][(size_t)crossSteps]));
            pushRaw(local(rows[i + 1][0]));
        } else {
            // N[0], P[0], N[s], P[s], ... - same argument, same diagonal
            // P[j]-N[j+s], walked across the road instead. The cuts are no
            // longer uniformly spaced, which changes nothing here: the walk
            // visits them in order and the diagonal is the same one.
            alongOpen = false;
            startStrip(local(rows[i + 1][0]));
            pushRaw(local(rows[i][0]));
            for (size_t k = 1; k < cuts.size(); ++k) {
                pushRaw(local(rows[i + 1][(size_t)cuts[k]]));
                pushRaw(local(rows[i][(size_t)cuts[k]]));
            }
        }
        ++spansInChunk;
    }
    closeChunk();
    return arc;
}

void findJunctions(const std::vector<float>& aPoints, float aWidth,
                   const std::vector<float>& bPoints, float bWidth,
                   std::vector<Junction>& out) {
    out.clear();
    const std::vector<P> a = centreLine(aPoints), b = centreLine(bPoints);
    const float wa = std::max(0.1f, aWidth) * 0.5f + 0.15f;
    const float wb = std::max(0.1f, bWidth) * 0.5f + 0.15f;
    for (size_t ai = 0; ai + 1 < a.size(); ++ai) {
        const float adx = a[ai + 1].x - a[ai].x;
        const float adz = a[ai + 1].z - a[ai].z;
        const float al = std::hypot(adx, adz);
        if (!(al > 1e-5f)) continue;
        const float dax = adx / al, daz = adz / al;
        for (size_t bi = 0; bi + 1 < b.size(); ++bi) {
            const float bdx = b[bi + 1].x - b[bi].x;
            const float bdz = b[bi + 1].z - b[bi].z;
            const float bl = std::hypot(bdx, bdz);
            if (!(bl > 1e-5f)) continue;
            const float dbx = bdx / bl, dbz = bdz / bl;
            const float den = adx * bdz - adz * bdx;
            // Below ~14 degrees the overlap is a huge needle and is better
            // authored as a merge than disguised as an automatic crossing.
            if (std::fabs(dax * dbz - daz * dbx) < 0.25f) continue;
            const float qx = b[bi].x - a[ai].x;
            const float qz = b[bi].z - a[ai].z;
            const float ta = (qx * bdz - qz * bdx) / den;
            const float tb = (qx * adz - qz * adx) / den;
            if (ta < -1e-4f || ta > 1.0001f || tb < -1e-4f || tb > 1.0001f)
                continue;
            Junction j;
            j.x = a[ai].x + adx * ta;
            j.z = a[ai].z + adz * ta;
            const float mergeRadius = 0.5f * std::min(aWidth, bWidth);
            bool duplicate = false;
            for (const Junction& old : out)
                duplicate |= std::hypot(old.x - j.x, old.z - j.z) < mergeRadius;
            if (duplicate) continue;

            // Intersection of the two infinite width strips. Their right
            // normals are the two linear equations; four sign combinations
            // produce the convex parallelogram around the centre.
            const float nax = daz, naz = -dax;
            const float nbx = dbz, nbz = -dbx;
            const float nd = nax * nbz - naz * nbx;
            struct Corner { float x, z, angle; } c[4];
            int ci = 0;
            for (int sa : {-1, 1}) for (int sb : {-1, 1}) {
                const float ca = (float)sa * wa, cb = (float)sb * wb;
                const float ox = (ca * nbz - naz * cb) / nd;
                const float oz = (nax * cb - ca * nbx) / nd;
                c[ci++] = {j.x + ox, j.z + oz, std::atan2(oz, ox)};
            }
            std::sort(c, c + 4,
                      [](const Corner& l, const Corner& r) { return l.angle < r.angle; });
            for (int k = 0; k < 4; ++k) {
                j.cornerXZ[k * 2] = c[k].x;
                j.cornerXZ[k * 2 + 1] = c[k].z;
            }
            out.push_back(j);
        }
    }
}

void tessellateJunction(const Junction& j, const HeightFn& height,
                        std::vector<Vertex>& out) {
    const float cy = (height ? height(j.x, j.z) : 0.0f) + kLift + 0.02f;
    Vertex center{j.x, cy, j.z, 0.5f, 0.5f};
    std::vector<float> ring = j.outline;
    if (ring.size() < 6) ring.assign(j.cornerXZ, j.cornerXZ + 8);
    const size_t corners = ring.size() / 2;
    for (size_t k = 0; k < corners; ++k) {
        const size_t n = (k + 1) % corners;
        const float ax = ring[k * 2], az = ring[k * 2 + 1];
        const float bx = ring[n * 2], bz = ring[n * 2 + 1];
        const Vertex a{ax, (height ? height(ax, az) : 0.0f) + kLift + 0.02f,
                       az, 0.5f + (ax - j.x) / 32.0f,
                       0.5f + (az - j.z) / 32.0f};
        const Vertex b{bx, (height ? height(bx, bz) : 0.0f) + kLift + 0.02f,
                       bz, 0.5f + (bx - j.x) / 32.0f,
                       0.5f + (bz - j.z) / 32.0f};
        out.push_back(center); out.push_back(a); out.push_back(b);
    }
}

float terrainHeight(const std::vector<float>& heights, int columns, int rows,
                    float width, float depth, float x, float z) {
    if (columns < 2 || rows < 2 || width <= 0 || depth <= 0 ||
        heights.size() < (size_t)columns * rows) return 0.0f;
    const float gx = std::clamp((x + width * 0.5f) / width * (columns - 1),
                              0.0f, columns - 1.001f);
    const float gz = std::clamp((z + depth * 0.5f) / depth * (rows - 1),
                              0.0f, rows - 1.001f);
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - ix, fz = gz - iz;
    auto h = [&](int a, int b) { return heights[(size_t)b * columns + a]; };
    if (fx + fz <= 1.0f)
        return h(ix, iz) + fx * (h(ix + 1, iz) - h(ix, iz)) +
               fz * (h(ix, iz + 1) - h(ix, iz));
    return h(ix + 1, iz + 1) + (1 - fz) * (h(ix + 1, iz) - h(ix + 1, iz + 1)) +
           (1 - fx) * (h(ix, iz + 1) - h(ix + 1, iz + 1));
}

namespace {

struct DQ {
    double x, z;
};

double crossDQ(DQ o, DQ a, DQ b) {
    return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
}

// Ear clipping of a counter-clockwise simple polygon: triangles appended to
// `tris` as corner triples, counter-clockwise, zero-area ones dropped. A
// polygon that will not clip (degenerate leftovers) fans what remains.
void earClip(std::vector<DQ> p, std::vector<DQ>& tris) {
    auto emit = [&](DQ a, DQ b, DQ c) {
        // Slivers under 1e-6 square units are dropped: they cover nothing.
        if (crossDQ(a, b, c) > 2e-6) tris.insert(tris.end(), {a, b, c});
    };
    while (p.size() > 3) {
        bool clipped = false;
        for (size_t k = 0; k < p.size() && !clipped; ++k) {
            const size_t ka = (k + p.size() - 1) % p.size(), kc = (k + 1) % p.size();
            const DQ a = p[ka], b = p[k], c = p[kc];
            if (crossDQ(a, b, c) <= 1e-9) continue;
            bool empty = true;
            for (size_t m = 0; m < p.size() && empty; ++m) {
                if (m == k || m == ka || m == kc) continue;
                empty = !(crossDQ(a, b, p[m]) > 0 && crossDQ(b, c, p[m]) > 0 &&
                          crossDQ(c, a, p[m]) > 0);
            }
            if (!empty) continue;
            emit(a, b, c);
            p.erase(p.begin() + (long)k);
            clipped = true;
        }
        if (!clipped) break;
    }
    for (size_t k = 1; k + 1 < p.size(); ++k) emit(p[0], p[k], p[k + 1]);
}

}  // namespace

#ifndef TYRA_JUNCTION_TOL
#define TYRA_JUNCTION_TOL 0.01f
#endif
constexpr float kJunctionTol = TYRA_JUNCTION_TOL;

TerrainGrid terrainGridOf(int columns, int rows, float width, float depth) {
    TerrainGrid g;
    if (columns < 2 || rows < 2 || width <= 0 || depth <= 0) return g;
    g.x0 = -0.5f * width;
    g.z0 = -0.5f * depth;
    g.dx = width / (float)(columns - 1);
    g.dz = depth / (float)(rows - 1);
    return g;
}

void tessellateJunctionSurface(const Junction& j,
    const std::vector<Vertex>& roads, const HeightFn& terrain, float lift,
    std::vector<Vertex>& out, const TerrainGrid& grid) {
    // The footprint ring: the node outline, or the legacy four corners.
    std::vector<float> ring = j.outline;
    if (ring.size() < 6) ring.assign(j.cornerXZ, j.cornerXZ + 8);
    const size_t corners = ring.size() / 2;
    // Only triangles near this junction participate in the host-side proof.
    float minX = j.x, maxX = j.x, minZ = j.z, maxZ = j.z;
    for (size_t k = 0; k < corners; ++k) {
        minX = std::min(minX, ring[k * 2]);
        maxX = std::max(maxX, ring[k * 2]);
        minZ = std::min(minZ, ring[k * 2 + 1]);
        maxZ = std::max(maxZ, ring[k * 2 + 1]);
    }
    std::vector<Vertex> local;
    for (size_t i = 0; i + 2 < roads.size(); i += 3) {
        const Vertex* t = &roads[i];
        if (std::max({t[0].x, t[1].x, t[2].x}) < minX ||
            std::min({t[0].x, t[1].x, t[2].x}) > maxX ||
            std::max({t[0].z, t[1].z, t[2].z}) < minZ ||
            std::min({t[0].z, t[1].z, t[2].z}) > maxZ) continue;
        local.insert(local.end(), t, t + 3);
    }
    // A node outline reaches past the roads (its fillets lie on bare ground),
    // so the ground itself joins the proof as a fine grid of triangles at
    // the height a road would have there: the patch then clears the terrain
    // between its vertices as well as at them.
    // The grid is the terrain's own (its two triangles per cell, the split
    // terrainHeight uses), so these triangles ARE the ground, not a resampling
    // of it. Without a known grid a 2-unit one stands in.
    TerrainGrid g = grid;
    if (!(g.dx > 0.0f && g.dz > 0.0f)) g = TerrainGrid{0.0f, 0.0f, 2.0f, 2.0f};
    int ix0 = (int)std::floor((minX - g.x0) / g.dx), ix1 = (int)std::ceil((maxX - g.x0) / g.dx);
    int iz0 = (int)std::floor((minZ - g.z0) / g.dz), iz1 = (int)std::ceil((maxZ - g.z0) / g.dz);
    // A huge node on a fine grid would make the proof quadratic; coarsen.
    const int stride = std::max(1, std::max(ix1 - ix0, iz1 - iz0) / 64);
    std::vector<std::array<Vertex, 3>> cells;  // the ground triangles, CCW
    if (j.outline.size() >= 6) {
        auto node = [&](int ix, int iz) {
            const float x = g.x0 + g.dx * (float)ix, z = g.z0 + g.dz * (float)iz;
            return Vertex{x, (terrain ? terrain(x, z) : 0.0f) + kLift + lift, z, 0.0f, 0.0f};
        };
        for (int iz = iz0; iz < iz1; iz += stride)
            for (int ix = ix0; ix < ix1; ix += stride) {
                const Vertex a = node(ix, iz), b = node(ix + stride, iz);
                const Vertex c = node(ix + stride, iz + stride), d = node(ix, iz + stride);
                cells.push_back({a, b, d});
                cells.push_back({b, c, d});
            }
        if (terrain)
            for (const auto& t : cells) local.insert(local.end(), t.begin(), t.end());
    }
    auto plane =[](const Vertex* t, double x, double z, double* a = nullptr,
                    double* b = nullptr) {
        const double den = (double)(t[1].z - t[2].z) * (t[0].x - t[2].x) +
                           (double)(t[2].x - t[1].x) * (t[0].z - t[2].z);
        if (std::abs(den) < 1e-12) return -1e30;
        const double u = ((t[1].z - t[2].z) * (x - t[2].x) +
                          (t[2].x - t[1].x) * (z - t[2].z)) / den;
        const double v = ((t[2].z - t[0].z) * (x - t[2].x) +
                          (t[0].x - t[2].x) * (z - t[2].z)) / den;
        if (a) *a = u;
        if (b) *b = v;
        return u * t[0].y + v * t[1].y + (1 - u - v) * t[2].y;
    };
    auto vertex = [&](float x, float z) {
        float y = (terrain ? terrain(x, z) : 0.0f) + kLift + lift;
        for (size_t i = 0; i + 2 < local.size(); i += 3) {
            double a = -1, b = -1;
            const double h = plane(&local[i], x, z, &a, &b);
            if (a >= -1e-6 && b >= -1e-6 && a + b <= 1.000001)
                y = std::max(y, (float)h);
        }
        return Vertex{x, y + kSpillLift, z, 0.5f + (x - j.x) / 32.0f,
                      0.5f + (z - j.z) / 32.0f};
    };
    out.clear();
    // A fan from the centre when every outline step turns counter-clockwise
    // about it (the usual node, and the legacy four corners); otherwise an arm
    // on a bend has curled the ring around the centre and it is ear-clipped.
    bool star = true;
    for (size_t k = 0; k < corners && star; ++k) {
        const size_t n = (k + 1) % corners;
        star = (ring[k * 2] - j.x) * (ring[n * 2 + 1] - j.z) -
                   (ring[k * 2 + 1] - j.z) * (ring[n * 2] - j.x) > 1e-6f;
    }
    if (star) {
        const Vertex center = vertex(j.x, j.z);
        for (size_t k = 0; k < corners; ++k) {
            const size_t n = (k + 1) % corners;
            out.insert(out.end(), {center, vertex(ring[k * 2], ring[k * 2 + 1]),
                                  vertex(ring[n * 2], ring[n * 2 + 1])});
        }
    } else {
        std::vector<DQ> poly, tris;
        for (size_t k = 0; k < corners; ++k) poly.push_back({ring[k * 2], ring[k * 2 + 1]});
        earClip(poly, tris);
        for (const DQ& q : tris) out.push_back(vertex((float)q.x, (float)q.z));
    }
    if (out.empty()) return;
    struct Q { double x, z; };
    // The difference of two triangle planes is affine. Its maximum over
    // their overlap is at an overlap corner: this is a bound, not a sampling
    // heuristic that can miss a narrow ridge between test points.
    auto deficit = [&]() {
        std::vector<float> errors;
        for (size_t p = 0; p + 2 < out.size(); p += 3) {
            double worst = 0;
            const Vertex* t = &out[p];
            const double area2 = (double)(t[1].x - t[0].x) * (t[2].z - t[0].z) -
                                 (double)(t[1].z - t[0].z) * (t[2].x - t[0].x);
            // A sliver with no area in XZ covers no ground and has no plane:
            // plane() answers -1e30 for it, which would read as a 1e30
            // deficit and lift the whole patch out of the world.
            if (std::fabs(area2) < 1e-9) {
                errors.push_back(0.0f);
                continue;
            }
            const double sign = area2 >= 0 ? 1 : -1;
            for (size_t r = 0; r + 2 < local.size(); r += 3) {
                const Vertex* road = &local[r];
                if (std::max({road[0].x, road[1].x, road[2].x}) <
                        std::min({t[0].x, t[1].x, t[2].x}) ||
                    std::min({road[0].x, road[1].x, road[2].x}) >
                        std::max({t[0].x, t[1].x, t[2].x}) ||
                    std::max({road[0].z, road[1].z, road[2].z}) <
                        std::min({t[0].z, t[1].z, t[2].z}) ||
                    std::min({road[0].z, road[1].z, road[2].z}) >
                        std::max({t[0].z, t[1].z, t[2].z})) continue;
                std::vector<Q> poly{{road[0].x, road[0].z}, {road[1].x, road[1].z},
                                    {road[2].x, road[2].z}};
                for (int e = 0; e < 3 && !poly.empty(); ++e) {
                    const Vertex& a = t[e]; const Vertex& b = t[(e + 1) % 3];
                    auto side = [&](Q q) { return sign * ((b.x - a.x) * (q.z - a.z) -
                                                         (b.z - a.z) * (q.x - a.x)); };
                    std::vector<Q> next;
                    Q prev = poly.back(); double dp = side(prev);
                    for (Q q : poly) {
                        const double dq = side(q);
                        if ((dp >= 0) != (dq >= 0)) {
                            const double f = dp / (dp - dq);
                            next.push_back({prev.x + f * (q.x - prev.x),
                                            prev.z + f * (q.z - prev.z)});
                        }
                        if (dq >= 0) next.push_back(q);
                        prev = q; dp = dq;
                    }
                    poly.swap(next);
                }
                for (Q q : poly)
                    worst = std::max(worst, plane(road, q.x, q.z) + kSpillLift -
                                            plane(t, q.x, q.z));
            }
            errors.push_back((float)worst);
        }
        return errors;
    };
    auto edge = [](Vertex a, Vertex b) {
        if (a.x > b.x || (a.x == b.x && a.z > b.z)) std::swap(a, b);
        return std::array<float, 4>{a.x, a.z, b.x, b.z};
    };
    // The outline cut along the ground's own triangles: every piece lies in
    // one terrain plane, so it follows the ground exactly where a fan from
    // the centre would chord across every fold. Each piece is the ring
    // clipped by one (convex) ground triangle, then ear-clipped.
    auto gridPatch = [&]() {
        std::vector<Vertex> tris;
        std::vector<Q> subject;
        for (size_t k = 0; k < corners; ++k) subject.push_back({ring[k * 2], ring[k * 2 + 1]});
        // The ring clipped by one (convex) ground triangle, repeats dropped.
        auto clip = [&](const std::array<Vertex, 3>& cell, double* area) {
            std::vector<Q> poly = subject;
            for (int e = 0; e < 3 && poly.size() >= 3; ++e) {
                const Vertex& a = cell[(size_t)e];
                const Vertex& b = cell[(size_t)(e + 1) % 3];
                auto side = [&](Q q) {
                    return (b.x - a.x) * (q.z - a.z) - (b.z - a.z) * (q.x - a.x);
                };
                std::vector<Q> next;
                Q prev = poly.back();
                double dp = side(prev);
                for (Q q : poly) {
                    const double dq = side(q);
                    if ((dp >= 0) != (dq >= 0)) {
                        const double f = dp / (dp - dq);
                        next.push_back({prev.x + f * (q.x - prev.x), prev.z + f * (q.z - prev.z)});
                    }
                    if (dq >= 0) next.push_back(q);
                    prev = q;
                    dp = dq;
                }
                poly.swap(next);
            }
            std::vector<DQ> p;
            for (Q q : poly)
                if (p.empty() || std::hypot(p.back().x - q.x, p.back().z - q.z) > 1e-5)
                    p.push_back({q.x, q.z});
            while (p.size() > 1 && std::hypot(p.back().x - p[0].x, p.back().z - p[0].z) <= 1e-5)
                p.pop_back();
            *area = 0;
            for (size_t k = 0; p.size() >= 3 && k < p.size(); ++k)
                *area += crossDQ(p[0], p[k], p[(k + 1) % p.size()]);
            return p;
        };
        // Snapped, so the corner two neighbouring pieces share is the same
        // float pair however each piece clipped it (no hairline crack).
        auto snap = [](double v) { return (float)(std::round(v * 4096.0) / 4096.0); };
        for (const auto& cell : cells) {
            double area = 0;
            std::vector<DQ> p = clip(cell, &area);
            if (p.size() < 3 || area < 1e-6) continue;
            std::vector<DQ> pieces;
            earClip(p, pieces);
            for (const DQ& q : pieces) tris.push_back(vertex(snap(q.x), snap(q.z)));
        }
        return tris;
    };
    float guard = 0;
    bool gridded = false;
    for (int level = 0; ; ++level) {
        std::vector<float> errors = deficit();
        guard = *std::max_element(errors.begin(), errors.end());
        // A node patch the fan cannot follow is rebuilt along the ground
        // grid once, before any splitting.
        if (guard > kJunctionTol && !gridded && !cells.empty()) {
            gridded = true;
            std::vector<Vertex> g2 = gridPatch();
            if (!g2.empty()) {
                out.swap(g2);
                errors = deficit();
                guard = *std::max_element(errors.begin(), errors.end());
            }
        }
        // Split only triangles that need it, and split their neighbours on
        // the same edges. No T-junctions; flat patches retain four triangles.
        // The final bound still guarantees clearance at the safety cap.
        // A many-armed node starts from more triangles; stop refining before
        // it becomes a vertex budget of its own (one more split at most
        // quadruples it, so this caps a patch at 3600 vertices; the lift
        // below still holds).
        if (guard <= kJunctionTol || level == 3 || out.size() > 3 * 300) break;
        std::set<std::array<float, 4>> split;
        for (size_t i = 0; i < errors.size(); ++i) {
            if (errors[i] <= kJunctionTol) continue;
            const Vertex* t = &out[i * 3];
            for (int k = 0; k < 3; ++k) split.insert(edge(t[k], t[(k + 1) % 3]));
        }
        std::vector<Vertex> refined;
        refined.reserve(out.size() * 4);
        for (size_t i = 0; i + 2 < out.size(); i += 3) {
            const Vertex a = out[i], b = out[i + 1], c = out[i + 2];
            const int mask = (split.count(edge(a, b)) ? 1 : 0) |
                             (split.count(edge(b, c)) ? 2 : 0) |
                             (split.count(edge(c, a)) ? 4 : 0);
            auto mid = [&](Vertex p, Vertex q) {
                return vertex((p.x + q.x) * 0.5f, (p.z + q.z) * 0.5f);
            };
            const Vertex ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
            switch (mask) {
            case 0: refined.insert(refined.end(), {a, b, c}); break;
            case 1: refined.insert(refined.end(), {a, ab, c, ab, b, c}); break;
            case 2: refined.insert(refined.end(), {b, bc, a, bc, c, a}); break;
            case 4: refined.insert(refined.end(), {c, ca, b, ca, a, b}); break;
            case 3: refined.insert(refined.end(), {b, bc, ab, a, ab, c, ab, bc, c}); break;
            case 6: refined.insert(refined.end(), {c, ca, bc, b, bc, a, bc, ca, a}); break;
            case 5: refined.insert(refined.end(), {a, ab, ca, c, ca, b, ca, ab, b}); break;
            case 7: refined.insert(refined.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca}); break;
            }
        }
        out.swap(refined);
    }
    for (Vertex& v : out) v.y += guard + 0.0001f;
}

void tessellateSpill(const std::vector<float>& lowPts, float lowWidth,
                     float lowSampleStep, const std::vector<float>& highPts,
                     float highWidth, float spill, std::vector<SpillVertex>& out,
                     float lowEdgeFade) {
    out.clear();
    if (spill <= 0.0f || lowPts.size() < 4 || highPts.size() < 4) return;
    // The high road's centreline as a dense polyline (0.5-unit pieces per
    // segment): the fade is a distance from its edge.
    std::vector<P> line;
    const int hn = (int)(highPts.size() / 2);
    for (int seg = 0; seg + 1 < hn; ++seg) {
        const P a = pointAt(highPts, seg), b = pointAt(highPts, seg + 1);
        const int steps =
            std::max(1, (int)std::ceil(std::hypot(b.x - a.x, b.z - a.z) / 0.5f));
        for (int k = (seg == 0 ? 0 : 1); k <= steps; ++k)
            line.push_back(sample(highPts, seg, (float)k / (float)steps));
    }
    if (line.size() < 2) return;
    float mnx = 1e30f, mnz = 1e30f, mxx = -1e30f, mxz = -1e30f;
    for (const P& q : line) {
        mnx = std::min(mnx, q.x);
        mnz = std::min(mnz, q.z);
        mxx = std::max(mxx, q.x);
        mxz = std::max(mxz, q.z);
    }
    const float hw = 0.5f * highWidth;
    mnx -= hw; mnz -= hw; mxx += hw; mxz += hw;
    // Depth INSIDE the high road: half its width minus the distance to the
    // centreline (negative outside it).
    auto inside = [&](float x, float z) {
        if (x < mnx || x > mxx || z < mnz || z > mxz) return -1e30f;
        float best = 1e30f;
        for (size_t i = 0; i + 1 < line.size(); ++i) {
            const float ax = line[i].x, az = line[i].z;
            const float dx = line[i + 1].x - ax, dz = line[i + 1].z - az;
            const float l2 = dx * dx + dz * dz;
            float t = l2 > 1e-12f ? ((x - ax) * dx + (z - az) * dz) / l2 : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const float ex = ax + dx * t - x, ez = az + dz * t - z;
            best = std::min(best, ex * ex + ez * ez);
        }
        return hw - std::sqrt(best);
    };
    // The low road flat (no height: the consumer lifts it onto the surface).
    // With a soft edge the spill needs the DENSE lateral grid - the reduced
    // one collapses a flat street to one quad per station, whose only lateral
    // vertices are the two faded edges - and each vertex also carries the
    // edge's lateral fade (the soft-edge bands' alpha).
    std::vector<Vertex> tris;
    std::vector<float> lateral;  // per vertex, 1 without a soft edge
    const EdgeFade ef = edgeFadeFor(lowWidth, lowEdgeFade);
    if (ef.columns > 0) {
        std::vector<std::vector<Vertex>> rows;
        int cs = 1;
        buildRows(lowPts, lowWidth, nullptr, lowSampleStep, rows, &cs);
        const int kc = ef.columns;
        auto lat = [&](int j) {
            if (j <= kc) return (float)j / (float)kc;
            if (j >= cs - kc) return (float)(cs - j) / (float)kc;
            return 1.0f;
        };
        for (size_t i = 0; i + 1 < rows.size(); ++i)
            for (int j = 0; j < cs; ++j) {
                const Vertex& A = rows[i][(size_t)j];
                const Vertex& B = rows[i][(size_t)j + 1];
                const Vertex& C = rows[i + 1][(size_t)j + 1];
                const Vertex& D = rows[i + 1][(size_t)j];
                for (const Vertex* q : {&A, &B, &C, &A, &C, &D}) tris.push_back(*q);
                for (int jj : {j, j + 1, j + 1, j, j + 1, j}) lateral.push_back(lat(jj));
            }
    } else {
        tessellate(lowPts, lowWidth, nullptr, tris, {}, lowSampleStep);
        lateral.assign(tris.size(), 1.0f);
    }
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        float d[3], a[3];
        bool anyInside = false, anyVisible = false;
        for (int k = 0; k < 3; ++k) {
            d[k] = inside(tris[t + k].x, tris[t + k].z);
            // 1 at (and beyond) the edge, 0 `spill` units in.
            a[k] = d[k] <= 0.0f ? 1.0f : std::clamp(1.0f - d[k] / spill, 0.0f, 1.0f);
            a[k] *= lateral[t + k];
            anyInside |= d[k] > 0.0f;
            anyVisible |= a[k] > 0.001f;
        }
        // Wholly outside: that is the low road itself. Wholly faded: nothing.
        if (!anyInside || !anyVisible) continue;
        for (int k = 0; k < 3; ++k)
            out.push_back({tris[t + k].x, tris[t + k].z, tris[t + k].u,
                           tris[t + k].v, a[k]});
    }
}

EdgeFade edgeFadeFor(float width, float fade) {
    EdgeFade e;
    const float w = width > 0.1f ? width : 0.1f;
    e.coreWidth = w;
    if (fade <= 0.0f) return e;
    // buildRows' own lateral grid, so the band's inner column IS a core edge.
    const int cs = std::max(1, (int)std::ceil(w / kCrossSampleStep));
    const float step = w / (float)cs;
    // Keep at least one core cell: a road that is all fade has nothing opaque.
    const int maxCols = (cs - 1) / 2;
    const int cols = std::clamp((int)std::lround(fade / step), 0, maxCols);
    if (cols <= 0) return e;
    e.columns = cols;
    e.coreWidth = w - 2.0f * (float)cols * step;
    e.uInset = (float)cols / (float)cs;
    return e;
}

void tessellateEdges(const std::vector<float>& pointsXZ, float width,
                     float sampleStep, float fade, std::vector<SpillVertex>& out) {
    out.clear();
    const EdgeFade e = edgeFadeFor(width, fade);
    if (e.columns <= 0) return;
    std::vector<std::vector<Vertex>> rows;
    int cs = 1;
    buildRows(pointsXZ, width, nullptr, sampleStep, rows, &cs);
    const int k = e.columns;
    auto alphaOf = [&](int j) {
        if (j <= k) return (float)j / (float)k;              // left band
        if (j >= cs - k) return (float)(cs - j) / (float)k;  // right band
        return 1.0f;
    };
    auto push = [&](const Vertex& q, int j) {
        out.push_back({q.x, q.z, q.u, q.v, alphaOf(j)});
    };
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        const std::vector<Vertex>& r0 = rows[i];
        const std::vector<Vertex>& r1 = rows[i + 1];
        for (int side = 0; side < 2; ++side) {
            const int j0 = side == 0 ? 0 : cs - k;
            for (int j = j0; j < j0 + k; ++j) {
                // The road's stitch: A B C, A C D (CCW from above).
                push(r0[(size_t)j], j);
                push(r0[(size_t)j + 1], j + 1);
                push(r1[(size_t)j + 1], j + 1);
                push(r0[(size_t)j], j);
                push(r1[(size_t)j + 1], j + 1);
                push(r1[(size_t)j], j);
            }
        }
    }
}

// --- crossings: the one decision (docs/roads.md, "Junction overrides") -----

namespace {

// How far from a crossing's centre its spill and overlay triangles can lie:
// the node outline, half the widest road (a reduced full-width triangle's
// centroid) and a station of slack.
float crossingReach(const Crossing& c, const std::vector<CrossingRoad>& roads) {
    float r = 0.0f;
    const std::vector<float>& o = c.shape.outline;
    if (!o.empty()) {
        for (size_t k = 0; k + 1 < o.size(); k += 2)
            r = std::max(r, std::hypot(o[k] - c.shape.x, o[k + 1] - c.shape.z));
    } else {
        for (int k = 0; k < 4; ++k)
            r = std::max(r, std::hypot(c.shape.cornerXZ[k * 2] - c.shape.x,
                                       c.shape.cornerXZ[k * 2 + 1] - c.shape.z));
    }
    float w = 0.0f;
    for (int ri : c.roads) w = std::max(w, roads[(size_t)ri].width);
    return r + 0.5f * w + 2.0f;
}

// --- road nodes (docs/roads.md "Road nodes") ---------------------------------

// A road centre line as a polyline with its running arc length. One-unit
// pieces, the spacing findJunctions always sampled crossings at.
struct Line {
    std::vector<P> p;
    std::vector<float> s;
    bool closed = false;
    float len() const { return s.empty() ? 0.0f : s.back(); }
};

Line lineOf(const std::vector<float>& pts) {
    Line l;
    l.closed = isClosed(pts);
    l.p = centreLine(pts);
    for (size_t i = 0; i < l.p.size(); ++i)
        l.s.push_back(i == 0 ? 0.0f
                             : l.s.back() + std::hypot(l.p[i].x - l.p[i - 1].x,
                                                       l.p[i].z - l.p[i - 1].z));
    return l;
}

// The centre-line point at arc length s: wrapped on a loop, clamped otherwise.
P lineAt(const Line& l, float s) {
    if (l.p.empty()) return {0.0f, 0.0f};
    const float L = l.len();
    if (l.p.size() < 2 || !(L > 0.0f)) return l.p[0];
    if (l.closed) {
        s = std::fmod(s, L);
        if (s < 0.0f) s += L;
    } else {
        s = std::clamp(s, 0.0f, L);
    }
    size_t i = (size_t)(std::upper_bound(l.s.begin(), l.s.end(), s) - l.s.begin());
    i = std::clamp(i, (size_t)1, l.s.size() - 1);
    const float s0 = l.s[i - 1], s1 = l.s[i];
    const float f = s1 > s0 ? (s - s0) / (s1 - s0) : 0.0f;
    return {l.p[i - 1].x + (l.p[i].x - l.p[i - 1].x) * f,
            l.p[i - 1].z + (l.p[i].z - l.p[i - 1].z) * f};
}

// Distance from (x, z) to the centre line, and the arc length of the nearest
// point on it.
float projectOnto(const Line& l, float x, float z, float* sOut) {
    float best = 1e30f, bestS = 0.0f;
    for (size_t i = 0; i + 1 < l.p.size(); ++i) {
        const float ax = l.p[i].x, az = l.p[i].z;
        const float dx = l.p[i + 1].x - ax, dz = l.p[i + 1].z - az;
        const float l2 = dx * dx + dz * dz;
        float t = l2 > 1e-12f ? ((x - ax) * dx + (z - az) * dz) / l2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float ex = ax + dx * t - x, ez = az + dz * t - z;
        const float d = ex * ex + ez * ez;
        if (d < best) {
            best = d;
            bestS = l.s[i] + (l.s[i + 1] - l.s[i]) * t;
        }
    }
    if (sOut) *sOut = bestS;
    return std::sqrt(best);
}

// How far beyond its half width an open end may stop short of another road
// and still be read as ending ON it.
constexpr float kNodeSnap = 1.0f;
// Fillet radius as a fraction of the two arms' mean half width, clamped.
constexpr float kCornerRadiusScale = 1.5f;
constexpr float kCornerRadiusMin = 1.0f, kCornerRadiusMax = 8.0f;
// An arm is never trimmed shorter than this, nor further than this many of
// its own widths (a shallow fork would otherwise pave half the map).
constexpr float kMinTrim = 1.0f, kMaxTrimWidths = 3.0f;
// Square slack between the last fillet tangent and the arm's cap.
constexpr float kCapMargin = 0.5f;
// Arc tessellation: one outline point per this many radians of fillet.
#ifndef TYRA_NODE_ARC_DIV
#define TYRA_NODE_ARC_DIV 8.0f
#endif
#ifndef TYRA_NODE_EDGE_STEP
#define TYRA_NODE_EDGE_STEP 2.0f
#endif
constexpr float kArcStep = 3.14159265f / TYRA_NODE_ARC_DIV;
// Transition nodes: the smallest half-width step that makes one, and the
// taper's length - this many units per unit of width lost, clamped.
constexpr float kTransitionMinStep = 0.25f;
constexpr float kTaperPerUnit = 3.0f, kTaperMin = 4.0f, kTaperMax = 20.0f;
constexpr float kEdgeStep = TYRA_NODE_EDGE_STEP;

struct Arm {
    int road = -1;
    float ux = 1.0f, uz = 0.0f;  // outward unit direction at the node
    float h = 1.0f;              // half width
    float limit = 1e30f;         // the most it may be trimmed (centre-line arc)
    float angle = 0.0f;
    // The road under the arm: its centre line, the arc length nearest the
    // node and which way along it the arm leaves (+1 / -1). The outline's
    // edges follow this curve, so an arm on a bend bends with its road.
    const Line* line = nullptr;
    float s = 0.0f, dir = 1.0f;
    float fixedTrim = 0.0f;  // > 0: a transition node's set trim (in)
    float trim = 0.0f;       // where the outline cut the arm (out)
};

// A point on an arm's edge `sigma` along its centre line from the node:
// side +1 = the arm's left edge, -1 its right. `tx/tz` receive the arm's
// outward tangent there.
P armEdge(const Arm& a, float sigma, float side, float* tx = nullptr, float* tz = nullptr) {
    const float s = a.s + a.dir * sigma;
    const P c = lineAt(*a.line, s);
    const P f = lineAt(*a.line, s + a.dir * 0.5f), b = lineAt(*a.line, s - a.dir * 0.5f);
    float ux = f.x - b.x, uz = f.z - b.z;
    const float ul = std::hypot(ux, uz);
    if (ul > 1e-6f) {
        ux /= ul;
        uz /= ul;
    } else {
        ux = a.ux;
        uz = a.uz;
    }
    if (tx) *tx = ux;
    if (tz) *tz = uz;
    return {c.x - side * a.h * uz, c.z + side * a.h * ux};
}

// The convex hull of a point ring (monotone chain), counter-clockwise.
std::vector<P> hullOf(std::vector<P> pts) {
    std::sort(pts.begin(), pts.end(), [](const P& a, const P& b) {
        return a.x < b.x || (a.x == b.x && a.z < b.z);
    });
    if (pts.size() < 3) return pts;
    auto cross = [](const P& o, const P& a, const P& b) {
        return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
    };
    std::vector<P> h(pts.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    h.resize(k - 1);
    return h;
}

// The node polygon: every arm cut square at its trim distance, the gap
// between each pair of neighbouring arms closed by a fillet tangent to both
// road edges (or, on a reflex side, an arc around the centre). Trims grow
// until the fillets fit and are then clamped by how much road each arm has;
// a fillet that no longer fits shrinks, and one that cannot shrink enough is
// cut straight across.
// `taper`: a TRANSITION node (two arms in line, two widths) - every corner
// is cut, so the outline is the straight taper between the two caps.
std::vector<float> nodeOutline(float cx, float cz, std::vector<Arm>& arms, bool taper,
                               std::vector<unsigned char>* capsOut) {
    const float kPi = 3.14159265358979f;
    for (Arm& a : arms) a.angle = std::atan2(a.uz, a.ux);
    std::sort(arms.begin(), arms.end(),
              [](const Arm& l, const Arm& r) { return l.angle < r.angle; });
    const int n = (int)arms.size();
    enum { kOpen, kFillet, kCut };
    struct Corner {
        int type = kOpen;
        float a = 0, b = 0, t = 0, theta = 0;
    };
    std::vector<Corner> cs((size_t)n);
    // Left normal of an arm: its direction turned +90 degrees, so the angular
    // order and the "left" side agree.
    auto nlx = [&](int i) { return -arms[(size_t)i].uz; };
    auto nlz = [&](int i) { return arms[(size_t)i].ux; };
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const Arm& A = arms[(size_t)i];
        const Arm& B = arms[(size_t)j];
        float theta = B.angle - A.angle;
        if (n == 1 || theta <= 0.0f) theta += 2.0f * kPi;
        Corner& c = cs[(size_t)i];
        c.theta = theta;
        if (theta >= kPi - 0.02f) continue;  // a reflex (or straight) side
        // Left edge of A: h_A nl_A + a u_A. Right edge of B: -h_B nl_B + b u_B.
        const float rx = -B.h * nlx(j) - A.h * nlx(i);
        const float rz = -B.h * nlz(j) - A.h * nlz(i);
        const float det = -(A.ux * B.uz - A.uz * B.ux);
        if (std::fabs(det) < 1e-6f) continue;
        c.a = (rx * -B.uz - rz * -B.ux) / det;
        c.b = (A.ux * rz - A.uz * rx) / det;
        const float r = std::clamp(kCornerRadiusScale * 0.5f * (A.h + B.h),
                                   kCornerRadiusMin, kCornerRadiusMax);
        c.t = std::max({r / std::tan(0.5f * theta), -c.a, -c.b});
        c.type = kFillet;
    }
    std::vector<float> d((size_t)n);
    for (int i = 0; i < n; ++i) {
        const int p = (i + n - 1) % n;
        float need = 0.0f;
        if (cs[(size_t)i].type == kFillet) need = cs[(size_t)i].a + cs[(size_t)i].t;
        if (cs[(size_t)p].type == kFillet)
            need = std::max(need, cs[(size_t)p].b + cs[(size_t)p].t);
        const Arm& A = arms[(size_t)i];
        const float cap = std::max(kMinTrim, std::min(A.limit, kMaxTrimWidths * 2.0f * A.h));
        d[(size_t)i] = A.fixedTrim > 0.0f
                           ? std::max(0.25f, std::min(A.fixedTrim, A.limit))
                           : std::min(std::max(need + kCapMargin, kMinTrim), cap);
        if (taper) cs[(size_t)i].type = kCut;
    }
    for (int i = 0; i < n; ++i) arms[(size_t)i].trim = d[(size_t)i];
    for (int i = 0; i < n; ++i) {
        Corner& c = cs[(size_t)i];
        if (c.type != kFillet) continue;
        const int j = (i + 1) % n;
        const float tmax = std::min(d[(size_t)i] - c.a, d[(size_t)j] - c.b);
        if (tmax >= c.t) continue;
        if (tmax > std::max({-c.a, -c.b, 0.0f}) + 0.05f)
            c.t = tmax;
        else
            c.type = kCut;
    }

    std::vector<P> ring;
    // Per point: 1 when the segment FROM it is an arm's cap (where the road
    // carries on), which is what an edge line must not be painted across.
    std::vector<unsigned char> capFrom;
    auto push = [&](P q) {
        if (!ring.empty() && std::hypot(ring.back().x - q.x, ring.back().z - q.z) < 1e-3f)
            return;
        ring.push_back(q);
        capFrom.push_back(0);
    };
    auto markCap = [&]() {
        if (!capFrom.empty()) capFrom.back() = 1;
    };
    // An arm's edge between two distances, walked in the given order, one
    // point per unit of road so a bend is followed.
    auto walkEdge = [&](const Arm& a, float from, float to, float side) {
        const int steps = std::max(1, (int)std::ceil(std::fabs(to - from) / kEdgeStep));
        for (int k = 0; k <= steps; ++k)
            push(armEdge(a, from + (to - from) * (float)k / (float)steps, side));
    };
    // Segment p0-p1 against q0-q1: the crossing and its fraction along q.
    auto crossAt = [](P p0, P p1, P q0, P q1, P* at, float* fq) {
        const float rx = p1.x - p0.x, rz = p1.z - p0.z;
        const float sx = q1.x - q0.x, sz = q1.z - q0.z;
        const float den = rx * sz - rz * sx;
        if (std::fabs(den) < 1e-9f) return false;
        const float qx = q0.x - p0.x, qz = q0.z - p0.z;
        const float t = (qx * sz - qz * sx) / den, u = (qx * rz - qz * rx) / den;
        if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) return false;
        *at = {p0.x + rx * t, p0.z + rz * t};
        *fq = u;
        return true;
    };
    // An arm whose cap begins on its neighbour's edge (a cut corner, below)
    // does not emit its own right cap corner.
    std::vector<char> skipRight((size_t)n, 0);
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const Arm& A = arms[(size_t)i];
        const Arm& B = arms[(size_t)j];
        const float di = d[(size_t)i], dj = d[(size_t)j];
        if (!skipRight[(size_t)i]) {
            push(armEdge(A, di, -1.0f));
            markCap();
        }
        const Corner& c = cs[(size_t)i];
        if (c.type == kFillet) {
            const float ta = c.a + c.t, tb = c.b + c.t;
            walkEdge(A, di, ta, 1.0f);
            // A quadratic Bezier tangent to both edges where they leave it:
            // the circle's job, on edges that may curve.
            float t1x, t1z, t2x, t2z;
            const P p0 = armEdge(A, ta, 1.0f, &t1x, &t1z);
            const P p2 = armEdge(B, tb, -1.0f, &t2x, &t2z);
            // Rays inward along each edge: p0 - l*t1 and p2 - m*t2.
            const float den = t1x * t2z - t1z * t2x;
            bool curved = false;
            if (std::fabs(den) > 1e-4f) {
                const float qx = p2.x - p0.x, qz = p2.z - p0.z;
                // p0 - l t1 = p2 - m t2  =>  -l t1 + m t2 = q
                const float l = -(qx * t2z - qz * t2x) / den;
                const float m = (t1x * qz - t1z * qx) / den;
                if (l > 0.0f && m > 0.0f) {
                    const P p1{p0.x - l * t1x, p0.z - l * t1z};
                    const int steps =
                        std::max(2, (int)std::ceil((kPi - c.theta) / kArcStep));
                    for (int k = 1; k < steps; ++k) {
                        const float f = (float)k / (float)steps, g = 1.0f - f;
                        push({g * g * p0.x + 2.0f * g * f * p1.x + f * f * p2.x,
                              g * g * p0.z + 2.0f * g * f * p1.z + f * f * p2.z});
                    }
                    curved = true;
                }
            }
            (void)curved;
            walkEdge(B, tb, dj, -1.0f);
        } else if (c.type == kOpen) {
            walkEdge(A, di, 0.0f, 1.0f);
            const P s1 = armEdge(A, 0.0f, 1.0f), s2 = armEdge(B, 0.0f, -1.0f);
            const float g1 = std::atan2(s1.z - cz, s1.x - cx);
            float sweep = std::atan2(s2.z - cz, s2.x - cx) - g1;
            while (sweep < 0.0f) sweep += 2.0f * kPi;
            while (sweep >= 2.0f * kPi) sweep -= 2.0f * kPi;
            // A side just short of straight puts s2 a hair clockwise of s1;
            // that is a straight join, not a full turn around the node.
            if (sweep > kPi) sweep = 0.0f;
            const float r1 = std::hypot(s1.x - cx, s1.z - cz);
            const float r2 = std::hypot(s2.x - cx, s2.z - cz);
            const int steps = std::max(1, (int)std::ceil(sweep / kArcStep));
            for (int k = 1; k < steps; ++k) {
                const float f = (float)k / (float)steps;
                const float rr = r1 + (r2 - r1) * f;
                push({cx + rr * std::cos(g1 + sweep * f), cz + rr * std::sin(g1 + sweep * f)});
            }
            walkEdge(B, 0.0f, dj, -1.0f);
        } else {
            // Cut: the two arms still overlap where they are trimmed (a slip
            // road leaving at a shallow angle). The union's outline runs
            // from one cap onto the other arm's edge where they cross, so
            // the gore beyond stays ground.
            const P capR = armEdge(A, di, -1.0f), capL = armEdge(A, di, 1.0f);
            const P jR0 = armEdge(B, 0.0f, -1.0f), jR1 = armEdge(B, dj, -1.0f);
            const P iL0 = armEdge(A, 0.0f, 1.0f);
            const P jCapR = jR1, jCapL = armEdge(B, dj, 1.0f);
            P at;
            float f = 0.0f;
            if (crossAt(capR, capL, jR0, jR1, &at, &f)) {
                // A's cap runs into B: leave A's cap there, follow B's edge out.
                push(at);
                walkEdge(B, f * dj, dj, -1.0f);
            } else if (crossAt(jCapR, jCapL, iL0, capL, &at, &f)) {
                // B's cap runs into A: follow A's edge in to it, start B's cap there.
                walkEdge(A, di, f * di, 1.0f);
                push(at);
                markCap();
                skipRight[(size_t)j] = 1;
                if (j == 0 && !ring.empty()) {
                    ring.erase(ring.begin());
                    capFrom.erase(capFrom.begin());
                }
            } else {
                push(capL);
            }
        }
    }
    while (ring.size() > 1 &&
           std::hypot(ring.back().x - ring[0].x, ring.back().z - ring[0].z) < 1e-3f) {
        // The closing duplicate IS ring[0]: a cap that starts there starts
        // at ring[0].
        const unsigned char f = capFrom.back();
        ring.pop_back();
        capFrom.pop_back();
        if (!capFrom.empty()) capFrom[0] = (unsigned char)(capFrom[0] | f);
    }
    // The ring need not be star-shaped about the centre (an arm on a bend
    // curls around it); tessellateJunctionSurface ear-clips one that is not.
    // It must be SIMPLE, though: arms folding over each other would make a
    // self-crossing ring, and their convex hull still covers every arm.
    bool simple = ring.size() >= 3;
    double area = 0.0;
    for (size_t k = 0; k < ring.size(); ++k) {
        const P& a = ring[k];
        const P& b = ring[(k + 1) % ring.size()];
        area += (double)a.x * b.z - (double)b.x * a.z;
    }
    simple &= area > 1e-4;
    for (size_t k = 0; k < ring.size() && simple; ++k)
        for (size_t m = k + 2; m < ring.size() && simple; ++m) {
            if (k == 0 && m + 1 == ring.size()) continue;  // neighbours via the wrap
            P at;
            float f;
            simple = !crossAt(ring[k], ring[(k + 1) % ring.size()], ring[m],
                              ring[(m + 1) % ring.size()], &at, &f);
        }
    if (!simple) {
        ring = hullOf(ring);
        capFrom.assign(ring.size(), 1);  // a hull's segments map to nothing: no edge lines
    }
    if (capsOut) *capsOut = capFrom;
    std::vector<float> out;
    for (const P& q : ring) out.insert(out.end(), {q.x, q.z});
    return out;
}

}  // namespace

std::vector<Crossing> findNodes(const std::vector<CrossingRoad>& roads) {
    const int n = (int)roads.size();
    std::vector<Line> lines((size_t)n);
    for (int i = 0; i < n; ++i)
        if (roads[(size_t)i].points.size() >= 4) lines[(size_t)i] = lineOf(roads[(size_t)i].points);
    auto halfW = [&](int i) { return 0.5f * std::max(0.1f, roads[(size_t)i].width); };
    auto usable = [&](int i) { return lines[(size_t)i].p.size() >= 2; };

    // 1. Contacts between pairs of roads.
    struct Contact {
        float x, z;
        int a, b;
    };
    std::vector<Contact> contacts;
    // Bridges (docs/roads.md "Bridges"): a node forms only where both roads
    // are on the ground - a deck passing over another road is an OVERPASS,
    // not a junction, and two decks meeting in the air are not supported.
    auto elevated = [&](int r, float x, float z) {
        return roads[(size_t)r].elevation ? roads[(size_t)r].elevation(x, z) : 0.0f;
    };
    auto addContact = [&](int a, int b, float x, float z) {
        if (std::max(elevated(a, x, z), elevated(b, x, z)) > kOverpassClearance) return;
        const float merge = 0.5f * std::min(roads[(size_t)a].width, roads[(size_t)b].width);
        for (const Contact& c : contacts)
            if (std::min(c.a, c.b) == std::min(a, b) && std::max(c.a, c.b) == std::max(a, b) &&
                std::hypot(c.x - x, c.z - z) < merge)
                return;
        contacts.push_back({x, z, a, b});
    };
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b) {
            if (!usable(a) || !usable(b)) continue;
            const std::vector<P>& A = lines[(size_t)a].p;
            const std::vector<P>& B = lines[(size_t)b].p;
            // Centre lines crossing, at any angle.
            for (size_t ai = 0; ai + 1 < A.size(); ++ai) {
                const float adx = A[ai + 1].x - A[ai].x, adz = A[ai + 1].z - A[ai].z;
                const float amnx = std::min(A[ai].x, A[ai + 1].x), amxx = std::max(A[ai].x, A[ai + 1].x);
                const float amnz = std::min(A[ai].z, A[ai + 1].z), amxz = std::max(A[ai].z, A[ai + 1].z);
                for (size_t bi = 0; bi + 1 < B.size(); ++bi) {
                    if (std::max(B[bi].x, B[bi + 1].x) < amnx || std::min(B[bi].x, B[bi + 1].x) > amxx ||
                        std::max(B[bi].z, B[bi + 1].z) < amnz || std::min(B[bi].z, B[bi + 1].z) > amxz)
                        continue;
                    const float bdx = B[bi + 1].x - B[bi].x, bdz = B[bi + 1].z - B[bi].z;
                    const float den = adx * bdz - adz * bdx;
                    if (std::fabs(den) < 1e-9f) continue;
                    const float qx = B[bi].x - A[ai].x, qz = B[bi].z - A[ai].z;
                    const float ta = (qx * bdz - qz * bdx) / den;
                    const float tb = (qx * adz - qz * adx) / den;
                    if (ta < -1e-4f || ta > 1.0001f || tb < -1e-4f || tb > 1.0001f) continue;
                    addContact(a, b, A[ai].x + adx * ta, A[ai].z + adz * ta);
                }
            }
        }
    // Open ends resting on another road: a T, a fork, a corner.
    for (int a = 0; a < n; ++a) {
        if (!usable(a) || lines[(size_t)a].closed) continue;
        for (const P& e : {lines[(size_t)a].p.front(), lines[(size_t)a].p.back()})
            for (int b = 0; b < n; ++b) {
                if (b == a || !usable(b)) continue;
                float s = 0.0f;
                if (projectOnto(lines[(size_t)b], e.x, e.z, &s) > halfW(b) + kNodeSnap) continue;
                const P q = lineAt(lines[(size_t)b], s);
                addContact(a, b, q.x, q.z);
            }
    }

    // 2. Contacts close together are ONE node (three roads meeting make three
    // pairwise contacts at one spot).
    std::vector<int> parent(contacts.size());
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int)i;
    std::function<int(int)> root = [&](int i) {
        return parent[(size_t)i] == i ? i : parent[(size_t)i] = root(parent[(size_t)i]);
    };
    for (size_t i = 0; i < contacts.size(); ++i)
        for (size_t j = i + 1; j < contacts.size(); ++j) {
            const Contact& p = contacts[i];
            const Contact& q = contacts[j];
            const float r = std::max({halfW(p.a), halfW(p.b), halfW(q.a), halfW(q.b)}) + 0.5f;
            if (std::hypot(p.x - q.x, p.z - q.z) < r) parent[(size_t)root((int)j)] = root((int)i);
        }
    struct Node {
        float x = 0, z = 0;
        int count = 0;
        std::vector<int> roads;
        std::vector<float> s;  // per road: arc length nearest the centre
    };
    std::vector<Node> nodes;
    std::vector<int> nodeOf(contacts.size(), -1);
    for (size_t i = 0; i < contacts.size(); ++i) {
        const int r = root((int)i);
        if (nodeOf[(size_t)r] < 0) {
            nodeOf[(size_t)r] = (int)nodes.size();
            nodes.emplace_back();
        }
        Node& nd = nodes[(size_t)nodeOf[(size_t)r]];
        nd.x += contacts[i].x;
        nd.z += contacts[i].z;
        ++nd.count;
        for (int rd : {contacts[i].a, contacts[i].b})
            if (std::find(nd.roads.begin(), nd.roads.end(), rd) == nd.roads.end())
                nd.roads.push_back(rd);
    }
    for (Node& nd : nodes) {
        nd.x /= (float)nd.count;
        nd.z /= (float)nd.count;
        std::sort(nd.roads.begin(), nd.roads.end());
        for (int rd : nd.roads) {
            float s = 0.0f;
            projectOnto(lines[(size_t)rd], nd.x, nd.z, &s);
            nd.s.push_back(s);
        }
    }

    // 3. Arms, outline.
    std::vector<Crossing> out;
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        const Node& nd = nodes[ni];
        std::vector<Arm> arms;
        for (size_t k = 0; k < nd.roads.size(); ++k) {
            const int rd = nd.roads[k];
            const Line& l = lines[(size_t)rd];
            const float s = nd.s[k], L = l.len();
            float others = 0.0f;
            for (int q : nd.roads)
                if (q != rd) others = std::max(others, halfW(q));
            const float endTol = others + 1.5f;
            const P at = lineAt(l, s);
            const float off = std::hypot(at.x - nd.x, at.z - nd.z);
            const float look = std::max(2.0f, off * 2.0f);
            // The gap to the nearest other node along this road, either way.
            float fwdGap = l.closed ? 0.5f * L : L - s, backGap = l.closed ? 0.5f * L : s;
            for (size_t nj = 0; nj < nodes.size(); ++nj) {
                if (nj == ni) continue;
                const Node& o = nodes[nj];
                for (size_t m = 0; m < o.roads.size(); ++m) {
                    if (o.roads[m] != rd) continue;
                    float ds = o.s[m] - s;
                    if (l.closed) {
                        while (ds > 0.5f * L) ds -= L;
                        while (ds < -0.5f * L) ds += L;
                    }
                    if (ds > 0.0f) fwdGap = std::min(fwdGap, 0.5f * ds);
                    if (ds < 0.0f) backGap = std::min(backGap, -0.5f * ds);
                }
            }
            auto addArm = [&](float dir, float limit) {
                const P to = lineAt(l, s + dir * look);
                float ux = to.x - nd.x, uz = to.z - nd.z;
                const float ul = std::hypot(ux, uz);
                if (!(ul > 1e-4f)) return;
                Arm a;
                a.road = rd;
                a.ux = ux / ul;
                a.uz = uz / ul;
                a.h = halfW(rd);
                a.limit = limit;
                a.line = &l;
                a.s = s;
                a.dir = dir;
                arms.push_back(a);
            };
            const bool fwd = l.closed || L - s > endTol;
            const bool back = l.closed || s > endTol;
            if (fwd) addArm(1.0f, fwdGap);
            if (back) addArm(-1.0f, backGap);
            // A short road wholly inside the node still leaves it one way.
            if (!fwd && !back) addArm(L - s >= s ? 1.0f : -1.0f, std::max(L - s, s));
        }
        if (arms.size() < 2) continue;
        // Two arms almost in line is a road continuing, not a junction - unless
        // the width changes there: then it is a TRANSITION node, and its patch
        // is the taper from the wide road to the narrow one, laid entirely on
        // the narrow side (the wide road's own ribbon ends at the node at full
        // width, so a taper reaching into it would leave its corners showing).
        bool taper = false;
        if (arms.size() == 2 &&
            arms[0].ux * arms[1].ux + arms[0].uz * arms[1].uz < -0.866f) {
            Arm& w = arms[0].h >= arms[1].h ? arms[0] : arms[1];
            Arm& nw = &w == &arms[0] ? arms[1] : arms[0];
            if (w.h - nw.h < kTransitionMinStep || w.road == nw.road) continue;
            taper = true;
            w.fixedTrim = 0.5f;
            nw.fixedTrim = std::clamp(kTaperPerUnit * 2.0f * (w.h - nw.h), kTaperMin, kTaperMax);
        }
        Crossing c;
        c.roads = nd.roads;
        c.a = nd.roads[0];
        c.b = nd.roads.size() > 1 ? nd.roads[1] : nd.roads[0];
        c.arms = (int)arms.size();
        c.shape.x = nd.x;
        c.shape.z = nd.z;
        c.transition = taper;
        c.shape.outline = nodeOutline(nd.x, nd.z, arms, taper, &c.shape.outlineCap);
        if (c.shape.outline.size() < 6 || c.a == c.b) continue;
        // The arms as the markings (and anything else drawn on a node) see
        // them: where each was cut, which way it leaves, whether its road
        // ends here.
        for (const Arm& a : arms) {
            NodeArm na;
            na.road = a.road;
            na.h = a.h;
            na.trim = a.trim;
            int same = 0;
            for (const Arm& b : arms) same += b.road == a.road;
            na.ends = same == 1;
            const P cc = lineAt(*a.line, a.s + a.dir * a.trim);
            float tx = a.ux, tz = a.uz;
            armEdge(a, a.trim, 0.0f, &tx, &tz);
            na.capX = cc.x;
            na.capZ = cc.z;
            na.tx = tx;
            na.tz = tz;
            c.armList.push_back(na);
        }
        out.push_back(std::move(c));
    }
    return out;
}

CrossingPlan planCrossings(const std::vector<CrossingRoad>& roads,
                           const std::vector<JunctionOverride>& overrides,
                           bool withDecals) {
    CrossingPlan plan;
    const int n = (int)roads.size();
    auto usable = [&](int i) { return roads[(size_t)i].points.size() >= 4; };

    // 1. Every node: crossings, T's, forks and corners, one per place however
    // many roads meet there (docs/roads.md "Road nodes").
    plan.crossings = findNodes(roads);

    // 2. Overrides -> crossings: a node holding both roads, the nearest within
    // the narrower road's width. Stored order decides ties; a crossing takes
    // at most one override.
    plan.overrideCrossing.assign(overrides.size(), -1);
    std::vector<int> overrideWinner(plan.crossings.size(), -1);
    auto roadIndex = [&](const std::string& id) {
        if (id.empty()) return -1;
        for (int i = 0; i < n; ++i)
            if (roads[(size_t)i].id == id) return i;
        return -1;
    };
    for (size_t oi = 0; oi < overrides.size(); ++oi) {
        const JunctionOverride& o = overrides[oi];
        const int ia = roadIndex(o.roadA), ib = roadIndex(o.roadB);
        if (ia < 0 || ib < 0 || ia == ib) {
            ++plan.orphans;
            continue;
        }
        float best = std::max(1.0f, std::min(roads[(size_t)ia].width,
                                             roads[(size_t)ib].width));
        int at = -1;
        for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
            const Crossing& c = plan.crossings[ci];
            if (!c.has(ia) || !c.has(ib) || c.override >= 0) continue;
            const float d = std::hypot(c.shape.x - o.x, c.shape.z - o.z);
            if (d <= best) {
                best = d;
                at = (int)ci;
            }
        }
        if (at < 0) {
            ++plan.orphans;
            continue;
        }
        plan.crossings[(size_t)at].override = (int)oi;
        if (o.winner == kWinnerRoadA) overrideWinner[(size_t)at] = ia;
        if (o.winner == kWinnerRoadB) overrideWinner[(size_t)at] = ib;
        plan.overrideCrossing[oi] = at;
    }

    // 3. What each crossing does. A node may hold more than two roads; the
    // rules are the pair rules asked of all of them at once.
    for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
        Crossing& c = plan.crossings[ci];
        int winner = kWinnerAuto;
        std::string material;
        float grip = 0.0f;
        if (c.override >= 0) {
            const JunctionOverride& o = overrides[(size_t)c.override];
            winner = o.winner;
            material = o.material;
            grip = o.grip;
            // A material alone asks for a patch.
            if (winner == kWinnerAuto && !material.empty()) winner = kWinnerPatch;
        }
        bool sameRank = true, sameMaterial = true;
        int top = c.roads[0];
        float minGrip = 1e30f, maxLift = -1e30f;
        std::string anyMaterial;
        for (int ri : c.roads) {
            const CrossingRoad& R = roads[(size_t)ri];
            sameRank &= R.rank == roads[(size_t)c.roads[0]].rank;
            sameMaterial &= !R.intersection.empty() &&
                            R.intersection == roads[(size_t)c.roads[0]].intersection;
            if (R.rank > roads[(size_t)top].rank) top = ri;
            minGrip = std::min(minGrip, R.grip);
            maxLift = std::max(maxLift, rankLift(R.rank));
            if (anyMaterial.empty()) anyMaterial = R.intersection;
        }
        if (winner == kWinnerAuto) {
            if (sameRank) {
                if (sameMaterial) {
                    c.kind = kCrossPatch;
                    c.material = roads[(size_t)c.roads[0]].intersection;
                    c.grip = minGrip;
                    c.lift = maxLift;
                }
            } else {
                c.kind = kCrossThrough;
                c.winner = top;
            }
        } else if (winner == kWinnerPatch) {
            c.kind = kCrossPatch;
            c.material = !material.empty() ? material : anyMaterial;
            c.grip = grip > 0.0f ? grip : minGrip;
            c.lift = maxLift;
        } else {
            c.kind = kCrossThrough;
            c.winner = overrideWinner[ci] >= 0 ? overrideWinner[ci] : c.a;
            int loserRank = -1000;
            for (int ri : c.roads)
                if (ri != c.winner) loserRank = std::max(loserRank, roads[(size_t)ri].rank);
            // A winner the rank lift already puts on top needs nothing drawn,
            // unless the crossing's grip is overridden (the overlay carries it).
            c.overlay = roads[(size_t)c.winner].rank <= loserRank || grip > 0.0f;
            c.overlayGrip = grip > 0.0f ? grip : roads[(size_t)c.winner].grip;
        }
        if (c.kind == kCrossPatch)
            for (size_t e = 0; e < ci; ++e) {
                const Crossing& old = plan.crossings[e];
                if (old.kind == kCrossPatch && !old.patchDuplicate &&
                    std::hypot(old.shape.x - c.shape.x, old.shape.z - c.shape.z) < 0.5f)
                    c.patchDuplicate = true;
            }
    }
    if (!withDecals) return plan;

    // The crossing of pair (p, q) a triangle centred at (x, z) belongs to.
    auto crossingAt = [&](int p, int q, float x, float z) {
        int best = -1;
        float bestD = 1e30f;
        for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
            const Crossing& c = plan.crossings[ci];
            if (!c.has(p) || !c.has(q)) continue;
            const float d = std::hypot(c.shape.x - x, c.shape.z - z);
            if (d < crossingReach(c, roads) && d < bestD) {
                bestD = d;
                best = (int)ci;
            }
        }
        return best;
    };

    // 4a. OVERLAYS: at a crossing an override hands to the road the rank
    // would put underneath, the winner's own surface is laid over the loser
    // there - the spill's arrangement with no fade (alpha 1, only the
    // winner's soft edges), lifted kSpillLift over the highest road.
    for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
        const Crossing& c = plan.crossings[ci];
        if (c.kind != kCrossThrough || !c.overlay) continue;
        const int w = c.winner;
        for (int l : c.roads) {
            if (l == w) continue;
            const CrossingRoad& W = roads[(size_t)w];
            const CrossingRoad& L = roads[(size_t)l];
            std::vector<SpillVertex> sv;
            tessellateSpill(W.points, W.width, W.sampleStep, L.points, L.width,
                            std::numeric_limits<float>::infinity(), sv, W.edgeFade);
            CrossingDecal d;
            d.road = w;
            d.under = l;
            d.overlay = true;
            d.grip = c.overlayGrip;
            d.baseGrip = L.grip;
            d.hostLift = std::max(rankLift(W.rank), rankLift(L.rank)) + kSpillLift;
            for (size_t t = 0; t + 2 < sv.size(); t += 3) {
                const float cx = (sv[t].x + sv[t + 1].x + sv[t + 2].x) / 3.0f;
                const float cz = (sv[t].z + sv[t + 1].z + sv[t + 2].z) / 3.0f;
                if (crossingAt(w, l, cx, cz) != (int)ci) continue;
                d.verts.insert(d.verts.end(), sv.begin() + (long)t, sv.begin() + (long)t + 3);
            }
            if (!d.verts.empty()) plan.decals.push_back(std::move(d));
        }
    }

    // 4b. SPILLS, in the (low, high) order the codegen always used. The rank
    // decides the direction everywhere no crossing says otherwise; at a
    // crossing, only the road that crossing lets win is spilled onto.
    for (int li = 0; li < n; ++li)
        for (int hi = 0; hi < n; ++hi) {
            if (li == hi || !usable(li) || !usable(hi)) continue;
            const CrossingRoad& lo = roads[(size_t)li];
            const CrossingRoad& up = roads[(size_t)hi];
            if (lo.spill <= 0.0f) continue;
            const bool rankDir = up.rank > lo.rank;
            bool needed = rankDir;
            for (const Crossing& c : plan.crossings)
                if (c.override >= 0 && c.kind == kCrossThrough && c.winner == hi &&
                    c.has(li))
                    needed = true;
            if (!needed) continue;
            std::vector<SpillVertex> sv;
            tessellateSpill(lo.points, lo.width, lo.sampleStep, up.points, up.width,
                            lo.spill, sv, lo.edgeFade);
            if (sv.empty()) continue;
            // Grouped by where they land: on the road itself, or on an overlay
            // (one kSpillLift higher, over the overlay's grip). Without
            // overrides that is one group - the spill exactly as before.
            std::vector<CrossingDecal> groups;
            for (size_t t = 0; t + 2 < sv.size(); t += 3) {
                const float cx = (sv[t].x + sv[t + 1].x + sv[t + 2].x) / 3.0f;
                const float cz = (sv[t].z + sv[t + 1].z + sv[t + 2].z) / 3.0f;
                const int ci = crossingAt(li, hi, cx, cz);
                const Crossing* c = ci >= 0 ? &plan.crossings[(size_t)ci] : nullptr;
                const bool keep =
                    c ? (c->kind == kCrossThrough && c->winner == hi) : rankDir;
                if (!keep) continue;
                const bool onOverlay = c && c->overlay;
                const float extra = onOverlay ? kSpillLift : 0.0f;
                const float base = onOverlay ? c->overlayGrip : up.grip;
                CrossingDecal* g = nullptr;
                for (CrossingDecal& e : groups)
                    if (e.extraLift == extra && e.baseGrip == base) g = &e;
                if (!g) {
                    groups.emplace_back();
                    g = &groups.back();
                    g->road = li;
                    g->under = hi;
                    g->grip = lo.grip;
                    g->baseGrip = base;
                    g->extraLift = extra;
                    g->hostLift =
                        std::max(rankLift(lo.rank), rankLift(up.rank)) + kSpillLift + extra;
                }
                g->verts.insert(g->verts.end(), sv.begin() + (long)t,
                                sv.begin() + (long)t + 3);
            }
            for (CrossingDecal& g : groups) plan.decals.push_back(std::move(g));
        }
    return plan;
}

void bakeMarkings(const CrossingPlan& plan, const std::vector<CrossingRoad>& roads,
                  const Surface& surface, std::vector<Vertex>& out) {
    out.clear();
    auto y = [&](P q) {
        const float h = surface.at(q.x, q.z);
        return h == Surface::kNone ? h : h + kSpillLift;
    };
    // One painted quad a0 a1 b1 b0, a0->a1 its long side: split along that
    // side until each piece is flat to 1 cm (or 1 unit long), so flat ground
    // keeps one quad per stripe and a bend in the surface is followed.
    auto paint = [&](P a0, P a1, P b1, P b0) {
        auto lerp = [](P p, P q, float f) { return P{p.x + (q.x - p.x) * f, p.z + (q.z - p.z) * f}; };
        const float len = std::hypot(a1.x - a0.x, a1.z - a0.z);
        int steps = 1;
        for (; steps < 16 && len / (float)steps > 1.0f; steps *= 2) {
            bool flat = true;
            for (int k = 0; k < steps && flat; ++k) {
                const float f0 = (float)k / steps, f1 = (float)(k + 1) / steps;
                const P q[4] = {lerp(a0, a1, f0), lerp(a0, a1, f1), lerp(b0, b1, f1), lerp(b0, b1, f0)};
                const P mid = lerp(lerp(q[0], q[1], 0.5f), lerp(q[3], q[2], 0.5f), 0.5f);
                flat = std::fabs(y(mid) - 0.25f * (y(q[0]) + y(q[1]) + y(q[2]) + y(q[3]))) <= 0.01f;
            }
            if (flat) break;
        }
        for (int k = 0; k < steps; ++k) {
            const float f0 = (float)k / steps, f1 = (float)(k + 1) / steps;
            const P q[4] = {lerp(a0, a1, f0), lerp(a0, a1, f1), lerp(b0, b1, f1), lerp(b0, b1, f0)};
            float h[4];
            bool off = false;
            for (int m = 0; m < 4; ++m) off |= (h[m] = y(q[m])) == Surface::kNone;
            if (off) continue;  // paint never hangs past the road
            // Counter-clockwise from above whichever way the frame turned.
            const float cr = (q[1].x - q[0].x) * (q[2].z - q[0].z) -
                             (q[1].z - q[0].z) * (q[2].x - q[0].x);
            const int o[6] = {0, 1, 2, 0, 2, 3}, r[6] = {0, 2, 1, 0, 3, 2};
            for (int m = 0; m < 6; ++m) {
                const int idx = cr >= 0.0f ? o[m] : r[m];
                out.push_back({q[idx].x, h[idx], q[idx].z, 0.0f, 0.0f});
            }
        }
    };
    // A rectangle in an arm's frame: `along` from the cap (negative = inside
    // the patch), `lat` across (+ = the arm's left, which is the lane coming
    // INTO the node under right-hand traffic).
    auto quad = [&](const NodeArm& a, float al0, float al1, float la0, float la1) {
        const float nx = -a.tz, nz = a.tx;
        auto at = [&](float al, float la) {
            return P{a.capX + a.tx * al + nx * la, a.capZ + a.tz * al + nz * la};
        };
        if (std::fabs(al1 - al0) >= std::fabs(la1 - la0))
            paint(at(al0, la0), at(al1, la0), at(al1, la1), at(al0, la1));
        else
            paint(at(al0, la0), at(al0, la1), at(al1, la1), at(al1, la0));
    };
    for (const Crossing& c : plan.crossings) {
        if (c.kind != kCrossPatch || c.patchDuplicate || c.transition) continue;
        // A railway at the node (docs/roads.md "Rails and tram tracks"): no
        // zebra across the tracks, no edge line round a turnout's bed.
        bool railway = false;
        for (int r : c.roads) railway |= roads[(size_t)r].kind == 1;
        if (railway) continue;
        bool through = false;
        for (const NodeArm& a : c.armList) through |= !a.ends;
        // The road that gives way at a crossing of through roads: lower rank,
        // then narrower, then the later one (a deterministic tie-break).
        int minor = -1;
        bool anyEnds = false;
        for (const NodeArm& a : c.armList) anyEnds |= a.ends;
        if (!anyEnds)
            for (int r : c.roads) {
                if (minor < 0) { minor = r; continue; }
                const CrossingRoad& R = roads[(size_t)r];
                const CrossingRoad& M = roads[(size_t)minor];
                if (R.rank < M.rank || (R.rank == M.rank && R.width < M.width) ||
                    (R.rank == M.rank && R.width == M.width))
                    minor = r;
            }
        // EDGE LINES around the node: the road texture's own edge line (5..8
        // of 128 across the width) carried along every outline segment that
        // is a road edge or a fillet - never across a cap, where the road and
        // its painted line carry on.
        bool paintEdges = !c.shape.outlineCap.empty();
        float in0 = 0.0f, in1 = 0.0f;  // the arms' own edge lines, averaged
        for (const NodeArm& a : c.armList) {
            const CrossingRoad& R = roads[(size_t)a.road];
            paintEdges &= R.markings > kMarkNone && R.edgeLine;
            in0 += R.edgeU0 * 2.0f * a.h / (float)c.armList.size();
            in1 += R.edgeU1 * 2.0f * a.h / (float)c.armList.size();
        }
        const std::vector<float>& ring = c.shape.outline;
        const size_t pts = ring.size() / 2;
        if (paintEdges && c.shape.outlineCap.size() == pts) {
            auto pt = [&](size_t k) { return P{ring[(k % pts) * 2], ring[(k % pts) * 2 + 1]}; };
            for (size_t k = 0; k < pts; ++k) {
                if (c.shape.outlineCap[k]) continue;
                // A straight run of segments (an arm's edge walked every 2
                // units) is ONE stripe: extend while the next segment is not a
                // cap and turns by under ~1.5 degrees. paint() still splits it
                // where the ground bends.
                const P p0 = pt(k);
                size_t e = k + 1;
                P p1 = pt(e);
                while (e < pts && !c.shape.outlineCap[e % pts]) {
                    const P q = pt(e + 1);
                    const float ax = p1.x - p0.x, az = p1.z - p0.z;
                    const float bx = q.x - p1.x, bz = q.z - p1.z;
                    const float la = std::hypot(ax, az), lb = std::hypot(bx, bz);
                    if (!(la > 1e-4f) || !(lb > 1e-4f)) break;
                    if ((ax * bz - az * bx) / (la * lb) > 0.026f ||
                        (ax * bz - az * bx) / (la * lb) < -0.026f ||
                        ax * bx + az * bz <= 0.0f)
                        break;
                    ++e;
                    p1 = q;
                }
                const float dx = p1.x - p0.x, dz = p1.z - p0.z, l = std::hypot(dx, dz);
                k = e - 1;  // the loop's ++k moves to the run's last segment's end
                if (!(l > 1e-4f)) continue;
                const float nx = -dz / l, nz = dx / l;  // inward: the ring is CCW
                paint({p0.x + nx * in0, p0.z + nz * in0}, {p1.x + nx * in0, p1.z + nz * in0},
                      {p1.x + nx * in1, p1.z + nz * in1}, {p0.x + nx * in1, p0.z + nz * in1});
            }
        }
        for (const NodeArm& a : c.armList) {
            const CrossingRoad& R = roads[(size_t)a.road];
            if (R.markings <= kMarkNone) continue;
            const bool givesWay = (through && a.ends) || a.road == minor;
            if (givesWay && a.h >= 1.5f)
                quad(a, -0.7f, -0.25f, 0.15f, a.h - 0.35f);  // stop line, incoming lane
            if (R.markings >= kMarkCrossings && c.arms >= 3 && a.h >= 2.5f) {
                // Zebra: 0.5-wide stripes on a 1-unit pitch, centred on the road.
                const int n = (int)std::floor((2.0f * a.h - 1.0f) / 1.0f);
                const float first = -0.5f * (float)(n - 1);
                for (int k = 0; k < n; ++k) {
                    const float m = first + (float)k;
                    quad(a, 0.6f, 3.6f, m - 0.25f, m + 0.25f);
                }
            }
        }
    }
}

void addCrossingsToSurface(Surface& s, const std::vector<CrossingRoad>& roads,
                           const CrossingPlan& plan, const HeightFn& terrain,
                           const TerrainGrid& grid) {
    std::vector<Vertex> roadTriangles;
    for (const CrossingRoad& r : roads) {
        std::vector<Vertex> mesh;
        tessellate(r.points, r.width,
            [&](float x, float z) { return terrain(x, z) + rankLift(r.rank); },
            mesh, {}, r.sampleStep);
        roadTriangles.insert(roadTriangles.end(), mesh.begin(), mesh.end());
    }
    for (const Crossing& c : plan.crossings) {
        if (c.kind != kCrossPatch || c.patchDuplicate) continue;
        std::vector<Vertex> tris;
        tessellateJunctionSurface(c.shape, roadTriangles, terrain, c.lift, tris, grid);
        s.add(tris, c.grip);
    }
    for (const CrossingDecal& d : plan.decals) {
        std::vector<Vertex> tris;
        std::vector<float> grips;
        for (const SpillVertex& v : d.verts) {
            tris.push_back({v.x, terrain(v.x, v.z) + kLift + d.hostLift, v.z, v.u, v.v});
            grips.push_back(d.baseGrip + (d.grip - d.baseGrip) * v.a);
        }
        s.addBlended(tris, grips);
    }
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

// --- the drawn surface, for host code standing on a road --------------------

void Surface::add(const std::vector<Vertex>& triangles, float grip) {
    const size_t n = triangles.size() - triangles.size() % 3;
    tris_.insert(tris_.end(), triangles.begin(), triangles.begin() + (long)n);
    grip_.insert(grip_.end(), n, grip);
    cover_.insert(cover_.end(), n, 1.0f);
}

void Surface::addEdge(const std::vector<Vertex>& triangles, float grip,
                      const std::vector<float>& covers) {
    const size_t n = std::min(triangles.size() - triangles.size() % 3,
                              covers.size() - covers.size() % 3);
    tris_.insert(tris_.end(), triangles.begin(), triangles.begin() + (long)n);
    grip_.insert(grip_.end(), n, grip);
    cover_.insert(cover_.end(), covers.begin(), covers.begin() + (long)n);
}

void Surface::addBlended(const std::vector<Vertex>& triangles,
                         const std::vector<float>& grips) {
    const size_t n = std::min(triangles.size() - triangles.size() % 3,
                              grips.size() - grips.size() % 3);
    tris_.insert(tris_.end(), triangles.begin(), triangles.begin() + (long)n);
    grip_.insert(grip_.end(), grips.begin(), grips.begin() + (long)n);
    cover_.insert(cover_.end(), n, 1.0f);
}

void Surface::build() {
    cellStart_.clear();
    cellItems_.clear();
    nx_ = nz_ = 0;
    if (tris_.empty()) return;
    float mnx = 1e30f, mnz = 1e30f, mxx = -1e30f, mxz = -1e30f;
    for (const Vertex& v : tris_) {
        mnx = std::min(mnx, v.x);
        mnz = std::min(mnz, v.z);
        mxx = std::max(mxx, v.x);
        mxz = std::max(mxz, v.z);
    }
    // 4-unit cells: a road triangle is at most a few units across, so a cell
    // holds a few dozen candidates and a probe tests only those.
    const float cell = 4.0f;
    minX_ = mnx;
    minZ_ = mnz;
    inv_ = 1.0f / cell;
    nx_ = std::max(1, (int)((mxx - mnx) * inv_) + 1);
    nz_ = std::max(1, (int)((mxz - mnz) * inv_) + 1);
    const size_t cells = (size_t)nx_ * (size_t)nz_;
    cellStart_.assign(cells + 1, 0u);
    std::vector<unsigned> cursor;
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t t = 0; t + 2 < tris_.size(); t += 3) {
            const Vertex& a = tris_[t];
            const Vertex& b = tris_[t + 1];
            const Vertex& c = tris_[t + 2];
            const int ix0 = (int)((std::min({a.x, b.x, c.x}) - minX_) * inv_);
            const int ix1 = std::min(nx_ - 1, (int)((std::max({a.x, b.x, c.x}) - minX_) * inv_));
            const int iz0 = (int)((std::min({a.z, b.z, c.z}) - minZ_) * inv_);
            const int iz1 = std::min(nz_ - 1, (int)((std::max({a.z, b.z, c.z}) - minZ_) * inv_));
            for (int iz = std::max(0, iz0); iz <= iz1; ++iz)
                for (int ix = std::max(0, ix0); ix <= ix1; ++ix) {
                    const size_t k = (size_t)iz * (size_t)nx_ + (size_t)ix;
                    if (pass == 0)
                        ++cellStart_[k + 1];
                    else
                        cellItems_[cursor[k]++] = (unsigned)t;
                }
        }
        if (pass == 0) {
            for (size_t k = 1; k < cellStart_.size(); ++k)
                cellStart_[k] += cellStart_[k - 1];
            cellItems_.assign(cellStart_.back(), 0u);
            cursor.assign(cellStart_.begin(), cellStart_.end() - 1);
        }
    }
}

float Surface::at(float x, float z, float* grip, float* cover, float maxY) const {
    float best = kNone;
    if (grip) *grip = 1.0f;
    if (cover) *cover = 1.0f;
    if (nx_ <= 0 || x < minX_ || z < minZ_) return best;
    const int ix = (int)((x - minX_) * inv_);
    const int iz = (int)((z - minZ_) * inv_);
    if (ix >= nx_ || iz >= nz_) return best;
    const size_t k = (size_t)iz * (size_t)nx_ + (size_t)ix;
    for (unsigned e = cellStart_[k]; e < cellStart_[k + 1]; ++e) {
        const Vertex& a = tris_[cellItems_[e]];
        const Vertex& b = tris_[cellItems_[e] + 1];
        const Vertex& c = tris_[cellItems_[e] + 2];
        // The runtime's roadSurfaceAt arithmetic and tolerance, term for term.
        const float den = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::fabs(den) < 0.000001f) continue;
        const float wa = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / den;
        const float wb = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / den;
        const float wc = 1.0f - wa - wb;
        if (wa < -0.0001f || wb < -0.0001f || wc < -0.0001f) continue;
        const float y = wa * a.y + wb * b.y + wc * c.y;
        // maxY (docs/roads.md "Bridges"): the highest surface NOT above it, so
        // a car under a bridge deck stays on the road it is driving on.
        if (y > best && y <= maxY) {
            best = y;
            const size_t t = cellItems_[e];
            if (grip) *grip = wa * grip_[t] + wb * grip_[t + 1] + wc * grip_[t + 2];
            if (cover)
                *cover = wa * cover_[t] + wb * cover_[t + 1] + wc * cover_[t + 2];
        }
    }
    return best;
}

// --- kerbs (docs/roads.md "Kerbs") -------------------------------------------
//
// Host-only, like the junction patches: the codegen bakes the strips and the
// console uploads them unchanged, so there is no EE twin to keep in step.

namespace {

struct KerbPt {
    float x, z, nx, nz;
};

// Even-odd point in an XZ ring.
bool insideRing(const std::vector<P>& ring, float x, float z) {
    bool in = false;
    for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const P& a = ring[i];
        const P& b = ring[j];
        if ((a.z > z) != (b.z > z)) {
            const float xi = a.x + (z - a.z) * (b.x - a.x) / (b.z - a.z);
            if (x < xi) in = !in;
        }
    }
    return in;
}

// What a kerb may not lie on: every road's surface and every drawn patch.
struct KerbWorld {
    const std::vector<CrossingRoad>& roads;
    std::vector<Line> lines;
    std::vector<std::array<float, 4>> box;  // per road: min x, min z, max x, max z
    struct Patch {
        int crossing = -1;
        std::vector<P> ring;
        float mnx = 0, mnz = 0, mxx = 0, mxz = 0;
    };
    std::vector<Patch> patches;

    KerbWorld(const std::vector<CrossingRoad>& r, const CrossingPlan& plan) : roads(r) {
        lines.resize(roads.size());
        box.resize(roads.size(), {1e30f, 1e30f, -1e30f, -1e30f});
        for (size_t i = 0; i < roads.size(); ++i) {
            if (roads[i].points.size() < 4) continue;
            lines[i] = lineOf(roads[i].points);
            const float h = halfW((int)i) + 1.0f;
            for (const P& q : lines[i].p) {
                box[i][0] = std::min(box[i][0], q.x - h);
                box[i][1] = std::min(box[i][1], q.z - h);
                box[i][2] = std::max(box[i][2], q.x + h);
                box[i][3] = std::max(box[i][3], q.z + h);
            }
        }
        for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
            const Crossing& c = plan.crossings[ci];
            if (c.kind != kCrossPatch || c.patchDuplicate) continue;
            const std::vector<float>& o = c.shape.outline;
            if (o.size() < 6) continue;
            Patch pa;
            pa.crossing = (int)ci;
            pa.mnx = pa.mnz = 1e30f;
            pa.mxx = pa.mxz = -1e30f;
            for (size_t k = 0; k + 1 < o.size(); k += 2) {
                pa.ring.push_back({o[k], o[k + 1]});
                pa.mnx = std::min(pa.mnx, o[k]);
                pa.mxx = std::max(pa.mxx, o[k]);
                pa.mnz = std::min(pa.mnz, o[k + 1]);
                pa.mxz = std::max(pa.mxz, o[k + 1]);
            }
            patches.push_back(std::move(pa));
        }
    }
    float halfW(int i) const { return 0.5f * std::max(0.1f, roads[(size_t)i].width); }
    // On road r's surface, `margin` inside its edge.
    bool onRoad(int r, float x, float z, float margin) const {
        const Line& l = lines[(size_t)r];
        if (l.p.size() < 2) return false;
        const std::array<float, 4>& b = box[(size_t)r];
        if (x < b[0] || z < b[1] || x > b[2] || z > b[3]) return false;
        return projectOnto(l, x, z, nullptr) < halfW(r) - margin;
    }
    // A road's surface above the bare ground here (0 unless it is a bridge).
    float elevation(int r, float x, float z) const {
        return r >= 0 && roads[(size_t)r].elevation ? roads[(size_t)r].elevation(x, z) : 0.0f;
    }
    // `except` is the road the kerb belongs to (-1 = a patch, on the ground);
    // a bridge deck passing over it, or a road under the deck, is not "on".
    bool onAnyRoad(float x, float z, int except, float margin) const {
        const float ref = elevation(except, x, z);
        for (int r = 0; r < (int)roads.size(); ++r)
            if (r != except && onRoad(r, x, z, margin) &&
                std::fabs(elevation(r, x, z) - ref) <= kOverpassClearance)
                return true;
        return false;
    }
    bool inPatch(float x, float z, int exceptCrossing) const {
        for (const Patch& pa : patches) {
            if (pa.crossing == exceptCrossing) continue;
            if (x < pa.mnx || x > pa.mxx || z < pa.mnz || z > pa.mxz) continue;
            if (insideRing(pa.ring, x, z)) return true;
        }
        return false;
    }
    // The road whose edge (x, z) lies on, among `among`.
    int edgeRoad(const std::vector<int>& among, float x, float z) const {
        int best = -1;
        float bestD = 1e30f;
        for (int r : among) {
            if (lines[(size_t)r].p.size() < 2) continue;
            const float d = std::fabs(projectOnto(lines[(size_t)r], x, z, nullptr) - halfW(r));
            if (d < bestD) {
                bestD = d;
                best = r;
            }
        }
        return best;
    }
};

// Longest single kerb segment after merging: keeps a chunk's box compact and
// bounds how far one straight chord can stray from a long, gentle crest.
constexpr float kKerbMaxRun = 8.0f;

// Heights onto the drawn surface, then merge every point a chord already
// represents within kKerbTolerance (laterally and vertically).
void finishKerbPiece(KerbPiece& piece, const std::vector<KerbPt>& in, float fallbackLift,
                     const HeightFn& surface, const HeightFn& ground,
                     std::vector<KerbPiece>& out) {
    if (in.size() < 2) return;
    float len = 0.0f;
    for (size_t i = 1; i < in.size(); ++i)
        len += std::hypot(in[i].x - in[i - 1].x, in[i].z - in[i - 1].z);
    if (len < 0.05f) return;
    std::vector<float> y(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const KerbPt& q = in[i];
        // A hair inside the road (or patch): the surface the face stands on.
        float h = surface ? surface(q.x - q.nx * 0.1f, q.z - q.nz * 0.1f) : Surface::kNone;
        const float glued = (ground ? ground(q.x, q.z) : 0.0f) + kLift + fallbackLift;
        // A bridge deck overhead is not what the kerb stands on (docs/roads.md
        // "Bridges"): the surface answers the highest triangle there.
        if (!(h > -1.0e29f) || h > glued + kOverpassClearance) h = glued;
        y[i] = h;
    }
    std::vector<size_t> keep{0};
    size_t a = 0;
    while (a + 1 < in.size()) {
        size_t best = a + 1;
        for (size_t j = a + 2; j < in.size(); ++j) {
            const float cx = in[j].x - in[a].x, cz = in[j].z - in[a].z;
            const float cl = std::hypot(cx, cz);
            if (cl > kKerbMaxRun || cl < 1e-5f) break;
            bool ok = true;
            for (size_t k = a + 1; k < j && ok; ++k) {
                const float px = in[k].x - in[a].x, pz = in[k].z - in[a].z;
                const float t = (px * cx + pz * cz) / (cl * cl);
                const float lat = std::fabs(px * cz - pz * cx) / cl;
                const float dy = std::fabs(y[k] - (y[a] + (y[j] - y[a]) * t));
                ok = t > 0.0f && t < 1.0f && lat <= kKerbTolerance && dy <= kKerbTolerance &&
                     in[k].nx * in[a].nx + in[k].nz * in[a].nz > 0.995f;
            }
            if (!ok) break;
            best = j;
        }
        keep.push_back(best);
        a = best;
    }
    piece.pts.clear();
    for (size_t k : keep)
        piece.pts.insert(piece.pts.end(), {in[k].x, y[k], in[k].z, in[k].nx, in[k].nz});
    out.push_back(piece);
}

// The profile at one point: face base, face top (= the top's inner edge),
// the top's outer edge.
void kerbProfile(const KerbPiece& p, int i, KerbVertex& b, KerbVertex& t, KerbVertex& o,
                 KerbVertex& tTop) {
    const float* q = &p.pts[(size_t)i * 5];
    b = {q[0], q[1] - kKerbSink, q[2], kKerbShadeFace};
    t = {q[0], q[1] + p.height, q[2], kKerbShadeFace};
    tTop = {q[0], q[1] + p.height, q[2], kKerbShadeTop};
    o = {q[0] + q[3] * p.width, q[1] + p.height, q[2] + q[4] * p.width, kKerbShadeTop};
}

}  // namespace

std::vector<KerbPiece> planKerbs(const std::vector<CrossingRoad>& roads,
                                 const CrossingPlan& plan, const HeightFn& surface,
                                 const HeightFn& ground) {
    std::vector<KerbPiece> out;
    bool any = false;
    for (const CrossingRoad& r : roads) any |= r.kerb && r.points.size() >= 4;
    if (!any) return out;
    const KerbWorld world(roads, plan);

    // 1. Patch chains: each drawn patch's outline, minus its arm caps (where
    // a road carries on), minus whatever lies on a road or another patch. A
    // run between two caps is the kerb around one fillet (or along the far
    // side of a T); it is kept only when the roads at both its ends have kerbs.
    struct End {
        float x, z;
    };
    std::vector<End> chainEnds;
    for (const KerbWorld::Patch& pa : world.patches) {
        const Crossing& c = plan.crossings[(size_t)pa.crossing];
        bool kerbed = false;
        for (int ri : c.roads) kerbed |= roads[(size_t)ri].kerb;
        if (!kerbed) continue;
        const std::vector<P>& ring = pa.ring;
        const size_t m = ring.size();
        double area = 0.0;
        for (size_t k = 0; k < m; ++k)
            area += (double)ring[k].x * ring[(k + 1) % m].z -
                    (double)ring[(k + 1) % m].x * ring[k].z;
        const float sign = area >= 0.0 ? 1.0f : -1.0f;
        // Outward normal of segment k, and whether a kerb may stand on it.
        std::vector<float> snx(m), snz(m);
        std::vector<char> kept(m, 0);
        float widest = 0.25f;
        for (int ri : c.roads) widest = std::max(widest, roads[(size_t)ri].kerbWidth);
        for (size_t k = 0; k < m; ++k) {
            const P& a = ring[k];
            const P& b = ring[(k + 1) % m];
            const float dx = b.x - a.x, dz = b.z - a.z;
            const float l = std::hypot(dx, dz);
            if (l < 1e-5f) continue;
            snx[k] = sign * dz / l;
            snz[k] = -sign * dx / l;
            const float mx = 0.5f * (a.x + b.x), mz = 0.5f * (a.z + b.z);
            bool blocked =
                world.onAnyRoad(mx + snx[k] * 0.05f, mz + snz[k] * 0.05f, -1, 0.0f) ||
                world.onAnyRoad(mx + snx[k] * widest, mz + snz[k] * widest, -1, 0.0f) ||
                world.inPatch(mx + snx[k] * 0.05f, mz + snz[k] * 0.05f, pa.crossing);
            kept[k] = blocked ? 0 : 1;
        }
        size_t start = m;
        for (size_t k = 0; k < m; ++k)
            if (kept[k] && !kept[(k + m - 1) % m]) {
                start = k;
                break;
            }
        const bool ringWhole =
            start == m && std::all_of(kept.begin(), kept.end(), [](char v) { return v != 0; });
        if (start == m && !ringWhole) continue;
        if (ringWhole) start = 0;
        auto vertexNormal = [&](size_t k, bool first, bool last, float* nx, float* nz) {
            // Chain ends take their one segment's normal; interior points the
            // mean of both, so the top's outer edge stays parallel round a curve.
            const size_t prev = (k + m - 1) % m;
            float x = 0.0f, z = 0.0f;
            if (!first || ringWhole) x += snx[prev], z += snz[prev];
            if (!last || ringWhole) x += snx[k % m], z += snz[k % m];
            const float l = std::hypot(x, z);
            *nx = l > 1e-6f ? x / l : snx[k % m];
            *nz = l > 1e-6f ? z / l : snz[k % m];
        };
        std::vector<char> used(m, 0);
        for (size_t s0 = start, guard = 0; guard < m; ++guard, s0 = (s0 + 1) % m) {
            if (!kept[s0] || used[s0]) continue;
            if (!ringWhole && kept[(s0 + m - 1) % m]) continue;  // not a run start
            // The run s0 .. s1 (segments), points s0 .. s1 + 1.
            size_t count = 0;
            while (count < m && kept[(s0 + count) % m]) used[(s0 + count) % m] = 1, ++count;
            const P& pa0 = ring[s0];
            const P& pa1 = ring[(s0 + count) % m];
            int r0 = -1, r1 = -1;
            if (ringWhole) {
                bool all = true;
                for (int ri : c.roads) all &= roads[(size_t)ri].kerb;
                if (!all) continue;
                r0 = r1 = c.roads[0];
            } else {
                r0 = world.edgeRoad(c.roads, pa0.x, pa0.z);
                r1 = world.edgeRoad(c.roads, pa1.x, pa1.z);
                if (r0 < 0 || r1 < 0 || !roads[(size_t)r0].kerb || !roads[(size_t)r1].kerb)
                    continue;
                chainEnds.push_back({pa0.x, pa0.z});
                chainEnds.push_back({pa1.x, pa1.z});
            }
            std::vector<KerbPt> pts;
            for (size_t q = 0; q <= count; ++q) {
                const size_t k = (s0 + q) % m;
                float nx, nz;
                vertexNormal(k, q == 0, q == count, &nx, &nz);
                if (q > 0) {
                    // Subdivide long segments so the heights follow the patch.
                    const P& a = ring[(k + m - 1) % m];
                    const P& b = ring[k];
                    const int steps = (int)std::ceil(std::hypot(b.x - a.x, b.z - a.z) / 1.0f);
                    const size_t seg = (k + m - 1) % m;
                    for (int st = 1; st < steps; ++st) {
                        const float f = (float)st / (float)steps;
                        pts.push_back({a.x + (b.x - a.x) * f, a.z + (b.z - a.z) * f, snx[seg],
                                       snz[seg]});
                    }
                }
                pts.push_back({ring[k].x, ring[k].z, nx, nz});
            }
            KerbPiece piece;
            piece.road = r0;
            piece.node = pa.crossing;
            piece.height = roads[(size_t)r0].kerbHeight;
            piece.width = roads[(size_t)r0].kerbWidth;
            finishKerbPiece(piece, pts, rankLift(roads[(size_t)r0].rank), surface, ground,
                            out);
        }
    }

    // 2. Road edges: both sides of every kerbed road, cut wherever the kerb
    // would enter a patch or lie on another road. A cut is placed by
    // bisection and snapped onto the patch chain's end when one is that close,
    // so the road's kerb and the fillet's meet in one point.
    for (int ri = 0; ri < (int)roads.size(); ++ri) {
        const CrossingRoad& R = roads[(size_t)ri];
        if (!R.kerb || R.points.size() < 4) continue;
        std::vector<std::vector<Vertex>> rows;
        int cs = 1;
        buildRows(R.points, R.width, nullptr, R.sampleStep, rows, &cs);
        if (rows.size() < 2) continue;
        for (int side = 0; side < 2; ++side) {
            std::vector<KerbPt> st;
            for (const std::vector<Vertex>& row : rows) {
                const Vertex& e = side == 0 ? row[0] : row[(size_t)cs];
                const Vertex& o = side == 0 ? row[(size_t)cs] : row[0];
                float nx = e.x - o.x, nz = e.z - o.z;
                const float l = std::hypot(nx, nz);
                if (l > 1e-6f) nx /= l, nz /= l;
                st.push_back({e.x, e.z, nx, nz});
            }
            auto blocked = [&](const KerbPt& q) {
                return world.inPatch(q.x - q.nx * 0.05f, q.z - q.nz * 0.05f, -1) ||
                       world.onAnyRoad(q.x + q.nx * 0.02f, q.z + q.nz * 0.02f, ri, 0.0f) ||
                       world.onAnyRoad(q.x + q.nx * R.kerbWidth, q.z + q.nz * R.kerbWidth, ri,
                                       0.0f);
            };
            auto lerpPt = [](const KerbPt& a, const KerbPt& b, float f) {
                KerbPt q{a.x + (b.x - a.x) * f, a.z + (b.z - a.z) * f, a.nx + (b.nx - a.nx) * f,
                         a.nz + (b.nz - a.nz) * f};
                const float l = std::hypot(q.nx, q.nz);
                if (l > 1e-6f) q.nx /= l, q.nz /= l;
                return q;
            };
            // The last free point between a free `in` and a blocked `out`.
            auto boundary = [&](const KerbPt& in, const KerbPt& outPt) {
                float lo = 0.0f, hi = 1.0f;
                for (int it = 0; it < 14; ++it) {
                    const float mid = 0.5f * (lo + hi);
                    if (blocked(lerpPt(in, outPt, mid))) hi = mid; else lo = mid;
                }
                KerbPt q = lerpPt(in, outPt, lo);
                float bestD = 0.35f;
                for (const End& e : chainEnds) {
                    const float d = std::hypot(e.x - q.x, e.z - q.z);
                    if (d < bestD) {
                        bestD = d;
                        q.x = e.x;
                        q.z = e.z;
                    }
                }
                return q;
            };
            std::vector<char> blk(st.size());
            for (size_t i = 0; i < st.size(); ++i) blk[i] = blocked(st[i]) ? 1 : 0;
            std::vector<KerbPt> cur;
            auto flush = [&]() {
                KerbPiece piece;
                piece.road = ri;
                piece.height = R.kerbHeight;
                piece.width = R.kerbWidth;
                finishKerbPiece(piece, cur, rankLift(R.rank), surface, ground, out);
                cur.clear();
            };
            for (size_t i = 0; i < st.size(); ++i) {
                if (!blk[i]) {
                    if (cur.empty() && i > 0) cur.push_back(boundary(st[i], st[i - 1]));
                    cur.push_back(st[i]);
                } else if (!cur.empty()) {
                    cur.push_back(boundary(st[i - 1], st[i]));
                    flush();
                }
            }
            if (!cur.empty()) flush();
        }
    }
    return out;
}

void kerbTriangles(const KerbPiece& p, std::vector<KerbVertex>& out) {
    for (int i = 0; i + 1 < p.points(); ++i) {
        KerbVertex b0, t0, o0, u0, b1, t1, o1, u1;
        kerbProfile(p, i, b0, t0, o0, u0);
        kerbProfile(p, i + 1, b1, t1, o1, u1);
        out.insert(out.end(), {b0, b1, t1, b0, t1, t0});  // the face
        out.insert(out.end(), {u0, u1, o1, u0, o1, o0});  // the top
    }
}

void addKerbsToSurface(Surface& s, const std::vector<CrossingRoad>& roads,
                       const CrossingPlan& plan, const HeightFn& ground) {
    bool any = false;
    for (const CrossingRoad& r : roads) any |= r.kerb;
    if (!any) return;
    s.build();
    const std::vector<KerbPiece> pieces =
        planKerbs(roads, plan, [&](float x, float z) { return s.at(x, z); }, ground);
    std::vector<KerbVertex> kv;
    for (const KerbPiece& p : pieces) kerbTriangles(p, kv);
    std::vector<Vertex> tris;
    for (size_t i = 0; i + 2 < kv.size(); i += 3) {
        const KerbVertex &a = kv[i], &b = kv[i + 1], &c = kv[i + 2];
        // A face has no area in XZ: the runtime's barycentric test skips it.
        const float area = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        if (std::fabs(area) < 1e-6f) continue;
        for (const KerbVertex* v : {&a, &b, &c}) tris.push_back({v->x, v->y, v->z, 0.0f, 0.0f});
    }
    s.add(tris, 1.0f);
    // The pavements behind them are drawn surface as well.
    addPavementsToSurface(s, planPavements(roads, plan, pieces, ground));
}

std::vector<PavementMesh> planPavements(const std::vector<CrossingRoad>& roads,
                                        const CrossingPlan& plan,
                                        const std::vector<KerbPiece>& kerbs,
                                        const HeightFn& ground) {
    std::vector<PavementMesh> out;
    bool any = false;
    for (const KerbPiece& k : kerbs)
        any |= k.road >= 0 && roads[(size_t)k.road].pavement > 0.01f;
    if (!any) return out;
    const KerbWorld world(roads, plan);
    // Clear of every road by `clear` beyond its edge (a pavement of its own
    // stops halfway, so two pavements meet instead of overlapping), and of
    // every patch.
    auto blocked = [&](float x, float z, float clear) {
        for (int r = 0; r < (int)roads.size(); ++r) {
            const Line& l = world.lines[(size_t)r];
            if (l.p.size() < 2) continue;
            const std::array<float, 4>& b = world.box[(size_t)r];
            const float pad = clear + 1.0f;
            if (x < b[0] - pad || z < b[1] - pad || x > b[2] + pad || z > b[3] + pad) continue;
            if (projectOnto(l, x, z, nullptr) < world.halfW(r) + clear) return true;
        }
        return world.inPatch(x, z, -1);
    };
    // No terrain answers a very low height (the codegen's -1e6): then there
    // is nothing to rise onto and no face to drop.
    auto groundAt = [&](float x, float z) {
        const float g = ground ? ground(x, z) : -1.0e30f;
        return g > -1.0e5f ? g : -1.0e30f;
    };
    std::map<std::pair<int, std::pair<int, int>>, size_t> slot;  // (road, cell) -> out

    for (const KerbPiece& kp : kerbs) {
        if (kp.road < 0) continue;
        const CrossingRoad& R = roads[(size_t)kp.road];
        const float want = std::min(R.pavement, kPavementMax);
        if (want <= 0.01f || kp.points() < 2) continue;
        // 1. Resample the kerb line at most 2 units apart: the merged kerb
        // points can be 8 apart, and the ground under a wide slab is not as
        // straight as the road edge.
        struct Q {
            float x, y, z, nx, nz;  // the kerb top's OUTER edge, its height
        };
        std::vector<Q> q;
        for (int i = 0; i < kp.points(); ++i) {
            const float* a = &kp.pts[(size_t)i * 5];
            Q v{a[0] + a[3] * kp.width, a[1] + kp.height, a[2] + a[4] * kp.width, a[3], a[4]};
            if (i > 0) {
                const Q& p = q.back();
                const float d = std::hypot(v.x - p.x, v.z - p.z);
                const int steps = (int)std::ceil(d / 2.0f);
                const Q p0 = p;
                for (int s = 1; s < steps; ++s) {
                    const float f = (float)s / (float)steps;
                    Q m{p0.x + (v.x - p0.x) * f, p0.y + (v.y - p0.y) * f, p0.z + (v.z - p0.z) * f,
                        p0.nx + (v.nx - p0.nx) * f, p0.nz + (v.nz - p0.nz) * f};
                    const float l = std::hypot(m.nx, m.nz);
                    if (l > 1e-6f) m.nx /= l, m.nz /= l;
                    q.push_back(m);
                }
            }
            q.push_back(v);
        }
        const size_t n = q.size();
        // 2. The outer edge: straight offsets, narrowed off other roads and
        // patches (bisection along the normal).
        std::vector<float> w(n, want);
        for (size_t i = 0; i < n; ++i) {
            const float clear = 0.5f * (want + kp.width);
            if (!blocked(q[i].x + q[i].nx * w[i], q[i].z + q[i].nz * w[i], clear) &&
                !blocked(q[i].x + q[i].nx * w[i] * 0.5f, q[i].z + q[i].nz * w[i] * 0.5f, clear))
                continue;
            float lo = 0.0f, hi = w[i];
            for (int it = 0; it < 12; ++it) {
                const float mid = 0.5f * (lo + hi);
                if (blocked(q[i].x + q[i].nx * mid, q[i].z + q[i].nz * mid, clear)) hi = mid;
                else lo = mid;
            }
            w[i] = lo;
        }
        // A width may change by at most one unit per unit along, so a cut
        // reads as a taper, not a notch.
        std::vector<float> arc(n, 0.0f);
        for (size_t i = 1; i < n; ++i)
            arc[i] = arc[i - 1] + std::hypot(q[i].x - q[i - 1].x, q[i].z - q[i - 1].z);
        for (size_t i = 1; i < n; ++i) w[i] = std::min(w[i], w[i - 1] + (arc[i] - arc[i - 1]));
        for (size_t i = n - 1; i-- > 0;) w[i] = std::min(w[i], w[i + 1] + (arc[i + 1] - arc[i]));
        std::vector<float> ox(n), oz(n);
        for (size_t i = 0; i < n; ++i) {
            ox[i] = q[i].x + q[i].nx * w[i];
            oz[i] = q[i].z + q[i].nz * w[i];
        }
        // 3. A tight corner (the inside of a fillet): the offsets fold over.
        // Each folded run closes in the point where the straight offsets
        // either side of it meet - the block's corner.
        for (size_t i = 0; i + 1 < n;) {
            const float ix = q[i + 1].x - q[i].x, iz = q[i + 1].z - q[i].z;
            const float fx = ox[i + 1] - ox[i], fz = oz[i + 1] - oz[i];
            if (fx * ix + fz * iz > 0.15f * (ix * ix + iz * iz)) {
                ++i;
                continue;
            }
            size_t a = i, b = i + 1;
            while (b + 1 < n) {
                const float jx = q[b + 1].x - q[b].x, jz = q[b + 1].z - q[b].z;
                const float gx = ox[b + 1] - ox[b], gz = oz[b + 1] - oz[b];
                if (gx * jx + gz * jz > 0.15f * (jx * jx + jz * jz)) break;
                ++b;
            }
            // Tangents just outside the run.
            const size_t a0 = a > 0 ? a - 1 : a, b1 = b + 1 < n ? b + 1 : b;
            float t0x = q[a].x - q[a0].x, t0z = q[a].z - q[a0].z;
            if (a0 == a) t0x = -q[a].nz, t0z = q[a].nx;
            float t1x = q[b1].x - q[b].x, t1z = q[b1].z - q[b].z;
            if (b1 == b) t1x = -q[b].nz, t1z = q[b].nx;
            const float den = t0x * t1z - t0z * t1x;
            if (std::fabs(den) > 1e-6f) {
                const float dx = ox[b] - ox[a], dz = oz[b] - oz[a];
                const float s = (dx * t1z - dz * t1x) / den;
                const float kx = ox[a] + t0x * s, kz = oz[a] + t0z * s;
                // Only a meeting point in front of both, and on clear ground.
                const bool ahead = (kx - ox[a]) * t0x + (kz - oz[a]) * t0z >= -0.01f &&
                                   (ox[b] - kx) * t1x + (oz[b] - kz) * t1z >= -0.01f;
                if (ahead && !blocked(kx, kz, 0.0f))
                    for (size_t k = a; k <= b; ++k) ox[k] = kx, oz[k] = kz;
                else
                    for (size_t k = a; k <= b; ++k) ox[k] = q[k].x, oz[k] = q[k].z;  // none
            }
            i = b + 1;
        }
        // 4. Heights: flat at the kerb top, raised onto higher ground; the
        // outer face drops to the ground under the outer edge.
        std::vector<float> yo(n), go(n), yi(n);
        for (size_t i = 0; i < n; ++i) {
            const float gi = groundAt(q[i].x, q[i].z);
            yi[i] = std::max(q[i].y, gi + kPavementLift);
            go[i] = groundAt(ox[i], oz[i]);
            const float gm = groundAt(0.5f * (q[i].x + ox[i]), 0.5f * (q[i].z + oz[i]));
            // The middle too: a slab is two vertices across, so the ground
            // under its middle must not poke through.
            const float raise = std::max(go[i], 2.0f * gm - gi) + kPavementLift;
            yo[i] = std::max(q[i].y, raise);
            yi[i] = std::max(yi[i], q[i].y);
        }
        // 5. Triangles, binned into kKerbCell cells by segment midpoint.
        for (size_t i = 0; i + 1 < n; ++i) {
            const float wi0 = std::hypot(ox[i] - q[i].x, oz[i] - q[i].z);
            const float wi1 = std::hypot(ox[i + 1] - q[i + 1].x, oz[i + 1] - q[i + 1].z);
            if (wi0 < 0.02f && wi1 < 0.02f) continue;
            const float mx = 0.25f * (q[i].x + q[i + 1].x + ox[i] + ox[i + 1]);
            const float mz = 0.25f * (q[i].z + q[i + 1].z + oz[i] + oz[i + 1]);
            const std::pair<int, std::pair<int, int>> key{
                kp.road, {(int)std::floor(mx / kKerbCell), (int)std::floor(mz / kKerbCell)}};
            auto it = slot.find(key);
            if (it == slot.end()) {
                it = slot.emplace(key, out.size()).first;
                PavementMesh pm;
                pm.road = kp.road;
                pm.cellX = key.second.first;
                pm.cellZ = key.second.second;
                out.push_back(std::move(pm));
            }
            std::vector<Vertex>& t = out[it->second].tris;
            const float v0 = arc[i] / kPavementTile, v1 = arc[i + 1] / kPavementTile;
            const Vertex a{q[i].x, yi[i], q[i].z, 0.0f, v0};
            const Vertex b{q[i + 1].x, yi[i + 1], q[i + 1].z, 0.0f, v1};
            const Vertex c{ox[i + 1], yo[i + 1], oz[i + 1], wi1 / kPavementTile, v1};
            const Vertex d{ox[i], yo[i], oz[i], wi0 / kPavementTile, v0};
            t.insert(t.end(), {a, b, c, a, c, d});
            // The outer face, where the slab stands above the ground.
            const float h0 = yo[i] - go[i], h1 = yo[i + 1] - go[i + 1];
            if ((h0 > 0.03f || h1 > 0.03f) && go[i] > -1.0e29f && go[i + 1] > -1.0e29f) {
                const float fb0 = go[i] - kPavementSink, fb1 = go[i + 1] - kPavementSink;
                const Vertex e{ox[i], fb0, oz[i], (wi0 + h0) / kPavementTile, v0};
                const Vertex f{ox[i + 1], fb1, oz[i + 1], (wi1 + h1) / kPavementTile, v1};
                t.insert(t.end(), {d, c, f, d, f, e});
            }
        }
    }
    // Small UVs: the physical PS2's ST path wants them near zero (the road
    // chunks' integer rebase rule).
    for (PavementMesh& pm : out) {
        float mu = 1e30f, mv = 1e30f;
        for (const Vertex& v : pm.tris) mu = std::min(mu, v.u), mv = std::min(mv, v.v);
        mu = std::floor(mu);
        mv = std::floor(mv);
        for (Vertex& v : pm.tris) v.u -= mu, v.v -= mv;
    }
    return out;
}

void addPavementsToSurface(Surface& s, const std::vector<PavementMesh>& meshes) {
    std::vector<Vertex> tris;
    for (const PavementMesh& pm : meshes)
        for (size_t i = 0; i + 2 < pm.tris.size(); i += 3) {
            const Vertex &a = pm.tris[i], &b = pm.tris[i + 1], &c = pm.tris[i + 2];
            const float area = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
            if (std::fabs(area) < 1e-6f) continue;
            tris.insert(tris.end(), {a, b, c});
        }
    if (!tris.empty()) s.add(tris, 1.0f);
}

void kerbStrips(const std::vector<KerbPiece>& pieces, std::vector<KerbVertex>& out,
                std::vector<int>& chunkSizes) {
    out.clear();
    chunkSizes.clear();
    // Pieces cut into kKerbCell cells by segment midpoint; one cell's pieces
    // share chunks, so a chunk's box stays about a cell wide and the frustum
    // and the draw distance reject kerbs a street at a time.
    struct Sub {
        size_t piece;
        int first, last;  // point range, inclusive
    };
    std::map<std::pair<int, int>, std::vector<Sub>> cells;
    for (size_t pi = 0; pi < pieces.size(); ++pi) {
        const KerbPiece& p = pieces[pi];
        const int n = p.points();
        if (n < 2) continue;
        auto cellOf = [&](int seg) {
            const float* a = &p.pts[(size_t)seg * 5];
            const float* b = &p.pts[(size_t)(seg + 1) * 5];
            return std::make_pair((int)std::floor(0.5f * (a[0] + b[0]) / kKerbCell),
                                  (int)std::floor(0.5f * (a[2] + b[2]) / kKerbCell));
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

    // The run contract of tessellateStrips (and meshstrip): runs of exactly
    // kStripRun, a run that fills mid-strip carries its last two vertices
    // over, unrelated strips join by repeating a vertex either side, and a
    // chunk's last run is padded to a multiple of 3.
    size_t chunkStart = 0, runStart = 0;
    bool open = false;
    auto runLen = [&]() { return out.size() - runStart; };
    auto pushRaw = [&](const KerbVertex& v) {
        if (runLen() == (size_t)kStripRun) {
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
        for (const Sub& s : subs) {
            const KerbPiece& p = pieces[s.piece];
            const int m = s.last - s.first + 1;
            // Two strips of 2m vertices, two joins, and the run carry-overs.
            const size_t cost = (size_t)(4 * m + 4) + (size_t)(4 * m + 4) / (kStripRun - 2) * 2 + 2;
            if (open && (out.size() - chunkStart) + cost > (size_t)kChunkBudget) closeChunk();
            open = true;
            KerbVertex b, t, o, u;
            // The face: base, top, base, top ... along the line.
            for (int i = s.first; i <= s.last; ++i) {
                kerbProfile(p, i, b, t, o, u);
                if (i == s.first) startStrip(b); else pushRaw(b);
                pushRaw(t);
            }
            // The top: inner, outer, inner, outer ...
            for (int i = s.first; i <= s.last; ++i) {
                kerbProfile(p, i, b, t, o, u);
                if (i == s.first) startStrip(u); else pushRaw(u);
                pushRaw(o);
            }
        }
        closeChunk();
    }
}

}  // namespace roadgen
