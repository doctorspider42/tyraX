#include "decalproj.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "objparser.hpp"
#include "primmesh.hpp"

namespace decalproj {
namespace {

constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;
// How far (world units) the decal geometry is pushed toward the decal along the
// receiver surface normal, so it sits just in front instead of z-fighting.
constexpr float kSurfaceOffset = 0.015f;

struct V3 {
    float x, y, z;
};
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline V3 normalize(V3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 1e-8f ? V3{v.x / l, v.y / l, v.z / l} : V3{0, 0, 0};
}

// Object transform (matches project.hpp: scale -> rotX -> rotY -> rotZ ->
// translate). Precomputed sin/cos so world<->local is cheap per vertex.
struct Xform {
    float pos[3], scale[3];
    float cx, sx, cy, sy, cz, sz;

    explicit Xform(const SceneObject& o) {
        for (int i = 0; i < 3; ++i) {
            pos[i] = o.position[i];
            scale[i] = o.scale[i] == 0.0f ? 1e-6f : o.scale[i];
        }
        cx = std::cos(o.rotation[0] * kDeg2Rad); sx = std::sin(o.rotation[0] * kDeg2Rad);
        cy = std::cos(o.rotation[1] * kDeg2Rad); sy = std::sin(o.rotation[1] * kDeg2Rad);
        cz = std::cos(o.rotation[2] * kDeg2Rad); sz = std::sin(o.rotation[2] * kDeg2Rad);
    }

