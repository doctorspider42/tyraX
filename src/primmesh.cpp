#include "primmesh.hpp"

#include <cmath>

#include "project.hpp"  // clampPrimDetail, primSphereStacks, kDefault* + PrimitiveType

namespace primmesh {
namespace {

constexpr float kPi = 3.14159265358979f;

struct V3 {
    float x, y, z;
};

// Vertex layout: pos(3) + normal(3) + uv(2). Normals are the geometric surface
// normals (the viewport bakes shade from them; decalproj uses them for the
// projector facing test).
void pushRaw(std::vector<float>& v, V3 p, V3 n, float tu, float tv) {
    v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, tu, tv});
}

void pushQuadRaw(std::vector<float>& v, V3 a, V3 b, V3 c, V3 d, V3 n) {
    pushRaw(v, a, n, 0, 0);
    pushRaw(v, b, n, 1, 0);
    pushRaw(v, c, n, 1, 1);
    pushRaw(v, a, n, 0, 0);
    pushRaw(v, c, n, 1, 1);
    pushRaw(v, d, n, 0, 1);
}

}  // namespace

// detail = subdivisions per edge; at 1 this emits exactly the original 6-quad
// box. Each face is an n x n grid; UVs span 0..1 over the whole face (texture
// does not tile with detail). Kept in sync with the PS2 runtime addBox.
std::vector<float> unitBox(int detail) {
    std::vector<float> v;
    const int n = clampPrimDetail(PrimitiveType::Box, detail);
    const float h = 0.5f, H = 1.0f;  // half-extent, full edge
    // Face grid spanning c0 + s*du + t*dv (s,t in [0,1]); du x dv points along
    // nrm so the winding stays CCW-outward.
    auto face = [&](V3 c0, V3 du, V3 dv, V3 nrm) {
        auto P = [&](float s, float t) -> V3 {
            return {c0.x + du.x * s + dv.x * t, c0.y + du.y * s + dv.y * t,
                    c0.z + du.z * s + dv.z * t};
        };
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const float s0 = (float)i / n, s1 = (float)(i + 1) / n;
                const float t0 = (float)j / n, t1 = (float)(j + 1) / n;
                const V3 a = P(s0, t0), b = P(s1, t0), c = P(s1, t1), d = P(s0, t1);
                pushRaw(v, a, nrm, s0, t0);
                pushRaw(v, b, nrm, s1, t0);
                pushRaw(v, c, nrm, s1, t1);
                pushRaw(v, a, nrm, s0, t0);
                pushRaw(v, c, nrm, s1, t1);
                pushRaw(v, d, nrm, s0, t1);
            }
    };
    face({h, -h, -h}, {0, H, 0}, {0, 0, H}, {1, 0, 0});    // +X
    face({-h, -h, h}, {0, H, 0}, {0, 0, -H}, {-1, 0, 0});  // -X
    face({-h, h, -h}, {0, 0, H}, {H, 0, 0}, {0, 1, 0});    // +Y
    face({-h, -h, h}, {0, 0, -H}, {H, 0, 0}, {0, -1, 0});  // -Y
    face({-h, -h, h}, {H, 0, 0}, {0, H, 0}, {0, 0, 1});    // +Z
    face({h, -h, -h}, {-H, 0, 0}, {0, H, 0}, {0, 0, -1});  // -Z
    return v;
}

// detail = radial segment count; see project.hpp for the shared tessellation
// formulas (kept in sync with the PS2 runtime in templates.cpp addSphere).
std::vector<float> unitSphere(int detail) {
    std::vector<float> v;
    const int slices = clampPrimDetail(PrimitiveType::Sphere, detail);
    const int stacks = primSphereStacks(slices);
    const float r = 0.5f;
    auto at = [&](float t, float p) -> V3 {
        return {r * std::sin(t) * std::cos(p), r * std::cos(t), r * std::sin(t) * std::sin(p)};
    };
    for (int st = 0; st < stacks; ++st) {
        const float t0 = kPi * st / stacks, t1 = kPi * (st + 1) / stacks;
        const float tv0 = (float)st / stacks, tv1 = (float)(st + 1) / stacks;
        for (int sl = 0; sl < slices; ++sl) {
            const float p0 = 2 * kPi * sl / slices, p1 = 2 * kPi * (sl + 1) / slices;
            const float tu0 = (float)sl / slices, tu1 = (float)(sl + 1) / slices;
            V3 v00 = at(t0, p0), v01 = at(t0, p1), v10 = at(t1, p0), v11 = at(t1, p1);
            auto n = [](V3 p) -> V3 { return {p.x * 2, p.y * 2, p.z * 2}; };
            pushRaw(v, v00, n(v00), tu0, tv0);
            pushRaw(v, v10, n(v10), tu0, tv1);
            pushRaw(v, v11, n(v11), tu1, tv1);
            pushRaw(v, v00, n(v00), tu0, tv0);
            pushRaw(v, v11, n(v11), tu1, tv1);
            pushRaw(v, v01, n(v01), tu1, tv0);
        }
    }
    return v;
}

