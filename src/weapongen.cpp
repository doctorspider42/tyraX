// Procedural weapon models - see weapongen.hpp for the why and the
// orientation convention (+Z forward, origin at the grip).
//
// Everything is built from exactly two primitives: a PRISM (a box along Z
// whose cross-section may differ at each end - that one shape is a box, a
// wedge, a blade and a tapered stock) and a TUBE (a cone/cylinder along Z -
// barrels, drums, hafts, pommels). Each part is placed by center + two
// rotations, which is enough for every angled grip and canted axe head here
// and keeps the whole file free of matrix code.
//
// Parts are flat-shaded on purpose: a per-face normal is what makes a
// 200-triangle gun read as machined metal instead of a soft blob, and it is
// also what the .tmdl bake would derive anyway (see meshlod.cpp's weld rule).

#include "weapongen.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace weapongen {
namespace {

constexpr float kPi = 3.14159265358979f;

struct V3 {
    float x = 0, y = 0, z = 0;
};

// Deterministic per-part jitter. Seeded once per generate() and consumed in a
// fixed order, so the same Params always produce the same bytes - the treegen
// rule (editing one slider must adjust the model, not reshuffle it).
struct Rng {
    uint32_t s;
    float next() {  // -1 .. 1
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

V3 rotXY(V3 v, float rx, float ry) {
    if (rx != 0.0f) {
        const float c = cosf(rx), s = sinf(rx);
        const float y = v.y * c - v.z * s, z = v.y * s + v.z * c;
        v.y = y, v.z = z;
    }
    if (ry != 0.0f) {
        const float c = cosf(ry), s = sinf(ry);
        const float x = v.x * c + v.z * s, z = -v.x * s + v.z * c;
        v.x = x, v.z = z;
    }
    return v;
}

void pushVert(std::vector<float>& out, const V3& p, const V3& n, float u, float v) {
    out.push_back(p.x), out.push_back(p.y), out.push_back(p.z);
    out.push_back(n.x), out.push_back(n.y), out.push_back(n.z);
    out.push_back(u), out.push_back(v);
}

// One flat-shaded triangle. UVs are a planar projection of the world position
// on the triangle's dominant axis pair: cosmetic (these materials carry no
// map_Kd) but well-formed, so a user who later assigns a texture gets
// something sane instead of a pile of zeros.
void tri(std::vector<float>& out, const V3& a, const V3& b, const V3& c,
         float uvScale) {
    const V3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
    const V3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
    V3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
         e1.x * e2.y - e1.y * e2.x};
    const float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (l < 1e-9f) return;  // degenerate (a fully tapered tip's end cap)
    n.x /= l, n.y /= l, n.z /= l;
    const float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
    auto uv = [&](const V3& p, float& u, float& v) {
        if (az >= ax && az >= ay) u = p.x, v = p.y;
        else if (ax >= ay) u = p.z, v = p.y;
        else u = p.x, v = p.z;
        u *= uvScale, v *= uvScale;
    };
    float u0, v0, u1, v1, u2, v2;
    uv(a, u0, v0), uv(b, u1, v1), uv(c, u2, v2);
    pushVert(out, a, n, u0, v0);
    pushVert(out, b, n, u1, v1);
    pushVert(out, c, n, u2, v2);
}

void quad(std::vector<float>& out, const V3& a, const V3& b, const V3& c,
          const V3& d, float uvScale) {
    tri(out, a, b, c, uvScale);
    tri(out, a, c, d, uvScale);
}

// A box along Z whose cross-section is (hx0, hy0) at the -Z end and
// (hx1, hy1) at the +Z end. Placed at `c`, rotated rx about X then ry about Y.
void prism(std::vector<float>& out, V3 c, float hx0, float hy0, float hx1,
           float hy1, float hz, float rx, float ry, float uvScale) {
    // A face that collapses to a line produces degenerate triangles that the
    // .tmdl bake would have to filter; keep every end at a sliver instead.
    const float eps = 0.0006f;
    if (hx0 < eps) hx0 = eps;
    if (hy0 < eps) hy0 = eps;
    if (hx1 < eps) hx1 = eps;
    if (hy1 < eps) hy1 = eps;
    V3 v[8] = {{-hx0, -hy0, -hz}, {hx0, -hy0, -hz}, {hx0, hy0, -hz},
               {-hx0, hy0, -hz},  {-hx1, -hy1, hz}, {hx1, -hy1, hz},
               {hx1, hy1, hz},    {-hx1, hy1, hz}};
    for (V3& p : v) {
        p = rotXY(p, rx, ry);
        p.x += c.x, p.y += c.y, p.z += c.z;
    }
    quad(out, v[4], v[5], v[6], v[7], uvScale);  // front (+Z)
    quad(out, v[1], v[0], v[3], v[2], uvScale);  // back  (-Z)
    quad(out, v[0], v[1], v[5], v[4], uvScale);  // bottom
    quad(out, v[3], v[7], v[6], v[2], uvScale);  // top
    quad(out, v[0], v[4], v[7], v[3], uvScale);  // left
    quad(out, v[1], v[2], v[6], v[5], uvScale);  // right
}

inline void box(std::vector<float>& out, V3 c, float hx, float hy, float hz,
                float rx, float ry, float uvScale) {
    prism(out, c, hx, hy, hx, hy, hz, rx, ry, uvScale);
}

// A cone/cylinder along Z: radius r0 at -Z, r1 at +Z. `caps` closes both ends
// (an open tube is cheaper where the end is buried inside another part).
void tube(std::vector<float>& out, V3 c, float r0, float r1, float hz,
          int sides, float rx, float ry, bool caps, float uvScale) {
    if (sides < 3) sides = 3;
    std::vector<V3> a(sides), b(sides);
    for (int i = 0; i < sides; ++i) {
        const float t = 2.0f * kPi * (float)i / (float)sides;
        const float cs = cosf(t), sn = sinf(t);
        V3 p0{cs * r0, sn * r0, -hz}, p1{cs * r1, sn * r1, hz};
        p0 = rotXY(p0, rx, ry), p1 = rotXY(p1, rx, ry);
        p0.x += c.x, p0.y += c.y, p0.z += c.z;
        p1.x += c.x, p1.y += c.y, p1.z += c.z;
        a[i] = p0, b[i] = p1;
    }
    for (int i = 0; i < sides; ++i) {
        const int j = (i + 1) % sides;
        quad(out, a[i], a[j], b[j], b[i], uvScale);
    }
    if (!caps) return;
    V3 cb = rotXY(V3{0, 0, -hz}, rx, ry), cf = rotXY(V3{0, 0, hz}, rx, ry);
    cb.x += c.x, cb.y += c.y, cb.z += c.z;
    cf.x += c.x, cf.y += c.y, cf.z += c.z;
    for (int i = 0; i < sides; ++i) {
        const int j = (i + 1) % sides;
        tri(out, cb, a[j], a[i], uvScale);
        tri(out, cf, b[i], b[j], uvScale);
    }
}

void bounds(const std::vector<float>& tris, float* mn, float* mx, bool& any) {
    for (size_t i = 0; i + 7 < tris.size(); i += 8) {
        for (int k = 0; k < 3; ++k) {
            const float v = tris[i + k];
            if (!any) mn[k] = mx[k] = v;
            else {
                if (v < mn[k]) mn[k] = v;
                if (v > mx[k]) mx[k] = v;
            }
        }
        any = true;
    }
}

}  // namespace

const char* kindName(int kind) {
    switch (kind) {
        case Params::Pistol: return "Pistol";
        case Params::Revolver: return "Revolver";
        case Params::Smg: return "SMG";
        case Params::Rifle: return "Rifle";
        case Params::Shotgun: return "Shotgun";
        case Params::Knife: return "Knife";
        case Params::Sword: return "Sword";
        case Params::Axe: return "Axe";
        case Params::Crowbar: return "Crowbar";
        default: return "Weapon";
    }
}

Mesh generate(const Params& pin) {
    Params p = pin;
    if (p.length < 0.02f) p.length = 0.02f;
    if (p.bulk < 0.2f) p.bulk = 0.2f;
    if (p.sides < 3) p.sides = 3;
    if (p.sides > 24) p.sides = 24;
    if (p.wear < 0.0f) p.wear = 0.0f;
    if (p.wear > 1.0f) p.wear = 1.0f;

    Mesh m;
    const float L = p.length;
    const float B = p.bulk;
    const int S = p.sides;
    const float uv = 1.0f / L;  // one texture repeat over the weapon's length
    Rng rng{p.seed ? p.seed : 1u};
    // Wear only ever SHRINKS a part (a used weapon is worn down, not swollen),
    // so a heavily worn gun stays inside the silhouette the length implies.
    auto w = [&](float v) { return v * (1.0f - 0.12f * p.wear * fabsf(rng.next())); };

    std::vector<float>& M = m.metal;
    std::vector<float>& G = m.grip;
    std::vector<float>& A = m.accent;
    const float d2r = kPi / 180.0f;

    switch (p.kind) {
        case Params::Pistol: {
            // Slide + frame + angled grip: the Browning silhouette everyone
            // reads as "handgun" from three pixels of screen.
            box(M, {0, 0.10f * L, 0.26f * L}, w(0.055f * L * B), w(0.078f * L * B),
                0.40f * L, 0, 0, uv);
            box(M, {0, -0.01f * L, 0.12f * L}, w(0.048f * L * B),
                w(0.055f * L * B), 0.26f * L, 0, 0, uv);
            tube(M, {0, 0.10f * L, 0.68f * L}, 0.036f * L * B, 0.034f * L * B,
                 0.02f * L, S, 0, 0, true, uv);
            // Grip: long in Y, raked back 13 degrees like a real frame.
            box(G, {0, -0.23f * L, -0.09f * L}, w(0.050f * L * B), 0.19f * L,
                w(0.070f * L * B), -13.0f * d2r, 0, uv);
            box(A, {0, -0.41f * L, -0.13f * L}, 0.050f * L * B, 0.015f * L,
                0.070f * L * B, -13.0f * d2r, 0, uv);  // magazine floorplate
            // Trigger guard: a bottom bar and a front post.
            box(M, {0, -0.155f * L, 0.02f * L}, 0.042f * L * B, 0.013f * L,
                0.10f * L, 0, 0, uv);
            box(M, {0, -0.10f * L, 0.11f * L}, 0.042f * L * B, 0.055f * L,
                0.013f * L, 0, 0, uv);
            box(A, {0, -0.09f * L, -0.01f * L}, 0.018f * L, 0.045f * L,
                0.012f * L, 0, 0, uv);  // trigger
            // Sights.
            box(A, {0, 0.20f * L, 0.62f * L}, 0.010f * L, 0.022f * L,
                0.012f * L, 0, 0, uv);
            box(A, {0, 0.20f * L, -0.10f * L}, 0.030f * L, 0.020f * L,
                0.014f * L, 0, 0, uv);
            break;
        }
        case Params::Revolver: {
            tube(M, {0, 0.10f * L, 0.48f * L}, 0.040f * L * B, 0.038f * L * B,
                 0.28f * L, S, 0, 0, true, uv);
            box(M, {0, 0.17f * L, 0.46f * L}, 0.016f * L, 0.020f * L,
                0.28f * L, 0, 0, uv);  // top rib
            // The drum: a low-sided tube is exactly a chambered cylinder.
            tube(A, {0, 0.09f * L, 0.10f * L}, 0.082f * L * B, 0.082f * L * B,
                 0.11f * L, S >= 6 ? 6 : S, 0, 0, true, uv);
            box(M, {0, 0.06f * L, -0.09f * L}, 0.045f * L * B, 0.085f * L,
                0.10f * L, 0, 0, uv);  // frame behind the drum
            box(A, {0, 0.20f * L, -0.17f * L}, 0.016f * L, 0.045f * L,
                0.030f * L, -20.0f * d2r, 0, uv);  // hammer
            box(G, {0, -0.24f * L, -0.16f * L}, w(0.048f * L * B), 0.24f * L,
                w(0.075f * L * B), -20.0f * d2r, 0, uv);
            box(M, {0, -0.14f * L, 0.00f * L}, 0.040f * L * B, 0.013f * L,
                0.085f * L, 0, 0, uv);
            box(A, {0, -0.08f * L, -0.05f * L}, 0.016f * L, 0.042f * L,
                0.012f * L, 0, 0, uv);
            break;
        }
        case Params::Smg: {
            box(M, {0, 0.08f * L, 0.20f * L}, w(0.055f * L * B), w(0.085f * L * B),
                0.30f * L, 0, 0, uv);
            tube(M, {0, 0.08f * L, 0.60f * L}, 0.030f * L * B, 0.028f * L * B,
                 0.12f * L, S, 0, 0, true, uv);
            box(M, {0, 0.15f * L, 0.60f * L}, 0.012f * L, 0.020f * L, 0.10f * L,
                0, 0, uv);  // barrel shroud rib
            // Magazine: raked forward, the MP5/Uzi read.
            box(A, {0, -0.24f * L, 0.16f * L}, w(0.038f * L * B), 0.24f * L,
                w(0.050f * L * B), 8.0f * d2r, 0, uv);
            box(G, {0, -0.22f * L, -0.10f * L}, w(0.046f * L * B), 0.20f * L,
                w(0.062f * L * B), -12.0f * d2r, 0, uv);
            box(M, {0, -0.13f * L, 0.01f * L}, 0.040f * L * B, 0.012f * L,
                0.08f * L, 0, 0, uv);
            // Folding stock: two struts and a shoulder plate.
            box(M, {0.045f * L * B, 0.02f * L, -0.30f * L}, 0.010f * L,
                0.010f * L, 0.22f * L, 0, 0, uv);
            box(M, {-0.045f * L * B, 0.02f * L, -0.30f * L}, 0.010f * L,
                0.010f * L, 0.22f * L, 0, 0, uv);
            box(G, {0, 0.02f * L, -0.52f * L}, 0.055f * L * B, 0.060f * L,
                0.016f * L, 0, 0, uv);
            box(A, {0, 0.19f * L, 0.42f * L}, 0.012f * L, 0.030f * L,
                0.012f * L, 0, 0, uv);
            break;
        }
        case Params::Rifle: {
            tube(M, {0, 0.06f * L, 0.55f * L}, 0.026f * L * B, 0.024f * L * B,
                 0.36f * L, S, 0, 0, true, uv);
            box(M, {0, 0.04f * L, 0.10f * L}, w(0.045f * L * B), w(0.070f * L * B),
                0.22f * L, 0, 0, uv);  // receiver
            box(G, {0, 0.02f * L, 0.30f * L}, w(0.050f * L * B), 0.048f * L,
                0.16f * L, 0, 0, uv);  // handguard
            // Stock: one tapered prism from the receiver to the butt plate -
            // the classic wooden rifle stock in a single part.
            prism(G, {0, -0.05f * L, -0.24f * L}, 0.052f * L * B, 0.115f * L,
                  0.048f * L * B, 0.075f * L, 0.24f * L, 0, 0, uv);
            box(A, {0, -0.06f * L, -0.49f * L}, 0.050f * L * B, 0.105f * L,
                0.014f * L, 0, 0, uv);  // butt plate
            box(A, {0, -0.15f * L, 0.06f * L}, w(0.034f * L * B), 0.10f * L,
                w(0.040f * L * B), 6.0f * d2r, 0, uv);  // magazine
            box(M, {0, -0.10f * L, -0.02f * L}, 0.038f * L * B, 0.012f * L,
                0.07f * L, 0, 0, uv);  // trigger guard
            box(A, {0, -0.06f * L, -0.05f * L}, 0.014f * L, 0.035f * L,
                0.010f * L, 0, 0, uv);  // trigger
            // Scope on two mounts - the part that sells "rifle" instantly.
            tube(M, {0, 0.16f * L, 0.14f * L}, 0.034f * L * B, 0.034f * L * B,
                 0.16f * L, S, 0, 0, true, uv);
            box(A, {0, 0.11f * L, 0.02f * L}, 0.014f * L, 0.026f * L,
                0.014f * L, 0, 0, uv);
            box(A, {0, 0.11f * L, 0.26f * L}, 0.014f * L, 0.026f * L,
                0.014f * L, 0, 0, uv);
            box(M, {0.055f * L * B, 0.06f * L, 0.00f * L}, 0.030f * L,
                0.010f * L, 0.010f * L, 0, 0, uv);  // bolt handle
            break;
        }
        case Params::Shotgun: {
            const float dx = 0.036f * L * B;
            tube(M, {dx, 0.09f * L, 0.50f * L}, 0.034f * L * B, 0.032f * L * B,
                 0.34f * L, S, 0, 0, true, uv);
            tube(M, {-dx, 0.09f * L, 0.50f * L}, 0.034f * L * B, 0.032f * L * B,
                 0.34f * L, S, 0, 0, true, uv);
            box(M, {0, 0.09f * L, 0.06f * L}, 0.070f * L * B, w(0.070f * L * B),
                0.14f * L, 0, 0, uv);  // breech
            box(G, {0, -0.02f * L, 0.42f * L}, w(0.070f * L * B), 0.048f * L,
                0.20f * L, 0, 0, uv);  // pump / forend
            prism(G, {0, -0.02f * L, -0.26f * L}, 0.050f * L * B, 0.110f * L,
                  0.046f * L * B, 0.080f * L, 0.24f * L, 0, 0, uv);
            box(A, {0, -0.03f * L, -0.51f * L}, 0.048f * L * B, 0.100f * L,
                0.014f * L, 0, 0, uv);
            box(M, {0, -0.06f * L, -0.05f * L}, 0.040f * L * B, 0.012f * L,
                0.07f * L, 0, 0, uv);
            box(A, {0, 0.13f * L, 0.80f * L}, 0.008f * L, 0.014f * L,
                0.010f * L, 0, 0, uv);  // bead sight
            break;
        }
        case Params::Knife: {
            // Blade: thin in X, tall in Y - a blade stands EDGE-DOWN in the
            // hand, and the crossguard below is perpendicular to it. Getting
            // this pair the wrong way round is what makes a generated sword
            // read as a plank. It tapers to a point in both axes at once.
            prism(M, {0, 0.02f * L, 0.42f * L}, 0.011f * L * B, 0.048f * L * B,
                  0.003f * L, 0.004f * L, 0.34f * L, 0, 0, uv);
            box(A, {0, 0.01f * L, 0.06f * L}, 0.070f * L * B, 0.022f * L,
                0.016f * L, 0, 0, uv);  // guard
            box(G, {0, 0.00f * L, -0.14f * L}, w(0.032f * L * B),
                w(0.038f * L * B), 0.20f * L, 0, 0, uv);
            box(A, {0, 0.00f * L, -0.36f * L}, 0.036f * L * B, 0.042f * L * B,
                0.020f * L, 0, 0, uv);  // pommel
            break;
        }
        case Params::Sword: {
            prism(M, {0, 0.00f * L, 0.52f * L}, 0.012f * L * B, 0.032f * L * B,
                  0.004f * L, 0.006f * L, 0.44f * L, 0, 0, uv);
            box(A, {0, 0.00f * L, 0.06f * L}, 0.105f * L * B, 0.024f * L,
                0.022f * L, 0, 0, uv);  // crossguard
            box(G, {0, 0.00f * L, -0.16f * L}, w(0.030f * L * B),
                w(0.034f * L * B), 0.20f * L, 0, 0, uv);
            tube(A, {0, 0.00f * L, -0.40f * L}, 0.048f * L * B, 0.030f * L * B,
                 0.030f * L, S, 0, 0, true, uv);  // pommel
            break;
        }
        case Params::Axe: {
            tube(G, {0, 0.00f * L, 0.08f * L}, 0.024f * L * B, 0.022f * L * B,
                 0.44f * L, S, 0, 0, true, uv);
            // Head: a wedge that WIDENS in Y while thinning in X as it goes
            // forward, so the cutting edge is a vertical line at the front.
            prism(M, {0, 0.04f * L, 0.54f * L}, 0.030f * L * B, 0.055f * L * B,
                  0.005f * L, 0.115f * L * B, 0.070f * L, 0, 0, uv);
            box(M, {0, 0.00f * L, 0.45f * L}, 0.034f * L * B, 0.050f * L,
                0.045f * L, 0, 0, uv);  // eye around the haft
            box(A, {0, -0.04f * L, 0.40f * L}, 0.028f * L * B, 0.038f * L,
                0.030f * L, 0, 0, uv);  // poll (the blunt back)
            box(A, {0, 0.00f * L, -0.34f * L}, 0.030f * L * B, 0.030f * L * B,
                0.020f * L, 0, 0, uv);  // butt cap
            break;
        }
        case Params::Crowbar:
        default: {
            tube(M, {0, 0.00f * L, 0.06f * L}, 0.021f * L * B, 0.019f * L * B,
                 0.42f * L, S, 0, 0, true, uv);
            // The claw: two short bars bent forward, the recognizable hook.
            box(M, {0, 0.045f * L, 0.53f * L}, 0.020f * L * B, 0.020f * L * B,
                0.075f * L, 35.0f * d2r, 0, uv);
            prism(M, {0, 0.135f * L, 0.60f * L}, 0.022f * L * B, 0.020f * L * B,
                  0.030f * L * B, 0.006f * L, 0.060f * L, 70.0f * d2r, 0, uv);
            box(A, {0, 0.00f * L, -0.40f * L}, 0.024f * L * B, 0.024f * L * B,
                0.045f * L, 20.0f * d2r, 0, uv);  // flattened pry end
            break;
        }
    }

    bool any = false;
    bounds(m.metal, m.min, m.max, any);
    bounds(m.grip, m.min, m.max, any);
    bounds(m.accent, m.min, m.max, any);
    return m;
}

const std::vector<Params>& presets() {
    static const std::vector<Params> list = [] {
        std::vector<Params> v;
        auto col = [](Params& p, const float me[3], const float gr[3],
                      const float ac[3]) {
            std::memcpy(p.metal, me, 12);
            std::memcpy(p.grip, gr, 12);
            std::memcpy(p.accent, ac, 12);
        };
        const float gunMetal[3] = {0.30f, 0.31f, 0.34f};
        const float darkGrip[3] = {0.13f, 0.12f, 0.11f};
        const float brass[3] = {0.60f, 0.48f, 0.22f};
        const float wood[3] = {0.35f, 0.21f, 0.11f};
        const float steel[3] = {0.62f, 0.64f, 0.68f};
        const float leather[3] = {0.24f, 0.15f, 0.09f};

        Params pistol;
        pistol.kind = Params::Pistol, pistol.length = 0.30f, pistol.seed = 11u;
        col(pistol, gunMetal, darkGrip, brass);
        v.push_back(pistol);

        Params revolver;
        revolver.kind = Params::Revolver, revolver.length = 0.34f;
        revolver.seed = 22u, revolver.wear = 0.25f;
        col(revolver, steel, wood, brass);
        v.push_back(revolver);

        Params smg;
        smg.kind = Params::Smg, smg.length = 0.52f, smg.seed = 33u;
        col(smg, gunMetal, darkGrip, gunMetal);
        v.push_back(smg);

        Params rifle;
        rifle.kind = Params::Rifle, rifle.length = 1.05f, rifle.seed = 44u;
        col(rifle, gunMetal, wood, gunMetal);
        v.push_back(rifle);

        Params shotgun;
        shotgun.kind = Params::Shotgun, shotgun.length = 0.95f;
        shotgun.seed = 55u, shotgun.bulk = 1.1f;
        col(shotgun, gunMetal, wood, brass);
        v.push_back(shotgun);

        Params knife;
        knife.kind = Params::Knife, knife.length = 0.28f, knife.seed = 66u;
        col(knife, steel, darkGrip, gunMetal);
        v.push_back(knife);

        Params sword;
        sword.kind = Params::Sword, sword.length = 0.95f, sword.seed = 77u;
        col(sword, steel, leather, brass);
        v.push_back(sword);

        Params axe;
        axe.kind = Params::Axe, axe.length = 0.75f, axe.seed = 88u;
        axe.wear = 0.35f;
        col(axe, steel, wood, gunMetal);
        v.push_back(axe);

        Params crowbar;
        crowbar.kind = Params::Crowbar, crowbar.length = 0.80f;
        crowbar.seed = 99u, crowbar.wear = 0.5f;
        const float rust[3] = {0.45f, 0.26f, 0.14f};
        col(crowbar, rust, rust, rust);
        v.push_back(crowbar);
        return v;
    }();
    return list;
}

bool writeAssets(const std::string& projectDir, const std::string& name,
                 const Params& p, const Mesh& mesh, std::string* outObjRel,
                 std::string* outError) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(projectDir) / "res" / "models" / "weapons";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (outError) *outError = "Could not create " + dir.generic_string();
        return false;
    }

    {
        std::ofstream mtl(dir / (name + ".mtl"));
        if (!mtl) {
            if (outError) *outError = "Could not write " + name + ".mtl";
            return false;
        }
        char buf[128];
        mtl << "# Generated by TyraX Weapon Generator ("
            << kindName(p.kind) << ", seed " << p.seed << ")\n";
        auto emit = [&](const char* n, const float* c) {
            std::snprintf(buf, sizeof(buf), "\nnewmtl %s\nKd %.4f %.4f %.4f\n", n,
                          c[0], c[1], c[2]);
            mtl << buf;
        };
        emit("metal", p.metal);
        emit("grip", p.grip);
        emit("accent", p.accent);
    }

    std::ofstream obj(dir / (name + ".obj"));
    if (!obj) {
        if (outError) *outError = "Could not write " + name + ".obj";
        // Take the .mtl back down with it. A Wavefront .mtl with no .obj
        // beside it is a broken sibling pair, and the caller's name-uniquing
        // loop only probes for the .obj - so a retry would reuse this base
        // name and silently overwrite the orphan instead of stepping past it.
        std::error_code rmEc;
        std::filesystem::remove(dir / (name + ".mtl"), rmEc);
        return false;
    }
    obj << "# Generated by TyraX Weapon Generator (" << kindName(p.kind)
        << ", seed " << p.seed << ", " << mesh.triangles() << " triangles)\n"
        << "# +Z is the muzzle/blade direction, the origin is the grip.\n"
        << "mtllib " << name << ".mtl\n";

    // Positions/UVs deduped by exact bits, like treegen: parts share corners
    // and the file shrinks several-fold with no welding heuristics.
    std::map<std::array<uint32_t, 3>, int> vIndex;
    std::map<std::array<uint32_t, 2>, int> tIndex;
    std::vector<std::array<float, 3>> vList;
    std::vector<std::array<float, 2>> tList;
    auto faceCorner = [&](const float* vtx, std::string& line) {
        std::array<uint32_t, 3> vk;
        std::memcpy(vk.data(), vtx, 12);
        auto vi = vIndex.find(vk);
        if (vi == vIndex.end()) {
            vList.push_back({vtx[0], vtx[1], vtx[2]});
            vi = vIndex.emplace(vk, (int)vList.size()).first;
        }
        const float t[2] = {vtx[6], 1.0f - vtx[7]};  // back to OBJ v-space
        std::array<uint32_t, 2> tk;
        std::memcpy(tk.data(), t, 8);
        auto ti = tIndex.find(tk);
        if (ti == tIndex.end()) {
            tList.push_back({t[0], t[1]});
            ti = tIndex.emplace(tk, (int)tList.size()).first;
        }
        char buf[48];
        std::snprintf(buf, sizeof(buf), " %d/%d", vi->second, ti->second);
        line += buf;
    };
    auto facesOf = [&](const std::vector<float>& tris, std::string& faces) {
        for (size_t i = 0; i + 23 < tris.size(); i += 24) {
            std::string line = "f";
            faceCorner(&tris[i], line);
            faceCorner(&tris[i + 8], line);
            faceCorner(&tris[i + 16], line);
            faces += line + "\n";
        }
    };
    std::string fMetal, fGrip, fAccent;
    facesOf(mesh.metal, fMetal);
    facesOf(mesh.grip, fGrip);
    facesOf(mesh.accent, fAccent);

    char buf[96];
    for (const auto& v : vList) {
        std::snprintf(buf, sizeof(buf), "v %.5f %.5f %.5f\n", v[0], v[1], v[2]);
        obj << buf;
    }
    for (const auto& t : tList) {
        std::snprintf(buf, sizeof(buf), "vt %.5f %.5f\n", t[0], t[1]);
        obj << buf;
    }
    if (!fMetal.empty()) obj << "usemtl metal\n" << fMetal;
    if (!fGrip.empty()) obj << "usemtl grip\n" << fGrip;
    if (!fAccent.empty()) obj << "usemtl accent\n" << fAccent;
    if (!obj.good()) {
        if (outError) *outError = "Write failed on " + name + ".obj";
        return false;
    }
    obj.close();

    if (outObjRel) *outObjRel = "res/models/weapons/" + name + ".obj";
    return true;
}

}  // namespace weapongen