    V3 rot(V3 p) const {
        V3 a = {p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z};        // Rx
        V3 b = {cy * a.x + sy * a.z, a.y, -sy * a.x + cy * a.z};       // Ry
        return {cz * b.x - sz * b.y, sz * b.x + cz * b.y, b.z};        // Rz
    }
    V3 rotInv(V3 p) const {
        V3 a = {cz * p.x + sz * p.y, -sz * p.x + cz * p.y, p.z};       // Rz^-1
        V3 b = {cy * a.x - sy * a.z, a.y, sy * a.x + cy * a.z};        // Ry^-1
        return {b.x, cx * b.y + sx * b.z, -sx * b.y + cx * b.z};       // Rx^-1
    }
    V3 worldFromLocal(V3 l) const {
        return rot({scale[0] * l.x, scale[1] * l.y, scale[2] * l.z}) +
               V3{pos[0], pos[1], pos[2]};
    }
    V3 localFromWorld(V3 w) const {
        const V3 r = rotInv(w - V3{pos[0], pos[1], pos[2]});
        return {r.x / scale[0], r.y / scale[1], r.z / scale[2]};
    }
};

struct Tri {
    V3 p[3];   // world-space vertices
    V3 n;      // world-space face normal (flat)
};

struct Aabb {
    V3 lo{0, 0, 0}, hi{0, 0, 0};
    bool empty = true;
    void add(V3 p) {
        if (empty) { lo = hi = p; empty = false; return; }
        lo = {std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
        hi = {std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
    }
    bool overlaps(const Aabb& o) const {
        return !empty && !o.empty && lo.x <= o.hi.x && hi.x >= o.lo.x &&
               lo.y <= o.hi.y && hi.y >= o.lo.y && lo.z <= o.hi.z && hi.z >= o.lo.z;
    }
};

// World AABB of the projector's oriented unit cube (its 8 transformed corners).
Aabb projectorAabb(const Xform& x) {
    Aabb a;
    for (int i = 0; i < 8; ++i)
        a.add(x.worldFromLocal({(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f,
                                (i & 4) ? 0.5f : -0.5f}));
    return a;
}

bool isReceiverType(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Box:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::Model:
        case PrimitiveType::SavePoint:
        case PrimitiveType::Plane:
            return true;
        default:  // decals, markers, lights, camera, player, emitter, empty...
            return false;
    }
}

// Appends the world-space triangles of a stride-8 (pos+normal+uv) local mesh,
// transformed by `x`, whose world AABB overlaps `box`.
void addLocalMesh(std::vector<Tri>& out, const std::vector<float>& mesh,
                  const Xform& x, const Aabb& box) {
    for (size_t i = 0; i + 23 < mesh.size(); i += 24) {
        Tri t;
        Aabb tb;
        for (int k = 0; k < 3; ++k) {
            const float* v = &mesh[i + k * 8];
            t.p[k] = x.worldFromLocal({v[0], v[1], v[2]});
            tb.add(t.p[k]);
        }
        if (!tb.overlaps(box)) continue;
        t.n = normalize(cross(t.p[1] - t.p[0], t.p[2] - t.p[0]));
        out.push_back(t);
    }
}

void addObjectReceiver(std::vector<Tri>& out, const Project& p, const SceneObject& o,
                       const Aabb& box) {
    const Xform x(o);
    if (!projectorAabb(x).overlaps(box)) {
        // cheap reject for primitives; models need their own AABB (below)
        if (o.type != PrimitiveType::Model) return;
    }
    if (o.type == PrimitiveType::Model) {
        if (o.modelPath.empty() || isAnimatedModelPath(o.modelPath)) return;  // .obj only
        objparser::Model m;
        if (!objparser::load(p.dir + "\\" + o.modelPath, m)) return;
        for (const objparser::Submesh& s : m.submeshes)
            addLocalMesh(out, s.verts, x, box);
        return;
    }
    std::vector<float> mesh;
    switch (o.type) {
        case PrimitiveType::Sphere: mesh = primmesh::unitSphere(o.primDetail); break;
        case PrimitiveType::Cylinder: mesh = primmesh::unitCylinder(o.primDetail); break;
        case PrimitiveType::Cone: mesh = primmesh::unitCone(o.primDetail); break;
        case PrimitiveType::Plane: mesh = primmesh::unitPlane(); break;
        default: mesh = primmesh::unitBox(o.primDetail); break;  // Box, SavePoint
    }
    addLocalMesh(out, mesh, x, box);
}

void addQuadTri(std::vector<Tri>& out, V3 a, V3 b, V3 c, const Aabb& box) {
    Tri t;
    Aabb tb;
    t.p[0] = a; t.p[1] = b; t.p[2] = c;
    tb.add(a); tb.add(b); tb.add(c);
    if (!tb.overlaps(box)) return;
    t.n = normalize(cross(b - a, c - a));
    out.push_back(t);
}

// Terrain triangles from the scene heightmap grid, restricted to the projector
// XZ footprint. Mirrors project::heightAtWorld's grid mapping. A flat scene
// (no sculpted heightmap) still renders terrain at y=0 in the game, so project
// onto a flat ground quad in that case - fake shadows land on flat floors too.
void addTerrainReceiver(std::vector<Tri>& out, const SceneData& s, const Aabb& box) {
    const float w = (float)s.terrain.width, d = (float)s.terrain.depth;
    if (w <= 0.0f || d <= 0.0f) return;
    if (s.hmW < 2 || s.hmD < 2 || (int)s.heights.size() != s.hmW * s.hmD) {
        // Flat terrain at y=0: one footprint quad clamped to the terrain extent
        // (the per-triangle overlap test drops it when the projector misses y=0).
        const float x0 = std::max(-w * 0.5f, box.lo.x), x1 = std::min(w * 0.5f, box.hi.x);
        const float z0 = std::max(-d * 0.5f, box.lo.z), z1 = std::min(d * 0.5f, box.hi.z);
        if (x1 <= x0 || z1 <= z0) return;
        // Wound so the face normal points UP (+Y) - the visible ground surface,
        // which is what a floor decal projects onto (facing test keeps +Z-local).
        const V3 a{x0, 0, z0}, b{x1, 0, z0}, c{x1, 0, z1}, e{x0, 0, z1};
        addQuadTri(out, a, e, c, box);
        addQuadTri(out, a, c, b, box);
        return;
    }
    const float stepX = w / (s.hmW - 1), stepZ = d / (s.hmD - 1);
    auto worldOf = [&](int gx, int gz) -> V3 {
        return {-w * 0.5f + gx * stepX, s.heights[(size_t)gz * s.hmW + gx],
                -d * 0.5f + gz * stepZ};
    };
    // Grid cell index range overlapping the projector XZ box (+1 margin).
    const int x0 = std::max(0, (int)std::floor((box.lo.x + w * 0.5f) / stepX) - 1);
    const int x1 = std::min(s.hmW - 2, (int)std::floor((box.hi.x + w * 0.5f) / stepX) + 1);
    const int z0 = std::max(0, (int)std::floor((box.lo.z + d * 0.5f) / stepZ) - 1);
    const int z1 = std::min(s.hmD - 2, (int)std::floor((box.hi.z + d * 0.5f) / stepZ) + 1);
    for (int gz = z0; gz <= z1; ++gz)
        for (int gx = x0; gx <= x1; ++gx) {
            const V3 a = worldOf(gx, gz), b = worldOf(gx + 1, gz);
            const V3 c = worldOf(gx + 1, gz + 1), e = worldOf(gx, gz + 1);
            // Up-facing (+Y) winding, matching the flat path (see addQuadTri).
            for (const std::array<V3, 3>& tri : {std::array<V3, 3>{a, e, c},
                                                 std::array<V3, 3>{a, c, b}}) {
                Tri t;
                Aabb tb;
                for (int k = 0; k < 3; ++k) { t.p[k] = tri[k]; tb.add(tri[k]); }
                if (!tb.overlaps(box)) continue;
                t.n = normalize(cross(t.p[1] - t.p[0], t.p[2] - t.p[0]));
                out.push_back(t);
            }
        }
}

// Sutherland-Hodgman clip of a convex polygon (local space) against one axis
// half-space. axis 0/1/2 = x/y/z; keepPositive true keeps coord <= +0.5, false
// keeps coord >= -0.5.
void clipPlane(std::vector<V3>& poly, int axis, bool upper) {
    if (poly.empty()) return;
    const float plane = upper ? 0.5f : -0.5f;
    auto coord = [axis](const V3& v) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; };
    auto inside = [&](const V3& v) { return upper ? coord(v) <= plane : coord(v) >= plane; };
    std::vector<V3> in;
    in.reserve(poly.size() + 1);
    for (size_t i = 0; i < poly.size(); ++i) {
        const V3& cur = poly[i];
        const V3& prv = poly[(i + poly.size() - 1) % poly.size()];
        const bool ci = inside(cur), pi = inside(prv);
        if (ci != pi) {
            const float cp = coord(prv), cc = coord(cur);
            const float t = (plane - cp) / (cc - cp);
            in.push_back({prv.x + (cur.x - prv.x) * t, prv.y + (cur.y - prv.y) * t,
                          prv.z + (cur.z - prv.z) * t});
        }
        if (ci) in.push_back(cur);
    }
    poly.swap(in);
}

}  // namespace

DecalMesh project(const Project& p, const SceneData& s, const SceneObject& decal) {
    DecalMesh out;
    const Xform x(decal);
    const Aabb box = projectorAabb(x);

    // Gather receiver triangles overlapping the projector box.
    std::vector<Tri> tris;
    addTerrainReceiver(tris, s, box);
    for (const SceneObject& o : s.objects) {
        if (&o == &decal || o.id == decal.id) continue;
        if (!isReceiverType(o.type)) continue;
        addObjectReceiver(tris, p, o, box);
    }

    // Clip each facing triangle to the projector unit cube in local space.
    int emitted = 0;
    for (const Tri& t : tris) {
        std::vector<V3> poly = {x.localFromWorld(t.p[0]), x.localFromWorld(t.p[1]),
                                x.localFromWorld(t.p[2])};
        // Facing test in local space: keep surfaces whose front faces the decal
        // (+Z), so we don't wrap onto the far side of a thin wall.
        const V3 ln = cross(poly[1] - poly[0], poly[2] - poly[0]);
        if (ln.z <= 1e-5f) continue;
        clipPlane(poly, 0, true);  clipPlane(poly, 0, false);
        clipPlane(poly, 1, true);  clipPlane(poly, 1, false);
        clipPlane(poly, 2, true);  clipPlane(poly, 2, false);
        if (poly.size() < 3) continue;

        // Fan-triangulate; world position via the forward transform, UV from
        // local XY (matches the flat decal / box +Z face), nudged along the
        // world surface normal to sit in front.
        const V3 off = {t.n.x * kSurfaceOffset, t.n.y * kSurfaceOffset,
                        t.n.z * kSurfaceOffset};
        auto emit = [&](const V3& l) {
            const V3 w = x.worldFromLocal(l) + off;
            // Slide-projector UV: u runs with the projector's right (local -X) so
            // the image reads correctly (not mirrored) when viewed from the +Z
            // front; v with +Y. Matches the flat decal (unitDecal / addDecal).
            out.verts.insert(out.verts.end(), {w.x, w.y, w.z, 0.5f - l.x, l.y + 0.5f});
        };
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
            if (emitted >= kMaxTris) { out.truncated = true; return out; }
            emit(poly[0]);
            emit(poly[i]);
            emit(poly[i + 1]);
            ++emitted;
        }
    }
    return out;
}

}  // namespace decalproj
