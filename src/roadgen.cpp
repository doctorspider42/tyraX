#include "roadgen.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

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

}  // namespace

float tessellate(const std::vector<float>& pointsXZ, float width,
                 const HeightFn& height, std::vector<Vertex>& out,
                 const std::vector<float>& lifts) {
    (void)lifts;  // legacy project field; roads are always terrain-projected
    out.clear();
    const int n = (int)(pointsXZ.size() / 2);
    if (n < 2) return 0.0f;
    const float hw = 0.5f * (width > 0.1f ? width : 0.1f);

    // Sample the whole spline first: a terrain-projected row per station,
    // the right vector from the local tangent, V from the running arc. The
    // lateral subdivisions matter as much as the longitudinal ones: an edge-
    // only strip is one plane across the full width and terrain can pierce it
    // between the shoulders.
    const int crossSteps = std::max(
        1, (int)std::ceil((hw * 2.0f) / kCrossSampleStep));
    std::vector<std::vector<Vertex>> rows;
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

    // Stitch every lateral cell. Wound counter-clockwise seen from above
    // (+Y), the terrain's own convention.
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
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
