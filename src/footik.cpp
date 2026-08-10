#include "footik.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace footik {

namespace {

constexpr float kPi = 3.14159265358979f;

std::string lower(const std::string& s) {
    std::string r = s;
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return r;
}

bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// --- vector / matrix helpers, column-major 4x4 (m[12..14] = translation) ---

void v3sub(const float* a, const float* b, float* r) {
    r[0] = a[0] - b[0], r[1] = a[1] - b[1], r[2] = a[2] - b[2];
}
float v3dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void v3cross(const float* a, const float* b, float* r) {
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
}
float v3len(const float* a) { return std::sqrt(v3dot(a, a)); }
float v3norm(float* a) {
    const float l = v3len(a);
    if (l > 1e-8f) a[0] /= l, a[1] /= l, a[2] /= l;
    return l;
}
void xformDir(const float* m, const float* v, float* r) {
    r[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2];
    r[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2];
    r[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2];
}
void xformPoint(const float* m, const float* v, float* r) {
    r[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12];
    r[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13];
    r[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14];
}
void mulM4(float* r, const float* a, const float* b) {
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float acc = 0.0f;
            for (int k = 0; k < 4; ++k) acc += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = acc;
        }
}
void fromTrs(float* m, const float* t, const float* q, const float* s) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x * x2, xy = x * y2, xz = x * z2;
    const float yy = y * y2, yz = y * z2, zz = z * z2;
    const float wx = w * x2, wy = w * y2, wz = w * z2;
    m[0] = (1 - (yy + zz)) * s[0], m[1] = (xy + wz) * s[0],
    m[2] = (xz - wy) * s[0], m[3] = 0;
    m[4] = (xy - wz) * s[1], m[5] = (1 - (xx + zz)) * s[1],
    m[6] = (yz + wx) * s[1], m[7] = 0;
    m[8] = (xz + wy) * s[2], m[9] = (yz - wx) * s[2],
    m[10] = (1 - (xx + yy)) * s[2], m[11] = 0;
    m[12] = t[0], m[13] = t[1], m[14] = t[2], m[15] = 1;
}
bool invertAffine(const float* m, float* out) {
    const float a = m[0], b = m[4], c = m[8];
    const float d = m[1], e = m[5], f = m[9];
    const float g = m[2], h = m[6], i = m[10];
    const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.0f / det;
    out[0] = A * inv, out[1] = B * inv, out[2] = C * inv, out[3] = 0;
    out[4] = (c * h - b * i) * inv, out[5] = (a * i - c * g) * inv,
    out[6] = (b * g - a * h) * inv, out[7] = 0;
    out[8] = (b * f - c * e) * inv, out[9] = (c * d - a * f) * inv,
    out[10] = (a * e - b * d) * inv, out[11] = 0;
    const float tx = m[12], ty = m[13], tz = m[14];
    out[12] = -(out[0] * tx + out[4] * ty + out[8] * tz);
    out[13] = -(out[1] * tx + out[5] * ty + out[9] * tz);
    out[14] = -(out[2] * tx + out[6] * ty + out[10] * tz);
    out[15] = 1.0f;
    return true;
}
void rotateAbout(const float* v, const float* axis, float ang, float* r) {
    const float c = std::cos(ang), s = std::sin(ang);
    float cr[3];
    v3cross(axis, v, cr);
    const float d = v3dot(axis, v) * (1.0f - c);
    for (int k = 0; k < 3; ++k) r[k] = v[k] * c + cr[k] * s + axis[k] * d;
}
void rotFromTo(const float* from, const float* to, float* R) {
    float axis[3];
    v3cross(from, to, axis);
    float dot = std::max(-1.0f, std::min(1.0f, v3dot(from, to)));
    if (v3norm(axis) < 1e-7f) {
        if (dot > 0.0f) {
            std::memset(R, 0, 9 * sizeof(float));
            R[0] = R[4] = R[8] = 1.0f;
            return;
        }
        float pick[3] = {1, 0, 0};
        if (std::fabs(from[0]) > 0.9f) pick[0] = 0, pick[1] = 1;
        v3cross(from, pick, axis);
        v3norm(axis);
        dot = -1.0f;
    }
    const float ang = std::acos(dot), c = std::cos(ang), s = std::sin(ang);
    const float t = 1.0f - c, x = axis[0], y = axis[1], z = axis[2];
    R[0] = t * x * x + c, R[1] = t * x * y + s * z, R[2] = t * x * z - s * y;
    R[3] = t * x * y - s * z, R[4] = t * y * y + c, R[5] = t * y * z + s * x;
    R[6] = t * x * z + s * y, R[7] = t * y * z - s * x, R[8] = t * z * z + c;
}
void rot3Apply(const float* R, const float* v, float* r) {
    r[0] = R[0] * v[0] + R[3] * v[1] + R[6] * v[2];
    r[1] = R[1] * v[0] + R[4] * v[1] + R[7] * v[2];
    r[2] = R[2] * v[0] + R[5] * v[1] + R[8] * v[2];
}
void rotateJoint(float* m, const float* R, const float* pivot) {
    for (int col = 0; col < 3; ++col) {
        float v[3] = {m[col * 4], m[col * 4 + 1], m[col * 4 + 2]}, r[3];
        rot3Apply(R, v, r);
        m[col * 4] = r[0], m[col * 4 + 1] = r[1], m[col * 4 + 2] = r[2];
    }
    float rel[3] = {m[12] - pivot[0], m[13] - pivot[1], m[14] - pivot[2]}, rr[3];
    rot3Apply(R, rel, rr);
    m[12] = pivot[0] + rr[0], m[13] = pivot[1] + rr[1], m[14] = pivot[2] + rr[2];
}
void springTo(float* x, float* v, float target, float rate, float dt) {
    if (dt <= 0.0f || rate <= 0.0f) {
        *x = target, *v = 0.0f;
        return;
    }
    if (dt > 0.1f) dt = 0.1f;
    const float a = -2.0f * rate * (*v) - rate * rate * (*x - target);
    *v += a * dt;
    *x += (*v) * dt;
}
float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// --- clip sampling, mirroring SkelInstance ---