std::vector<float> unitCylinder(int detail) {
    std::vector<float> v;
    const int seg = clampPrimDetail(PrimitiveType::Cylinder, detail);
    const float r = 0.5f, h = 0.5f;
    for (int i = 0; i < seg; ++i) {
        const float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        const float x0 = r * std::cos(a0), z0 = r * std::sin(a0);
        const float x1 = r * std::cos(a1), z1 = r * std::sin(a1);
        const V3 n0 = {std::cos(a0), 0, std::sin(a0)};
        const V3 n1 = {std::cos(a1), 0, std::sin(a1)};
        const float u0 = (float)i / seg, u1 = (float)(i + 1) / seg;
        pushRaw(v, {x0, -h, z0}, n0, u0, 1);
        pushRaw(v, {x0, h, z0}, n0, u0, 0);
        pushRaw(v, {x1, h, z1}, n1, u1, 0);
        pushRaw(v, {x0, -h, z0}, n0, u0, 1);
        pushRaw(v, {x1, h, z1}, n1, u1, 0);
        pushRaw(v, {x1, -h, z1}, n1, u1, 1);
        pushRaw(v, {0, h, 0}, {0, 1, 0}, 0.5f, 0.5f);
        pushRaw(v, {x1, h, z1}, {0, 1, 0}, x1 + 0.5f, z1 + 0.5f);
        pushRaw(v, {x0, h, z0}, {0, 1, 0}, x0 + 0.5f, z0 + 0.5f);
        pushRaw(v, {0, -h, 0}, {0, -1, 0}, 0.5f, 0.5f);
        pushRaw(v, {x0, -h, z0}, {0, -1, 0}, x0 + 0.5f, z0 + 0.5f);
        pushRaw(v, {x1, -h, z1}, {0, -1, 0}, x1 + 0.5f, z1 + 0.5f);
    }
    return v;
}

std::vector<float> unitCone(int detail) {
    std::vector<float> v;
    const int seg = clampPrimDetail(PrimitiveType::Cone, detail);
    const float r = 0.5f, h = 0.5f;
    const float nl = 0.894f, ny = 0.447f;
    for (int i = 0; i < seg; ++i) {
        const float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        const float am = (a0 + a1) * 0.5f;
        const float x0 = r * std::cos(a0), z0 = r * std::sin(a0);
        const float x1 = r * std::cos(a1), z1 = r * std::sin(a1);
        const float u0 = (float)i / seg, u1 = (float)(i + 1) / seg;
        pushRaw(v, {0, h, 0}, {nl * std::cos(am), ny, nl * std::sin(am)},
                (u0 + u1) * 0.5f, 0);
        pushRaw(v, {x1, -h, z1}, {nl * std::cos(a1), ny, nl * std::sin(a1)}, u1, 1);
        pushRaw(v, {x0, -h, z0}, {nl * std::cos(a0), ny, nl * std::sin(a0)}, u0, 1);
        pushRaw(v, {0, -h, 0}, {0, -1, 0}, 0.5f, 0.5f);
        pushRaw(v, {x0, -h, z0}, {0, -1, 0}, x0 + 0.5f, z0 + 0.5f);
        pushRaw(v, {x1, -h, z1}, {0, -1, 0}, x1 + 0.5f, z1 + 0.5f);
    }
    return v;
}

// Flat unit square in the XZ plane, double-sided (top +Y and bottom -Y faces)
// so it is visible from both above and below.
std::vector<float> unitPlane() {
    std::vector<float> v;
    const float h = 0.5f;
    pushQuadRaw(v, {-h, 0, -h}, {-h, 0, h}, {h, 0, h}, {h, 0, -h}, {0, 1, 0});
    pushQuadRaw(v, {-h, 0, h}, {-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {0, -1, 0});
    return v;
}

}  // namespace primmesh
