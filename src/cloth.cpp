#include "cloth.hpp"

#include <cmath>

// The solver. Read cloth.hpp first - the three properties documented there
// (one particle = one quadword, four independent constraint batches, a fixed
// step) are what every function below is arranged around, and the generated
// PS2 twin in templates.cpp reproduces this file line for line with
// Tyra::Vec4. Nothing here allocates once the state is sized.
namespace cloth {
namespace {

// How fast the gust breathes, radians/second, and how far apart two rows sit
// in its phase. Both are look, not physics - they only have to agree with the
// generated twin.
constexpr float kGustRate = 2.1f;
constexpr float kGustRowPhase = 0.9f;
// The gust never reverses: a curtain blown from one side stays blown, it just
// breathes. (base, swing) - base - swing >= 0 is the property to keep.
constexpr float kGustBase = 0.65f;
constexpr float kGustSwing = 0.35f;

// One distance constraint, and the only place in the solver that divides.
//
// `f` is the FRACTION of the separation vector that has to disappear, so the
// correction is `d * f` and needs no second normalize. A pinned endpoint takes
// none of it and the other takes all - which is what makes the top row of a
// curtain a rail rather than a heavy hem.
inline void satisfy(V4* pos, const unsigned char* pinned, int ia, int ib,
                    float rest) {
    const V4 d = v4sub(pos[ib], pos[ia]);
    const float len2 = v4dot3(d, d);
    // Two particles exactly on top of each other have no direction to be
    // pushed apart along; leaving them is correct - the next step's gravity
    // separates them.
    if (len2 < 1e-12f) return;
    const float len = std::sqrt(len2);
    const float f = (len - rest) / len;
    const bool pa = pinned[ia] != 0, pb = pinned[ib] != 0;
    if (pa) {
        if (!pb) pos[ib] = v4madd(pos[ib], d, -f);
        return;
    }
    if (pb) {
        pos[ia] = v4madd(pos[ia], d, f);
        return;
    }
    const float h = f * 0.5f;
    pos[ia] = v4madd(pos[ia], d, h);
    pos[ib] = v4madd(pos[ib], d, -h);
}

// Is particle (r, c) nailed? The one place the Pin enum is interpreted, so a
// new mode is a case here plus a row in the editor's combo and the generated
// table's comment.
bool pinnedAt(Pin pin, int r, int c, int cols, int rows) {
    switch (pin) {
        case Pin::None: return false;
        case Pin::TopEdge: return r == 0;
        case Pin::TopCorners: return r == 0 && (c == 0 || c == cols - 1);
        case Pin::TopBottom: return r == 0 || r == rows - 1;
        case Pin::LeftEdge: return c == 0;
        case Pin::Corners:
            return (r == 0 || r == rows - 1) && (c == 0 || c == cols - 1);
    }
    return false;
}

}  // namespace

V4 windAt(const Wind* sources, int count, const V4& point) {
    V4 total{};
    for (int i = 0; i < count; ++i) {
        const Wind& w = sources[i];
        if (w.strength == 0.0f) continue;
        float f = 1.0f;
        if (w.radius > 0.0f) {
            const V4 d = v4sub(point, w.pos);
            const float d2 = v4dot3(d, d);
            const float r2 = w.radius * w.radius;
            if (d2 >= r2) continue;  // out of reach
            f = 1.0f - d2 / r2;
        }
        total = v4madd(total, w.dir, w.strength * f);
    }
    return total;
}

V4 v4normalize(const V4& a) {
    const float l2 = v4dot3(a, a);
    if (l2 < 1e-20f) return V4{0.0f, 1.0f, 0.0f, 0.0f};
    return v4scale(a, 1.0f / std::sqrt(l2));
}

void reset(const Params& p, const V4& origin, const V4& right, const V4& down,
           State& s) {
    const int cols = clampSide(p.cols), rows = clampSide(p.rows);
    const int n = cols * rows;
    s.pos.assign((size_t)n, V4{});
    s.prev.assign((size_t)n, V4{});
    s.pinned.assign((size_t)n, 0);
    s.nrm.assign((size_t)n, V4{});
    s.rowSin.assign((size_t)rows, 0.0f);
    s.rowCos.assign((size_t)rows, 0.0f);
    s.time = 0.0f;
    s.carry = 0.0f;
    for (int r = 0; r < rows; ++r) {
        s.rowSin[(size_t)r] = std::sin((float)r * kGustRowPhase);
        s.rowCos[(size_t)r] = std::cos((float)r * kGustRowPhase);
        const V4 rowOrigin = v4madd(origin, down, (float)r * p.restY);
        for (int c = 0; c < cols; ++c) {
            const int i = r * cols + c;
            s.pos[(size_t)i] = v4madd(rowOrigin, right, (float)c * p.restX);
            // prev == pos: a sheet that has just been laid out is at rest,
            // whatever it was doing before.
            s.prev[(size_t)i] = s.pos[(size_t)i];
            s.pinned[(size_t)i] =
                pinnedAt(p.pin, r, c, cols, rows) ? (unsigned char)1
                                                  : (unsigned char)0;
        }
    }
}

void anchor(const Params& p, const V4& origin, const V4& right, const V4& down,
            State& s) {
    const int cols = clampSide(p.cols), rows = clampSide(p.rows);
    if ((int)s.pos.size() != cols * rows) return;
    for (int r = 0; r < rows; ++r) {
        const V4 rowOrigin = v4madd(origin, down, (float)r * p.restY);
        for (int c = 0; c < cols; ++c) {
            const int i = r * cols + c;
            if (!s.pinned[(size_t)i]) continue;
            s.pos[(size_t)i] = v4madd(rowOrigin, right, (float)c * p.restX);
            s.prev[(size_t)i] = s.pos[(size_t)i];
        }
    }
}

void step(const Params& p, const Capsule* bodies, int bodyCount, State& s) {
    const int cols = clampSide(p.cols), rows = clampSide(p.rows);
    const int n = cols * rows;
    if ((int)s.pos.size() != n) return;  // never simulated against a stale grid

    V4* pos = s.pos.data();
    V4* prev = s.prev.data();
    const unsigned char* pinned = s.pinned.data();

    // --- integrate ---------------------------------------------------------
    // Verlet: the velocity IS (pos - prev), so damping is a scale on it and
    // the acceleration lands as one madd. Three vector ops per particle.
    const float h2 = kStepSeconds * kStepSeconds;
    const float keep = 1.0f - (p.damping < 0.0f ? 0.0f
                               : p.damping > 1.0f ? 1.0f
                                                  : p.damping);
    s.time += kStepSeconds;
    // ONE sinf/cosf for the whole sheet: each row's gust is the angle-addition
    // of the global phase with its own baked offset.
    const float gs = std::sin(s.time * kGustRate);
    const float gc = std::cos(s.time * kGustRate);
    for (int r = 0; r < rows; ++r) {
        // The gust envelope, applied to the summed wind vector. One scalar
        // per row, so the whole acceleration term is still one constant vector
        // per row - gravity plus this row's share of the wind, already
        // multiplied by h^2.
        const float gust = kGustBase + kGustSwing * (gs * s.rowCos[(size_t)r] +
                                                     gc * s.rowSin[(size_t)r]);
        const V4 acc{p.windAccel.x * gust * h2,
                     (-p.gravity + p.windAccel.y * gust) * h2,
                     p.windAccel.z * gust * h2, 0.0f};
        for (int c = 0; c < cols; ++c) {
            const int i = r * cols + c;
            if (pinned[i]) continue;
            const V4 cur = pos[i];
            const V4 vel = v4scale(v4sub(cur, prev[i]), keep);
            prev[i] = cur;
            pos[i] = v4add(v4add(cur, vel), acc);
        }
    }

    // --- relax -------------------------------------------------------------
    // Four batches, each internally independent (see cloth.hpp, property 2).
    const int iters = p.iterations < 1 ? 1 : (p.iterations > 4 ? 4 : p.iterations);
    for (int it = 0; it < iters; ++it) {
        for (int parity = 0; parity < 2; ++parity) {
            for (int r = 0; r < rows; ++r)
                for (int c = parity; c + 1 < cols; c += 2)
                    satisfy(pos, pinned, r * cols + c, r * cols + c + 1, p.restX);
        }
        for (int parity = 0; parity < 2; ++parity) {
            for (int r = parity; r + 1 < rows; r += 2)
                for (int c = 0; c < cols; ++c)
                    satisfy(pos, pinned, r * cols + c, (r + 1) * cols + c, p.restY);
        }
    }

    // --- collide -----------------------------------------------------------
    // Positions are moved and `prev` is NOT, which is the whole point: the
    // displacement becomes velocity on the next step, so a player walking
    // through a curtain lifts it and leaves it swinging instead of dragging a
    // rigid hole through it.
    for (int k = 0; k < bodyCount; ++k) {
        const Capsule& cap = bodies[k];
        if (cap.r <= 0.0f) continue;
        const float r2 = cap.r * cap.r;
        const V4 ab = v4sub(cap.b, cap.a);
        const float abLen2 = v4dot3(ab, ab);
        const float invAb = abLen2 > 1e-12f ? 1.0f / abLen2 : 0.0f;
        for (int i = 0; i < n; ++i) {
            if (pinned[i]) continue;
            // Closest point on the segment, then a radial push off it. One
            // test for the whole body, and no seam a particle can be pushed
            // INTO the way stacked spheres have.
            float t = v4dot3(v4sub(pos[i], cap.a), ab) * invAb;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            const V4 c = v4madd(cap.a, ab, t);
            const V4 d = v4sub(pos[i], c);
            const float l2 = v4dot3(d, d);
            if (l2 >= r2) continue;
            if (l2 < 1e-10f) {
                // Dead on the axis: no direction to leave along. Any fixed one
                // would do; +Y keeps a sheet on top of what walked into it.
                pos[i] = V4{c.x, c.y + cap.r, c.z, 0.0f};
                continue;
            }
            pos[i] = v4madd(c, d, cap.r / std::sqrt(l2));
        }
    }
}

int advance(const Params& p, const V4& origin, const V4& right, const V4& down,
            float dt, const Capsule* bodies, int bodyCount, State& s) {
    if (!(dt > 0.0f)) return 0;  // also catches NaN
    const float cap = kStepSeconds * (float)kMaxStepsPerAdvance;
    s.carry += dt > cap ? cap : dt;
    int ran = 0;
    while (s.carry >= kStepSeconds && ran < kMaxStepsPerAdvance) {
        s.carry -= kStepSeconds;
        anchor(p, origin, right, down, s);
        step(p, bodies, bodyCount, s);
        ++ran;
    }
    return ran;
}

void buildMesh(const Params& p, State& s, std::vector<Vertex>& out) {
    const int cols = clampSide(p.cols), rows = clampSide(p.rows);
    const int n = cols * rows;
    out.clear();
    if ((int)s.pos.size() != n) return;
    if ((int)s.nrm.size() != n) s.nrm.assign((size_t)n, V4{});

    const V4* pos = s.pos.data();
    V4* nrm = s.nrm.data();

    // Smoothed per-particle normals from central differences, clamped to the
    // edges. cross(down, right) points along the sheet's front, because the
    // grid runs right along +X and down along -Y (see reset()).
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int i = r * cols + c;
            const int cl = c > 0 ? c - 1 : c, cr = c + 1 < cols ? c + 1 : c;
            const int ru = r > 0 ? r - 1 : r, rd = r + 1 < rows ? r + 1 : r;
            const V4 tx = v4sub(pos[r * cols + cr], pos[r * cols + cl]);
            const V4 ty = v4sub(pos[rd * cols + c], pos[ru * cols + c]);
            nrm[i] = v4normalize(V4{ty.y * tx.z - ty.z * tx.y,
                                    ty.z * tx.x - ty.x * tx.z,
                                    ty.x * tx.y - ty.y * tx.x, 0.0f});
        }
    }

    const float du = cols > 1 ? 1.0f / (float)(cols - 1) : 1.0f;
    const float dv = rows > 1 ? 1.0f / (float)(rows - 1) : 1.0f;
    out.reserve((size_t)triangleCount(p) * 3);
    auto push = [&](int r, int c) {
        const int i = r * cols + c;
        Vertex v;
        v.pos = pos[i];
        v.nrm = nrm[i];
        v.u = (float)c * du;
        v.v = (float)r * dv;
        out.push_back(v);
    };
    for (int r = 0; r + 1 < rows; ++r) {
        for (int c = 0; c + 1 < cols; ++c) {
            // CCW seen from the front: top-left, bottom-left, bottom-right,
            // then top-left, bottom-right, top-right.
            push(r, c);
            push(r + 1, c);
            push(r + 1, c + 1);
            push(r, c);
            push(r + 1, c + 1);
            push(r, c + 1);
        }
    }
}

}  // namespace cloth