void slerp(const float* a, const float* bIn, float t, float* out) {
    float b[4] = {bIn[0], bIn[1], bIn[2], bIn[3]};
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0.0f) {
        dot = -dot;
        for (int i = 0; i < 4; ++i) b[i] = -b[i];
    }
    float wa, wb;
    if (dot > 0.9995f) {
        wa = 1.0f - t, wb = t;
    } else {
        const float theta = std::acos(std::min(1.0f, dot));
        const float st = std::sin(theta);
        wa = std::sin((1.0f - t) * theta) / st;
        wb = std::sin(t * theta) / st;
    }
    float len = 0.0f;
    for (int i = 0; i < 4; ++i) out[i] = wa * a[i] + wb * b[i], len += out[i] * out[i];
    len = std::sqrt(len);
    if (len > 1e-6f)
        for (int i = 0; i < 4; ++i) out[i] /= len;
    else
        out[3] = 1.0f;
}

void sampleChannel(const glbparser::SkelChannel& ch, float t, float* out) {
    const size_t n = ch.times.size();
    if (n == 0) return;
    size_t hi = 0;
    while (hi < n && ch.times[hi] < t) ++hi;

    // Rotations are plain floats here; the .tskl quantizes them to s16 on the
    // way out, which is the one numerical difference between this and the
    // console's sampler. It is under a ULP of a degree and never visible.
    if (ch.path == 1) {
        const size_t lo = hi == 0 ? 0 : hi - 1;
        const size_t hiC = hi >= n ? n - 1 : hi;
        float a[4], b[4];
        for (int c = 0; c < 4; ++c) {
            a[c] = ch.values[lo * 4 + c];
            b[c] = ch.values[hiC * 4 + c];
        }
        if (hi == 0 || hi >= n || lo == hiC) {
            const float* src = hi == 0 ? a : b;
            float len = 0.0f;
            for (int i = 0; i < 4; ++i) out[i] = src[i], len += src[i] * src[i];
            len = std::sqrt(len);
            if (len > 1e-6f)
                for (int i = 0; i < 4; ++i) out[i] /= len;
            return;
        }
        const float t0 = ch.times[lo], t1 = ch.times[hi];
        float f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0f;
        if (ch.step) f = 0.0f;
        slerp(a, b, f, out);
        return;
    }

    if (hi == 0) {
        std::memcpy(out, &ch.values[0], 3 * sizeof(float));
        return;
    }
    if (hi >= n) {
        std::memcpy(out, &ch.values[(n - 1) * 3], 3 * sizeof(float));
        return;
    }
    const size_t lo = hi - 1;
    const float t0 = ch.times[lo], t1 = ch.times[hi];
    float f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0f;
    if (ch.step) f = 0.0f;
    for (int c = 0; c < 3; ++c)
        out[c] = ch.values[lo * 3 + c] +
                 (ch.values[hi * 3 + c] - ch.values[lo * 3 + c]) * f;
}

