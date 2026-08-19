#include "roadgen.hpp"

#include <cmath>

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
                 const HeightFn& height, std::vector<Vertex>& out) {
    out.clear();
    const int n = (int)(pointsXZ.size() / 2);
    if (n < 2) return 0.0f;
    const float hw = 0.5f * (width > 0.1f ? width : 0.1f);

    // Sample the whole spline first: pairs of edge vertices per station,
    // the right vector from the local tangent, V from the running arc.
    std::vector<Vertex> left, right;
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
            // Tangent from a small step ahead (cheap and stable at joints).
            const P c2 = t + 0.05f <= 1.0f ? sample(pointsXZ, seg, t + 0.05f)
                                           : sample(pointsXZ, seg + 1, 0.05f);
            float tx = c2.x - c.x, tz = c2.z - c.z;
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
            Vertex l, r;
            l.x = c.x - rx;
            l.z = c.z - rz;
            l.y = (height ? height(l.x, l.z) : 0.0f) + kLift;
            l.u = 0.0f;
            l.v = v;
            r.x = c.x + rx;
            r.z = c.z + rz;
            r.y = (height ? height(r.x, r.z) : 0.0f) + kLift;
            r.u = 1.0f;
            r.v = v;
            left.push_back(l);
            right.push_back(r);
        }
    }

    // Stitch: two triangles per station pair. Wound counter-clockwise seen
    // from above (+Y), the terrain's own convention.
    for (size_t i = 0; i + 1 < left.size(); ++i) {
        out.push_back(left[i]);
        out.push_back(right[i]);
        out.push_back(right[i + 1]);
        out.push_back(left[i]);
        out.push_back(right[i + 1]);
        out.push_back(left[i + 1]);
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
