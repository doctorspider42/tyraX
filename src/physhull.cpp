#include "physhull.hpp"

#include <algorithm>
#include <cmath>

namespace physhull {
namespace {

struct P3 {
    double x, y, z;
};
P3 sub(const P3& a, const P3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
P3 add(const P3& a, const P3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
P3 mul(const P3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(const P3& a, const P3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P3 cross(const P3& a, const P3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double len(const P3& a) { return std::sqrt(dot(a, a)); }

// The support directions, in PRIORITY order: the greedy pick below keeps the
// first kMaxVerts distinct points, so the directions a body is most likely to
// rest on come first - the six axes, the eight box corners (a stool's feet, a
// crate's corners), the twelve edge diagonals, then a Fibonacci sphere to fill
// whatever budget is left on a rounded shape.
std::vector<P3> directions() {
    std::vector<P3> d;
    const double axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                               {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& a : axes) d.push_back({a[0], a[1], a[2]});
    for (int sy = -1; sy <= 1; sy += 2)  // the downward corners first
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2) d.push_back({(double)sx, (double)-sy, (double)sz});
    for (int a = 0; a < 3; ++a)
        for (int s1 = -1; s1 <= 1; s1 += 2)
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                P3 v{0, 0, 0};
                double* c[3] = {&v.x, &v.y, &v.z};
                *c[(a + 1) % 3] = s1;
                *c[(a + 2) % 3] = s2;
                d.push_back(v);
            }
    const int n = 160;
    const double golden = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; ++i) {
        const double y = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(1.0 - y * y);
        const double t = golden * i;
        d.push_back({r * std::cos(t), y, r * std::sin(t)});
    }
    for (P3& v : d) v = mul(v, 1.0 / len(v));
    return d;
}

}  // namespace

std::vector<float> positionsOf(const std::vector<float>& verts8) {
    std::vector<float> out;
    out.reserve(verts8.size() / 8 * 3);
    for (size_t i = 0; i + 2 < verts8.size(); i += 8) {
        out.push_back(verts8[i]);
        out.push_back(verts8[i + 1]);
        out.push_back(verts8[i + 2]);
    }
    return out;
}

Hull build(const std::vector<float>& points) {
    Hull h;
    const size_t n = points.size() / 3;
    if (n < 1) return h;

    std::vector<P3> pts(n);
    P3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
    for (size_t i = 0; i < n; ++i) {
        pts[i] = {points[i * 3], points[i * 3 + 1], points[i * 3 + 2]};
        mn = {std::min(mn.x, pts[i].x), std::min(mn.y, pts[i].y), std::min(mn.z, pts[i].z)};
        mx = {std::max(mx.x, pts[i].x), std::max(mx.y, pts[i].y), std::max(mx.z, pts[i].z)};
    }
    const P3 ext = sub(mx, mn);
    const double big = std::max(ext.x, std::max(ext.y, ext.z));
    if (!(big > 1e-9)) return h;

    // Thicken a flat cloud: a Plane primitive or a single card has no volume,
    // hence no inertia and no inside for a contact to be pushed out of.
    const double thin = 0.02 * big;
    {
        const double e[3] = {ext.x, ext.y, ext.z};
        for (int a = 0; a < 3; ++a) {
            if (e[a] >= thin) continue;
            const size_t m = pts.size();
            for (size_t i = 0; i < m; ++i) {
                P3 lo = pts[i], hi = pts[i];
                double* l[3] = {&lo.x, &lo.y, &lo.z};
                double* u[3] = {&hi.x, &hi.y, &hi.z};
                *l[a] -= 0.5 * thin;
                *u[a] += 0.5 * thin;
                pts[i] = lo;
                pts.push_back(hi);
            }
        }
    }
    const double diag = len(sub(mx, mn)) + thin;
    const double weld = 1e-3 * diag;

    // Greedy support-point pick.
    std::vector<P3> hv;
    for (const P3& d : directions()) {
        if ((int)hv.size() >= kMaxVerts) break;
        size_t best = 0;
        double bd = -1e300;
        for (size_t i = 0; i < pts.size(); ++i) {
            const double s = dot(pts[i], d);
            if (s > bd + 1e-12) bd = s, best = i;
        }
        bool dup = false;
        for (const P3& q : hv)
            if (len(sub(q, pts[best])) < weld) {
                dup = true;
                break;
            }
        if (!dup) hv.push_back(pts[best]);
    }
    if (hv.size() < 4) return h;

    // Faces by brute force - at most C(24, 3) = 2024 candidate planes over 24
    // points, trivially cheap on the host and immune to the degeneracies an
    // incremental hull has to special-case. Coplanar triples collapse into one
    // plane by the dedupe.
    const double eps = 1e-4 * diag;
    struct Plane {
        P3 n;
        double d;
    };
    std::vector<Plane> planes;
    const int m = (int)hv.size();
    for (int a = 0; a < m; ++a)
        for (int b = a + 1; b < m; ++b)
            for (int c = b + 1; c < m; ++c) {
                P3 nrm = cross(sub(hv[b], hv[a]), sub(hv[c], hv[a]));
                const double l = len(nrm);
                if (l < 1e-12 * diag * diag) continue;
                nrm = mul(nrm, 1.0 / l);
                double d = dot(nrm, hv[a]);
                int above = 0, below = 0;
                for (int k = 0; k < m; ++k) {
                    const double s = dot(nrm, hv[k]) - d;
                    if (s > eps) ++above;
                    else if (s < -eps) ++below;
                }
                if (above && below) continue;
                if (above) nrm = mul(nrm, -1.0), d = -d;
                bool dup = false;
                for (const Plane& p : planes)
                    if (dot(p.n, nrm) > 1.0 - 1e-6 && std::fabs(p.d - d) < eps) {
                        dup = true;
                        break;
                    }
                if (!dup) planes.push_back({nrm, d});
            }
    if (planes.size() < 4) return h;
    // Only CORNERS stay: a support point in the middle of a face or an edge
    // (a subdivided plane's centre vertex) is not a vertex of the hull, and
    // as a fan point it would scramble the face ordering below.
    {
        std::vector<P3> corners;
        for (const P3& q : hv) {
            int on = 0;
            for (const Plane& pl : planes)
                if (std::fabs(dot(pl.n, q) - pl.d) <= eps) ++on;
            if (on >= 3) corners.push_back(q);
        }
        hv.swap(corners);
    }
    if (hv.size() < 4) return h;

    // Mass properties: fan each face's polygon to an interior reference point
    // and sum the solid tetrahedra. For a tetrahedron (o, a, b, c) with the
    // vertices taken relative to o: integral of x x^T dV =
    // V/20 * (a a^T + b b^T + c c^T + s s^T), s = a + b + c.
    P3 o{0, 0, 0};
    for (const P3& q : hv) o = add(o, q);
    o = mul(o, 1.0 / hv.size());
    double V = 0, M1[3] = {0, 0, 0}, C[6] = {0, 0, 0, 0, 0, 0};
    for (const Plane& pl : planes) {
        std::vector<P3> poly;
        for (const P3& q : hv)
            if (std::fabs(dot(pl.n, q) - pl.d) <= eps) poly.push_back(q);
        if (poly.size() < 3) continue;
        P3 cen{0, 0, 0};
        for (const P3& q : poly) cen = add(cen, q);
        cen = mul(cen, 1.0 / poly.size());
        P3 u = sub(poly[0], cen);
        if (len(u) < 1e-12) u = sub(poly[1], cen);
        u = mul(u, 1.0 / len(u));
        const P3 v = cross(pl.n, u);
        std::sort(poly.begin(), poly.end(), [&](const P3& p, const P3& q) {
            const P3 a = sub(p, cen), b = sub(q, cen);
            return std::atan2(dot(a, v), dot(a, u)) < std::atan2(dot(b, v), dot(b, u));
        });
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
            const P3 a = sub(poly[0], o), b = sub(poly[i], o), c = sub(poly[i + 1], o);
            const double vt = std::fabs(dot(a, cross(b, c))) / 6.0;
            if (vt <= 0) continue;
            const P3 s = add(add(a, b), c);
            V += vt;
            M1[0] += vt * s.x / 4, M1[1] += vt * s.y / 4, M1[2] += vt * s.z / 4;
            const P3 w[4] = {a, b, c, s};
            for (const P3& q : w) {
                C[0] += vt / 20 * q.x * q.x;
                C[1] += vt / 20 * q.y * q.y;
                C[2] += vt / 20 * q.z * q.z;
                C[3] += vt / 20 * q.x * q.y;
                C[4] += vt / 20 * q.x * q.z;
                C[5] += vt / 20 * q.y * q.z;
            }
        }
    }
    if (!(V > 1e-12 * diag * diag * diag)) return h;
    const P3 cr{M1[0] / V, M1[1] / V, M1[2] / V};  // com relative to o
    C[0] -= V * cr.x * cr.x;
    C[1] -= V * cr.y * cr.y;
    C[2] -= V * cr.z * cr.z;
    C[3] -= V * cr.x * cr.y;
    C[4] -= V * cr.x * cr.z;
    C[5] -= V * cr.y * cr.z;

    h.ok = true;
    for (const P3& q : hv) {
        h.verts.push_back((float)q.x);
        h.verts.push_back((float)q.y);
        h.verts.push_back((float)q.z);
    }
    for (const Plane& p : planes) {
        h.planes.push_back((float)p.n.x);
        h.planes.push_back((float)p.n.y);
        h.planes.push_back((float)p.n.z);
        h.planes.push_back((float)p.d);
    }
    h.volume = (float)V;
    const P3 com = add(o, cr);
    h.com[0] = (float)com.x, h.com[1] = (float)com.y, h.com[2] = (float)com.z;
    for (int k = 0; k < 6; ++k) h.cov[k] = (float)C[k];
    return h;
}

}  // namespace physhull