// Parents-first traversal order. The parsers emit parents before children,
// but a hand-edited or exotic file need not, and a wrong order silently
// produces a pose built on stale parents.
std::vector<int> traversalOrder(const glbparser::Skel& skel) {
    const int n = (int)skel.nodes.size();
    std::vector<int> order;
    std::vector<char> done(n, 0);
    order.reserve(n);
    bool progress = true;
    while ((int)order.size() < n && progress) {
        progress = false;
        for (int i = 0; i < n; ++i) {
            if (done[i]) continue;
            const int p = skel.nodes[i].parent;
            if (p >= 0 && !done[p]) continue;
            done[i] = 1;
            order.push_back(i);
            progress = true;
        }
    }
    for (int i = 0; i < n; ++i)
        if (!done[i]) order.push_back(i);  // a cycle: emit anyway, never hang
    return order;
}

}  // namespace

int findNode(const glbparser::Skel& skel, const std::string& name) {
    if (name.empty()) return -1;
    for (size_t i = 0; i < skel.nodes.size(); ++i)
        if (skel.nodes[i].name == name) return (int)i;
    const std::string want = lower(name);
    for (size_t i = 0; i < skel.nodes.size(); ++i)
        if (lower(skel.nodes[i].name) == want) return (int)i;
    return -1;
}

Resolved resolve(const AnimRig& rig, const glbparser::Skel& skel) {
    Resolved r;
    auto need = [&](const std::string& name, const char* what) {
        const int idx = findNode(skel, name);
        if (idx < 0)
            r.problems.push_back(std::string(what) + " bone \"" + name +
                                 "\" is not in this model");
        return idx;
    };

    for (size_t i = 0; i < rig.legs.size() && i < 4; ++i) {
        const AnimRigLeg& g = rig.legs[i];
        Resolved::Leg leg;
        const std::string side = "leg " + std::to_string(i + 1) + " ";
        leg.hip = need(g.hip, (side + "hip").c_str());
        leg.knee = need(g.knee, (side + "knee").c_str());
        leg.ankle = need(g.ankle, (side + "ankle").c_str());
        leg.toe = g.toe.empty() ? -1 : findNode(skel, g.toe);
        // A chain the hierarchy does not actually connect solves into
        // nonsense - the solver assumes hip is an ancestor of ankle.
        if (leg.hip >= 0 && leg.knee >= 0 && leg.ankle >= 0) {
            auto descends = [&](int child, int ancestor) {
                for (int n = child; n >= 0; n = skel.nodes[n].parent)
                    if (n == ancestor) return true;
                return false;
            };
            if (!descends(leg.knee, leg.hip) || !descends(leg.ankle, leg.knee))
                r.problems.push_back(side +
                                     "chain is not connected: the ankle must "
                                     "descend from the knee, and the knee from "
                                     "the hip");
        }
        r.legs.push_back(leg);
    }
    if (r.legs.empty()) r.problems.push_back("no legs bound");

    if (!rig.pelvis.empty()) {
        r.pelvis = need(rig.pelvis, "pelvis");
        // Lowering something that is not above the legs moves the wrong half
        // of the character - worth refusing rather than watching it happen.
        if (r.pelvis >= 0)
            for (const Resolved::Leg& leg : r.legs)
                if (leg.hip >= 0) {
                    bool under = false;
                    for (int n = leg.hip; n >= 0; n = skel.nodes[n].parent)
                        if (n == r.pelvis) {
                            under = true;
                            break;
                        }
                    if (!under) {
                        r.problems.push_back(
                            "the pelvis bone is not an ancestor of every hip - "
                            "lowering it would not move the legs");
                        break;
                    }
                }
    }

    for (const std::string& j : rig.netJoints) {
        const int idx = findNode(skel, j);
        if (idx < 0)
            r.problems.push_back("gait-net joint \"" + j +
                                 "\" is not in this model");
        r.netJoints.push_back(idx);
    }
    return r;
}

