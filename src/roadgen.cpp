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
                const HeightFn& height, float sampleStep,
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
                q.u = u;
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
                 const std::vector<float>& lifts, float sampleStep) {
    (void)lifts;  // legacy project field; roads are always terrain-projected
    out.clear();
    std::vector<std::vector<Vertex>> rows;
    int crossSteps = 1;
    const float arc =
        buildRows(pointsXZ, width, height, sampleStep, rows, &crossSteps);

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
                       std::vector<int>* chunkSizes, float sampleStep) {
    static_assert(kStripRun == (int)meshstrip::kRun,
                  "the road run must be the run the .tmdl bake uses - both are "
                  "the smallest package a static program class derives");
    out.clear();
    if (chunkSizes != nullptr) chunkSizes->clear();
    std::vector<std::vector<Vertex>> rows;
    int crossSteps = 1;
    const float arc =
        buildRows(pointsXZ, width, height, sampleStep, rows, &crossSteps);

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
    for (int k = 0; k < 4; ++k) {
        const int n = (k + 1) & 3;
        const float ax = j.cornerXZ[k * 2], az = j.cornerXZ[k * 2 + 1];
        const float bx = j.cornerXZ[n * 2], bz = j.cornerXZ[n * 2 + 1];
        const Vertex a{ax, (height ? height(ax, az) : 0.0f) + kLift + 0.02f,
                       az, 0.5f + (ax - j.x) / 32.0f,
                       0.5f + (az - j.z) / 32.0f};
        const Vertex b{bx, (height ? height(bx, bz) : 0.0f) + kLift + 0.02f,
                       bz, 0.5f + (bx - j.x) / 32.0f,
                       0.5f + (bz - j.z) / 32.0f};
        out.push_back(center); out.push_back(a); out.push_back(b);
    }
}

void tessellateSpill(const std::vector<float>& lowPts, float lowWidth,
                     float lowSampleStep, const std::vector<float>& highPts,
                     float highWidth, float spill, std::vector<SpillVertex>& out) {
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
    std::vector<Vertex> tris;
    tessellate(lowPts, lowWidth, nullptr, tris, {}, lowSampleStep);
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        float d[3], a[3];
        bool anyInside = false, anyVisible = false;
        for (int k = 0; k < 3; ++k) {
            d[k] = inside(tris[t + k].x, tris[t + k].z);
            // 1 at (and beyond) the edge, 0 `spill` units in.
            a[k] = d[k] <= 0.0f ? 1.0f : std::clamp(1.0f - d[k] / spill, 0.0f, 1.0f);
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
}

void Surface::addBlended(const std::vector<Vertex>& triangles,
                         const std::vector<float>& grips) {
    const size_t n = std::min(triangles.size() - triangles.size() % 3,
                              grips.size() - grips.size() % 3);
    tris_.insert(tris_.end(), triangles.begin(), triangles.begin() + (long)n);
    grip_.insert(grip_.end(), grips.begin(), grips.begin() + (long)n);
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

float Surface::at(float x, float z, float* grip) const {
    float best = kNone;
    if (grip) *grip = 1.0f;
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
        if (y > best) {
            best = y;
            if (grip) {
                const size_t t = cellItems_[e];
                *grip = wa * grip_[t] + wb * grip_[t + 1] + wc * grip_[t + 2];
            }
        }
    }
    return best;
}

}  // namespace roadgen