AnimRig autoDetect(const std::string& modelRel, const glbparser::Skel& skel) {
    AnimRig rig;
    rig.model = modelRel;

    // Side detection, in priority order. "left"/"right" first because a bone
    // called "LeftUpLeg" also ends in no side suffix, and the suffix rules
    // would otherwise never fire on Mixamo rigs.
    auto sideOf = [](const std::string& lo) -> int {
        if (contains(lo, "left")) return 0;
        if (contains(lo, "right")) return 1;
        const size_t n = lo.size();
        if (n >= 2) {
            const char c = lo[n - 1], p = lo[n - 2];
            if ((p == '_' || p == '.' || p == '-') && (c == 'l')) return 0;
            if ((p == '_' || p == '.' || p == '-') && (c == 'r')) return 1;
        }
        return -1;
    };
    // Ankle before toe, and knee before hip: "upleg" contains "leg", and
    // "toebase" contains no "foot" but "foottoe" would. Longest, most
    // specific spellings are tested first inside each role.
    auto roleOf = [](const std::string& lo) -> int {
        if (contains(lo, "toe") || contains(lo, "ball")) return 3;
        if (contains(lo, "foot") || contains(lo, "ankle")) return 2;
        if (contains(lo, "calf") || contains(lo, "shin") ||
            contains(lo, "knee") || contains(lo, "lowerleg") ||
            contains(lo, "lowleg") || contains(lo, "leg02"))
            return 1;
        if (contains(lo, "upleg") || contains(lo, "thigh") ||
            contains(lo, "upperleg") || contains(lo, "hip") ||
            contains(lo, "leg01"))
            return 0;
        // "leg" on its own is ambiguous - Mixamo uses LeftLeg for the SHIN.
        // Deciding it by the hierarchy instead of by the name is the only
        // reliable answer, so leave it to the pass below.
        if (contains(lo, "leg")) return 4;
        return -1;
    };

    struct Cand {
        int hip = -1, knee = -1, ankle = -1, toe = -1;
    };
    Cand side[2];
    std::vector<int> ambiguous[2];

    for (size_t i = 0; i < skel.nodes.size(); ++i) {
        const std::string lo = lower(skel.nodes[i].name);
        if (lo.empty()) continue;
        const int s = sideOf(lo);
        if (s < 0) continue;
        switch (roleOf(lo)) {
            case 0: if (side[s].hip < 0) side[s].hip = (int)i; break;
            case 1: if (side[s].knee < 0) side[s].knee = (int)i; break;
            case 2: if (side[s].ankle < 0) side[s].ankle = (int)i; break;
            case 3: if (side[s].toe < 0) side[s].toe = (int)i; break;
            case 4: ambiguous[s].push_back((int)i); break;
            default: break;
        }
    }

    // A bare "…Leg" bone becomes the hip when nothing else claimed it, and
    // the knee when it descends from an already-found hip. That is exactly
    // the Mixamo case (LeftUpLeg -> LeftLeg -> LeftFoot).
    auto descends = [&](int child, int ancestor) {
        for (int n = child; n >= 0; n = skel.nodes[n].parent)
            if (n == ancestor) return true;
        return false;
    };
    for (int s = 0; s < 2; ++s)
        for (int idx : ambiguous[s]) {
            if (side[s].hip >= 0 && side[s].knee < 0 &&
                descends(idx, side[s].hip))
                side[s].knee = idx;
            else if (side[s].hip < 0)
                side[s].hip = idx;
        }

    for (int s = 0; s < 2; ++s) {
        const Cand& c = side[s];
        if (c.hip < 0 || c.knee < 0 || c.ankle < 0) continue;
        AnimRigLeg leg;
        leg.hip = skel.nodes[c.hip].name;
        leg.knee = skel.nodes[c.knee].name;
        leg.ankle = skel.nodes[c.ankle].name;
        if (c.toe >= 0) leg.toe = skel.nodes[c.toe].name;
        rig.legs.push_back(leg);
    }

    // Pelvis: the deepest common ancestor of the hips is exactly the bone a
    // drop has to move, whatever it happens to be called.
    if (rig.legs.size() >= 2 && side[0].hip >= 0 && side[1].hip >= 0) {
        std::vector<int> chain;
        for (int n = skel.nodes[side[0].hip].parent; n >= 0;
             n = skel.nodes[n].parent)
            chain.push_back(n);
        for (int n : chain)
            if (descends(side[1].hip, n)) {
                rig.pelvis = skel.nodes[n].name;
                break;
            }
    } else if (rig.legs.size() == 1 && side[0].hip >= 0) {
        const int p = skel.nodes[side[0].hip].parent;
        if (p >= 0) rig.pelvis = skel.nodes[p].name;
    }

    if (!rig.legs.empty()) {
        Resolved r = resolve(rig, skel);
        rig.soleOffset = measureSoleOffset(skel, r);
        // The joints worth handing the gait net beyond the legs: the pelvis
        // carries the weight shift, the lower spine the lean. The spine is
        // BELOW the pelvis in the hierarchy, not above it - every common rig
        // roots the legs and the spine at the same bone, so walking up from
        // the pelvis (the first thing tried here) finds the armature root and
        // nothing else.
        if (!rig.pelvis.empty()) rig.netJoints.push_back(rig.pelvis);
        if (r.pelvis >= 0) {
            // Spine bones hanging off the pelvis, nearest first. Two links is
            // enough for a lean and few enough that the net cannot start
            // waving the arms around.
            std::vector<std::pair<int, int>> spine;  // (depth, node)
            for (size_t i = 0; i < skel.nodes.size(); ++i) {
                const std::string lo = lower(skel.nodes[i].name);
                if (!contains(lo, "spine") && !contains(lo, "chest") &&
                    !contains(lo, "torso"))
                    continue;
                int depth = 0;
                int n = (int)i;
                for (; n >= 0 && n != r.pelvis; n = skel.nodes[n].parent) ++depth;
                // A second, unrelated "spine" (a prop, a cape rig) does not
                // descend from the pelvis and must not reach the net.
                if (n == r.pelvis) spine.emplace_back(depth, (int)i);
            }
            std::sort(spine.begin(), spine.end());
            for (size_t i = 0; i < spine.size() && i < 2; ++i)
                rig.netJoints.push_back(skel.nodes[spine[i].second].name);
        }
    }
    return rig;  // deliberately NOT enabled - a guess is a starting point
}

float measureSoleOffset(const glbparser::Skel& skel, const Resolved& r) {
    if (r.legs.empty()) return 0.08f;

    // Bind-pose globals, so the measurement is of the model as authored.
    Pose bind(skel.nodes.size() * 16, 0.0f);
    for (int i : traversalOrder(skel)) {
        const glbparser::SkelNode& n = skel.nodes[i];
        float local[16];
        if (n.hasMatrix)
            std::memcpy(local, n.matrix, sizeof(local));
        else
            fromTrs(local, n.t, n.r, n.s);
        if (n.parent >= 0)
            mulM4(&bind[i * 16], &bind[n.parent * 16], local);
        else
            std::memcpy(&bind[i * 16], local, sizeof(local));
    }

    // Lowest vertex weighted to the ankle (or the toe) of any leg, against
    // that leg's ankle height. Measuring beats guessing: a boot and a bare
    // foot differ by centimetres, and centimetres are what a sunk heel is.
    float best = 0.0f;
    bool found = false;
    for (const Resolved::Leg& leg : r.legs) {
        if (leg.ankle < 0) continue;
        const float ankleY = bind[leg.ankle * 16 + 13];
        float lowest = ankleY;
        for (const glbparser::SkelPart& part : skel.parts) {
            for (int v = 0; v < part.vertexCount; ++v) {
                bool onFoot = false;
                for (int k = 0; k < 4; ++k) {
                    const unsigned char w = part.weights[v * 4 + k];
                    if (w < 96) continue;  // a token weight is not "this bone"
                    const int slot = part.joints[v * 4 + k];
                    if (slot < 0 || slot >= (int)skel.palette.size()) continue;
                    const int node = skel.palette[slot].node;
                    if (node == leg.ankle || (leg.toe >= 0 && node == leg.toe))
                        onFoot = true;
                }
                if (!onFoot) continue;
                // The vertex is authored in bind space already (the .tskl
                // stores the bind-pose mesh), so its Y is directly comparable.
                const float y = part.positions[v * 3 + 1];
                if (y < lowest) lowest = y;
            }
        }
        const float drop = ankleY - lowest;
        if (drop > 1e-4f && (!found || drop < best)) best = drop, found = true;
    }
    if (found) return best;

    for (const Resolved::Leg& leg : r.legs)
        if (leg.ankle >= 0 && leg.toe >= 0) {
            const float drop = bind[leg.ankle * 16 + 13] - bind[leg.toe * 16 + 13];
            if (drop > 1e-4f) return drop;
        }
    return 0.08f;
}

void evalPose(const glbparser::Skel& skel, int clip, float time, Pose& out) {
    const size_t n = skel.nodes.size();
    out.assign(n * 16, 0.0f);

    std::vector<float> locals(n * 10);
    for (size_t i = 0; i < n; ++i) {
        const glbparser::SkelNode& nd = skel.nodes[i];
        float* l = &locals[i * 10];
        std::memcpy(l, nd.t, 3 * sizeof(float));
        std::memcpy(l + 3, nd.r, 4 * sizeof(float));
        std::memcpy(l + 7, nd.s, 3 * sizeof(float));
    }
    std::vector<char> animated(n, 0);
    if (clip >= 0 && clip < (int)skel.clips.size())
        for (const glbparser::SkelChannel& ch : skel.clips[clip].channels) {
            if (ch.node < 0 || ch.node >= (int)n) continue;
            float* l = &locals[(size_t)ch.node * 10];
            sampleChannel(ch, time, ch.path == 0 ? l : (ch.path == 1 ? l + 3 : l + 7));
            animated[ch.node] = 1;
        }

    for (int i : traversalOrder(skel)) {
        const glbparser::SkelNode& nd = skel.nodes[i];
        float local[16];
        if (nd.hasMatrix && !animated[i])
            std::memcpy(local, nd.matrix, sizeof(local));
        else {
            const float* l = &locals[(size_t)i * 10];
            fromTrs(local, l, l + 3, l + 7);
        }
        if (nd.parent >= 0)
            mulM4(&out[(size_t)i * 16], &out[(size_t)nd.parent * 16], local);
        else
            std::memcpy(&out[(size_t)i * 16], local, sizeof(local));
    }
}

bool StepGround::sample(const float world[3], float up, float down, float* outY,
                        float outNormal[3]) const {
    const float y = world[2] >= stepZ ? baseY + stepHeight : baseY;
    if (y > world[1] + up || y < world[1] - down) return false;
    *outY = y;
    outNormal[0] = 0.0f, outNormal[1] = 1.0f, outNormal[2] = 0.0f;
    return true;
}

void solve(const glbparser::Skel& skel, const AnimRig& rig,
           const Resolved& resolved, const Ground& ground, const float world[16],
           float dt, State& state, Pose& pose) {
    if (resolved.legs.empty() || pose.size() != skel.nodes.size() * 16) return;
    float worldInv[16];
    if (!invertAffine(world, worldInv)) return;

    // The pose as the clip produced it. Re-deriving a descendant needs its
    // LOCAL transform, and the animated one only exists as the difference
    // between two globals - the engine reads it straight out of localsCur,
    // which the host has no equivalent of. Taking it from the untouched pose
    // is what keeps an animated toe animated instead of snapping to bind.
    const Pose orig = pose;
    const std::vector<int> order = traversalOrder(skel);

    const float worldUp[3] = {0.0f, 1.0f, 0.0f};
    float upRaw[3];
    xformDir(worldInv, worldUp, upRaw);
    float upUnit[3] = {upRaw[0], upRaw[1], upRaw[2]};
    if (v3norm(upUnit) < 1e-8f) return;

    const size_t legs = std::min<size_t>(resolved.legs.size(), 4);
    float targetW[4][3];
    float deepest = 0.0f;

    for (size_t i = 0; i < legs; ++i) {
        const int ankle = resolved.legs[i].ankle;
        if (ankle < 0) continue;
        const float* am = &pose[(size_t)ankle * 16];
        float ankleM[3] = {am[12], am[13], am[14]};
        float soleM[3];
        for (int k = 0; k < 3; ++k)
            soleM[k] = ankleM[k] - upUnit[k] * rig.soleOffset;

        float soleW[3], ankleW[3];
        xformPoint(world, soleM, soleW);
        xformPoint(world, ankleM, ankleW);

        float gy = 0.0f, gn[3] = {0, 1, 0};
        state.grounded[i] =
            ground.sample(soleW, rig.traceUp, rig.traceDown, &gy, gn);
        float raw = 0.0f;
        if (state.grounded[i]) {
            raw = clampf(gy - soleW[1], -rig.traceDown, rig.maxLift);
            if (v3norm(gn) < 1e-6f) gn[0] = 0, gn[1] = 1, gn[2] = 0;
            if (gn[1] < 0.0f)
                for (int k = 0; k < 3; ++k) gn[k] = -gn[k];
            std::memcpy(state.normal[i], gn, sizeof(gn));
        } else {
            state.normal[i][0] = 0, state.normal[i][1] = 1, state.normal[i][2] = 0;
        }
        springTo(&state.offset[i], &state.offsetVel[i], raw, rig.smoothing, dt);
        if (state.offset[i] < deepest) deepest = state.offset[i];
        targetW[i][0] = ankleW[0];
        targetW[i][1] = ankleW[1] + state.offset[i];
        targetW[i][2] = ankleW[2];
    }

    state.pelvisOffset = clampf(deepest, -rig.maxDrop, 0.0f);
    if (resolved.pelvis >= 0 && state.pelvisOffset < -1e-5f) {
        // The whole subtree under the pelvis moves with it, which for a
        // resolved rig is every hip - resolve() refuses a pelvis for which
        // that is not true.
        const int p = resolved.pelvis;
        float delta[3] = {upRaw[0] * state.pelvisOffset,
                          upRaw[1] * state.pelvisOffset,
                          upRaw[2] * state.pelvisOffset};
        std::vector<char> under(skel.nodes.size(), 0);
        under[p] = 1;
        for (int i : order) {
            const int par = skel.nodes[i].parent;
            if (par >= 0 && under[par]) under[i] = 1;
            if (!under[i]) continue;
            pose[(size_t)i * 16 + 12] += delta[0];
            pose[(size_t)i * 16 + 13] += delta[1];
            pose[(size_t)i * 16 + 14] += delta[2];
        }
    }

    for (size_t i = 0; i < legs; ++i) {
        const Resolved::Leg& leg = resolved.legs[i];
        if (leg.hip < 0 || leg.knee < 0 || leg.ankle < 0) continue;
        float* hipM = &pose[(size_t)leg.hip * 16];
        float* kneeM = &pose[(size_t)leg.knee * 16];
        float* ankleM = &pose[(size_t)leg.ankle * 16];
        float A[3] = {hipM[12], hipM[13], hipM[14]};
        float B[3] = {kneeM[12], kneeM[13], kneeM[14]};
        float C[3] = {ankleM[12], ankleM[13], ankleM[14]};

        float T[3];
        xformPoint(worldInv, targetW[i], T);

        float ab[3], bc[3];
        v3sub(B, A, ab);
        v3sub(C, B, bc);
        const float L1 = v3len(ab), L2 = v3len(bc);
        if (L1 < 1e-5f || L2 < 1e-5f) continue;

        float ac[3], plane[3];
        v3sub(C, A, ac);
        v3cross(ab, ac, plane);
        if (v3norm(plane) < 1e-6f) {
            float hipX[3] = {hipM[0], hipM[1], hipM[2]};
            v3cross(ac, hipX, plane);
            if (v3norm(plane) < 1e-6f) continue;
        }

        float at[3];
        v3sub(T, A, at);
        float d = v3len(at);
        if (d < 1e-5f) continue;
        float dir[3] = {at[0] / d, at[1] / d, at[2] / d};
        d = clampf(d, std::fabs(L1 - L2) + 1e-3f, L1 + L2 - 1e-3f);
        float Tc[3];
        for (int k = 0; k < 3; ++k) Tc[k] = A[k] + dir[k] * d;

        const float cosA =
            clampf((L1 * L1 + d * d - L2 * L2) / (2.0f * L1 * d), -1.0f, 1.0f);
        const float angA = std::acos(cosA);
        float c1[3], c2[3];
        rotateAbout(dir, plane, angA, c1);
        rotateAbout(dir, plane, -angA, c2);
        float abUnit[3] = {ab[0] / L1, ab[1] / L1, ab[2] / L1};
        const float* pick = v3dot(c1, abUnit) >= v3dot(c2, abUnit) ? c1 : c2;
        float Bn[3];
        for (int k = 0; k < 3; ++k) Bn[k] = A[k] + pick[k] * L1;

        float newAb[3];
        v3sub(Bn, A, newAb);
        v3norm(newAb);
        float R1[9];
        rotFromTo(abUnit, newAb, R1);
        rotateJoint(hipM, R1, A);
        rotateJoint(kneeM, R1, A);
        rotateJoint(ankleM, R1, A);

        float C1[3] = {ankleM[12], ankleM[13], ankleM[14]};
        float bc1[3], bt[3];
        v3sub(C1, Bn, bc1);
        v3sub(Tc, Bn, bt);
        if (v3norm(bc1) > 1e-6f && v3norm(bt) > 1e-6f) {
            float R2[9];
            rotFromTo(bc1, bt, R2);
            rotateJoint(kneeM, R2, Bn);
            rotateJoint(ankleM, R2, Bn);
        }

        if (rig.normalBlend > 0.0f && state.grounded[i]) {
            float nModel[3];
            xformDir(worldInv, state.normal[i], nModel);
            if (v3norm(nModel) > 1e-6f) {
                float ang = std::acos(clampf(v3dot(upUnit, nModel), -1.0f, 1.0f)) *
                            rig.normalBlend;
                ang = std::min(ang, rig.maxRollDeg * (kPi / 180.0f));
                if (ang > 1e-4f) {
                    float axis[3];
                    v3cross(upUnit, nModel, axis);
                    if (v3norm(axis) > 1e-6f) {
                        float partial[3], R3[9];
                        rotateAbout(upUnit, axis, ang, partial);
                        rotFromTo(upUnit, partial, R3);
                        float pivot[3] = {ankleM[12], ankleM[13], ankleM[14]};
                        rotateJoint(ankleM, R3, pivot);
                    }
                }
            }
        }

        // The toe (and anything else under the ankle) rides along - the
        // engine gets this from refreshPose(); here it is one explicit walk,
        // with each local recovered from the untouched pose.
        std::vector<char> under(skel.nodes.size(), 0);
        under[leg.ankle] = 1;
        for (int nIdx : order) {
            const int par = skel.nodes[nIdx].parent;
            if (par < 0 || !under[par]) continue;
            under[nIdx] = 1;
            float invPar[16], local[16];
            if (!invertAffine(&orig[(size_t)par * 16], invPar)) continue;
            mulM4(local, invPar, &orig[(size_t)nIdx * 16]);
            mulM4(&pose[(size_t)nIdx * 16], &pose[(size_t)par * 16], local);
        }
    }
}

}  // namespace footik
