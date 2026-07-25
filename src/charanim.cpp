#include "charanim.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace charanim {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg = kPi / 180.0f;

// --- tiny quaternion helpers ------------------------------------------------
// Quaternions are (x, y, z, w), the glTF order.

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

Quat axisAngle(int axis, float radians) {
    const float h = radians * 0.5f;
    Quat q;
    q.w = std::cos(h);
    (&q.x)[axis] = std::sin(h);
    return q;
}

Quat mul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat conj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

Quat normalized(const Quat& q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-12f) return Quat();
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

// World-axis angles applied X, then Y, then Z.
Quat euler(float xDeg, float yDeg, float zDeg) {
    return mul(mul(axisAngle(2, zDeg * kDeg), axisAngle(1, yDeg * kDeg)),
               axisAngle(0, xDeg * kDeg));
}

using Vec = std::array<float, 3>;

Vec norm(Vec v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l < 1e-9f) return {0, -1, 0};
    return {v[0] / l, v[1] / l, v[2] / l};
}

// The shortest rotation taking `from` onto `to`. This is what lets the rest
// stance be described anatomically ("the upper arm hangs down and slightly
// out") instead of as a hardcoded angle: MakeHuman's arms bind diagonally
// down-out-forward, so a fixed "-72 degrees about Z" over-rotates them and
// folds the elbows across the chest. Derive, do not assume.
Quat alignTo(const Vec& fromIn, const Vec& toIn) {
    const Vec a = norm(fromIn), b = norm(toIn);
    const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (d > 0.999999f) return Quat();
    if (d < -0.999999f) {
        // Antiparallel: any perpendicular axis will do; pick a stable one.
        Vec axis = std::fabs(a[0]) < 0.9f ? Vec{1, 0, 0} : Vec{0, 1, 0};
        const Vec c = norm({a[1] * axis[2] - a[2] * axis[1], a[2] * axis[0] - a[0] * axis[2],
                            a[0] * axis[1] - a[1] * axis[0]});
        return {c[0], c[1], c[2], 0.0f};
    }
    const Vec c = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                   a[0] * b[1] - a[1] * b[0]};
    return normalized({c[0], c[1], c[2], 1.0f + d});
}

// --- the rig ---------------------------------------------------------------

enum Role {
    Hips, Spine, Spine1, Spine2, Neck, Head,
    ShoulderL, ArmL, ForeArmL, HandL,
    ShoulderR, ArmR, ForeArmR, HandR,
    UpLegL, LegL, FootL, ToeL,
    UpLegR, LegR, FootR, ToeR,
    RoleCount
};

const char* const kRoleNames[RoleCount] = {
    "Hips", "Spine", "Spine1", "Spine2", "Neck", "Head",
    "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
    "RightShoulder", "RightArm", "RightForeArm", "RightHand",
    "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase",
    "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase",
};

// The Mixamo hierarchy, in role terms. Frames carry WORLD rotations and this
// is what converts them back to the local ones glTF stores - which is the
// whole reason the cycles are readable: "the shin follows the thigh plus a
// knee bend" is a statement about world orientation, not about a chain of
// parent-relative frames.
const int kParentRole[RoleCount] = {
    -1,    Hips,  Spine, Spine1, Spine2, Neck,
    Spine2, ShoulderL, ArmL, ForeArmL,
    Spine2, ShoulderR, ArmR, ForeArmR,
    Hips,  UpLegL, LegL, FootL,
    Hips,  UpLegR, LegR, FootR,
};

struct Rig {
    int node[RoleCount];
    Vec pos[RoleCount];  // bind-pose world positions
    bool has[RoleCount];
    float height = 1.75f;

    // Bind direction of the bone at `role`, i.e. toward its child. Bind
    // rotations are identity, so this is a pure position difference.
    Vec dir(int role, int childRole) const {
        if (!has[role] || !has[childRole]) return {0, -1, 0};
        return norm({pos[childRole][0] - pos[role][0], pos[childRole][1] - pos[role][1],
                     pos[childRole][2] - pos[role][2]});
    }
};

Rig findRig(const glbparser::Skel& skel) {
    Rig r;
    for (int i = 0; i < RoleCount; ++i) {
        r.node[i] = -1;
        r.has[i] = false;
        r.pos[i] = {0, 0, 0};
    }
    for (size_t i = 0; i < skel.nodes.size(); ++i) {
        std::string name = skel.nodes[i].name;
        const size_t colon = name.find(':');  // accept "mixamorig:Hips" and "Hips"
        if (colon != std::string::npos) name = name.substr(colon + 1);
        for (int role = 0; role < RoleCount; ++role)
            if (r.node[role] < 0 && name == kRoleNames[role]) r.node[role] = (int)i;
    }

    // Bind-pose globals (translations only - every bind rotation is identity).
    std::vector<Vec> global(skel.nodes.size(), Vec{0, 0, 0});
    for (size_t i = 0; i < skel.nodes.size(); ++i) {
        const int parent = skel.nodes[i].parent;
        for (int k = 0; k < 3; ++k)
            global[i][k] = skel.nodes[i].t[k] +
                           (parent >= 0 && parent < (int)i ? global[parent][k] : 0.0f);
    }
    for (int role = 0; role < RoleCount; ++role)
        if (r.node[role] >= 0) {
            r.pos[role] = global[r.node[role]];
            r.has[role] = true;
        }

    const float h = skel.max[1] - skel.min[1];
    if (h > 0.1f) r.height = h;
    return r;
}

// --- frames -----------------------------------------------------------------

struct Frame {
    Quat world[RoleCount];
    bool set[RoleCount] = {};
    float hipsOffset[3] = {0, 0, 0};

    void put(int role, const Quat& q) {
        world[role] = q;
        set[role] = true;
    }
    Quat get(int role) const { return set[role] ? world[role] : Quat(); }
};

// The relaxed stance every clip starts from. The arms bind straight out to the
// sides (MakeHuman's reference pose), so without this the character walks like
// a scarecrow - and the down rotation has to be DERIVED from the bind
// direction, because that direction is diagonal and body-shape dependent.
void restStance(Frame& f, const Rig& rig, const Params& p) {
    const float slouch = -p.posture * 6.0f;

    for (int side = 0; side < 2; ++side) {
        const bool left = side == 0;
        const float sx = left ? 1.0f : -1.0f;  // +X is the character's own left
        const int arm = left ? ArmL : ArmR;
        const int fore = left ? ForeArmL : ForeArmR;
        const int hand = left ? HandL : HandR;

        // Down, a little away from the body, a little forward - a person
        // standing, not a doll dropped from a shelf.
        const Vec armTarget = {sx * 0.20f, -0.97f, 0.10f};
        const Vec foreTarget = {sx * 0.10f, -0.97f, 0.22f};
        f.put(arm, alignTo(rig.dir(arm, fore), armTarget));
        f.put(fore, alignTo(rig.dir(fore, hand), foreTarget));
        f.put(hand, f.get(fore));
        f.put(left ? ShoulderL : ShoulderR, euler(0.0f, 0.0f, -sx * slouch * 0.5f));
    }
    f.put(Spine1, euler(slouch * 0.4f, 0.0f, 0.0f));
    f.put(Neck, euler(-slouch * 0.6f, 0.0f, 0.0f));
}

// Rotations that swing a limb are applied in WORLD space on top of the rest
// stance: a positive `swing` carries the limb forward (+Z), which about the
// world X axis is a negative angle.
Quat swung(const Quat& rest, float degrees) {
    return mul(axisAngle(0, -degrees * kDeg), rest);
}

void poseLeg(Frame& f, const Rig& rig, bool left, float swing, float knee, float ankle) {
    const int up = left ? UpLegL : UpLegR;
    const int low = left ? LegL : LegR;
    const int foot = left ? FootL : FootR;
    const int toe = left ? ToeL : ToeR;
    // Legs bind close enough to straight down to keep their bind direction -
    // aligning them to vertical would make every character knock-kneed, since
    // the hip joint sits outboard of the knee.
    f.put(up, swung(Quat(), swing));
    f.put(low, swung(Quat(), swing - knee));  // the knee only folds backward
    f.put(foot, swung(Quat(), swing - knee + ankle));
    if (rig.has[toe]) f.put(toe, f.get(foot));
}

void poseArm(Frame& f, bool left, float swing, float elbow) {
    const int arm = left ? ArmL : ArmR;
    const int fore = left ? ForeArmL : ForeArmR;
    const int hand = left ? HandL : HandR;
    f.put(arm, swung(f.get(arm), swing));
    f.put(fore, swung(f.get(fore), swing + elbow));
    f.put(hand, f.get(fore));
}

// --- clip building ----------------------------------------------------------

void pushRotationChannel(glbparser::SkelClip& clip, int node, const std::vector<float>& times,
                         const std::vector<Quat>& values) {
    bool moves = false;
    for (const Quat& q : values)
        if (std::fabs(q.x) > 1e-5f || std::fabs(q.y) > 1e-5f || std::fabs(q.z) > 1e-5f) {
            moves = true;
            break;
        }
    if (!moves) return;  // an all-identity channel is pure cost on the EE

    glbparser::SkelChannel ch;
    ch.node = node;
    ch.path = 1;
    ch.times = times;
    ch.values.reserve(values.size() * 4);
    Quat prev = values.empty() ? Quat() : values[0];
    for (const Quat& q : values) {
        // Keep the quaternion path continuous: LINEAR interpolation between q
        // and -q takes the long way round, which reads as a limb snapping
        // through the body mid-step.
        const float dot = q.x * prev.x + q.y * prev.y + q.z * prev.z + q.w * prev.w;
        const float s = dot < 0.0f ? -1.0f : 1.0f;
        prev = {q.x * s, q.y * s, q.z * s, q.w * s};
        ch.values.insert(ch.values.end(), {prev.x, prev.y, prev.z, prev.w});
    }
    clip.channels.push_back(std::move(ch));
}

glbparser::SkelClip buildClip(const std::string& name, const Rig& rig, const float* bindHips,
                              const std::vector<float>& times, const std::vector<Frame>& frames) {
    glbparser::SkelClip clip;
    clip.name = name;
    clip.duration = times.empty() ? 0.0f : times.back();

    for (int role = 0; role < RoleCount; ++role) {
        if (rig.node[role] < 0) continue;
        const int parent = kParentRole[role];
        std::vector<Quat> local;
        local.reserve(frames.size());
        for (const Frame& f : frames) {
            // local = inverse(parent world) * world. A bone nobody posed keeps
            // identity here, i.e. it simply rides its parent.
            const Quat pw = parent >= 0 ? f.get(parent) : Quat();
            local.push_back(normalized(mul(conj(pw), f.get(role))));
        }
        pushRotationChannel(clip, rig.node[role], times, local);
    }

    if (rig.node[Hips] >= 0) {
        bool moves = false;
        for (const Frame& f : frames)
            if (std::fabs(f.hipsOffset[0]) > 1e-5f || std::fabs(f.hipsOffset[1]) > 1e-5f ||
                std::fabs(f.hipsOffset[2]) > 1e-5f) {
                moves = true;
                break;
            }
        if (moves) {
            glbparser::SkelChannel ch;
            ch.node = rig.node[Hips];
            ch.path = 0;
            ch.times = times;
            // Added to the bind translation, never replacing it, or the
            // character drops to the floor the moment a clip plays.
            for (const Frame& f : frames)
                for (int k = 0; k < 3; ++k) ch.values.push_back(bindHips[k] + f.hipsOffset[k]);
            clip.channels.push_back(std::move(ch));
        }
    }
    return clip;
}

// Samples `seconds` at the parameter's rate, always closing with a key at
// exactly `seconds` - a cycle that ends one sample early stutters at the loop.
std::vector<float> sampleTimes(float seconds, int fps) {
    const int steps = std::max(2, (int)std::lround(seconds * (float)std::max(4, fps)));
    std::vector<float> times;
    times.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) times.push_back(seconds * (float)i / (float)steps);
    return times;
}

Frame walkFrame(float phase, const Rig& rig, const Params& p, float amp, float lean, float bob,
                float elbow) {
    Frame f;
    restStance(f, rig, p);

    const float legSwing = 26.0f * amp * p.stride;
    const float armSw = 20.0f * amp * p.armSwing;
    const float s = std::sin(phase);
    const float sOpp = std::sin(phase + kPi);

    // The knee folds through the back half of the swing and as the leg passes
    // under the body; a knee that also straightens forward looks like a puppet.
    auto kneeFor = [&](float ph) {
        return std::max(0.0f, -std::sin(ph - 0.9f)) * (32.0f + 30.0f * amp);
    };
    // The ankle rolls the foot off at toe-off and levels it at contact.
    auto ankleFor = [&](float ph) { return 12.0f * amp * std::sin(ph + 1.2f); };

    poseLeg(f, rig, true, legSwing * s, kneeFor(phase), ankleFor(phase));
    poseLeg(f, rig, false, legSwing * sOpp, kneeFor(phase + kPi), ankleFor(phase + kPi));
    // Arms counter the legs: the left arm goes with the RIGHT leg. The elbow
    // is held at a fixed angle to the upper arm through the whole cycle - a
    // runner's arms stay folded at both extremes, they do not straighten out
    // behind, which is what a swing-dependent bend would do.
    poseArm(f, true, armSw * sOpp, elbow);
    poseArm(f, false, armSw * s, elbow);

    // Torso: a lean that grows with speed, a counter-rotation up the spine and
    // a small side sway; the head stays level while the chest leans.
    f.put(Hips, euler(0.0f, 7.0f * amp * s, -3.0f * amp * s));
    f.put(Spine, euler(lean * 0.5f, 2.0f * amp * s, 2.0f * amp * s));
    f.put(Spine1, mul(euler(lean * 0.3f, -3.0f * amp * s, 0.0f), f.get(Spine1)));
    f.put(Spine2, euler(lean * 0.2f, -6.0f * amp * s, 0.0f));
    f.put(Neck, mul(euler(-lean * 0.9f, 0.0f, 0.0f), f.get(Neck)));

    // Two rises per stride, lowest at each contact.
    f.hipsOffset[1] = -bob * std::cos(2.0f * phase);
    f.hipsOffset[0] = 0.35f * bob * s;
    return f;
}

}  // namespace

void addLocomotion(glbparser::Skel& skel, const Params& p) {
    const Rig rig = findRig(skel);
    if (rig.node[Hips] < 0) return;  // not a rig this module understands

    float bindHips[3] = {0, 0, 0};
    std::memcpy(bindHips, skel.nodes[rig.node[Hips]].t, sizeof(bindHips));
    const float unit = rig.height;  // translations below are shares of it

    std::vector<glbparser::SkelClip> clips;

    // --- idle ---------------------------------------------------------------
    // Breathing on a longer period than the weight shift, so the two never line
    // up into an obvious loop.
    {
        const float seconds = 4.0f;
        const std::vector<float> times = sampleTimes(seconds, std::max(6, p.fps / 2));
        std::vector<Frame> frames;
        frames.reserve(times.size());
        for (float t : times) {
            const float ph = 2.0f * kPi * t / seconds;
            const float m = p.idleMotion;
            const float breath = std::sin(ph);
            const float shift = std::sin(ph * 0.5f);
            Frame f;
            restStance(f, rig, p);
            f.put(Spine1, mul(euler(-1.6f * m * breath, 0.0f, 0.0f), f.get(Spine1)));
            f.put(Spine2, euler(-1.0f * m * breath, 0.0f, 0.0f));
            poseArm(f, true, 1.6f * m * breath, 2.0f * m * breath);
            poseArm(f, false, 1.6f * m * breath, 2.0f * m * breath);
            f.put(Hips, euler(0.0f, 1.5f * m * shift, -1.2f * m * shift));
            f.put(Neck, mul(euler(0.6f * m * breath, -2.5f * m * shift, 0.0f), f.get(Neck)));
            f.put(Head, euler(0.0f, -1.5f * m * shift, 0.0f));
            f.hipsOffset[1] = -0.0035f * unit * m * (1.0f - std::cos(ph));
            f.hipsOffset[0] = 0.004f * unit * m * shift;
            frames.push_back(f);
        }
        clips.push_back(buildClip("idle", rig, bindHips, times, frames));
    }

    // --- walk / run ---------------------------------------------------------
    struct Gait {
        const char* name;
        float seconds, amp, lean, bob, elbow;
    };
    const Gait gaits[] = {
        {"walk", std::max(0.2f, p.walkSeconds), 1.0f, 3.0f - p.posture * 2.0f, 0.012f, 22.0f},
        {"run", std::max(0.15f, p.runSeconds), 1.75f, 11.0f - p.posture * 3.0f, 0.026f, 72.0f},
    };
    for (const Gait& g : gaits) {
        const std::vector<float> times = sampleTimes(g.seconds, p.fps);
        std::vector<Frame> frames;
        frames.reserve(times.size());
        for (float t : times)
            frames.push_back(walkFrame(2.0f * kPi * t / g.seconds, rig, p, g.amp, g.lean,
                                       g.bob * unit, g.elbow));
        clips.push_back(buildClip(g.name, rig, bindHips, times, frames));
    }

    // --- jump ---------------------------------------------------------------
    // One shot: crouch, launch, tuck in the air, absorb on landing. Not a loop,
    // so the last key holds the landing instead of snapping back.
    {
        const float seconds = 0.95f;
        const std::vector<float> times = sampleTimes(seconds, p.fps);
        std::vector<Frame> frames;
        frames.reserve(times.size());
        for (float t : times) {
            const float u = t / seconds;
            float crouch = 0.0f, tuck = 0.0f, reach = 0.0f;
            if (u < 0.2f) {
                crouch = u / 0.2f;
            } else if (u < 0.35f) {
                const float k = (u - 0.2f) / 0.15f;
                crouch = 1.0f - k;
                reach = k;
            } else if (u < 0.7f) {
                const float k = (u - 0.35f) / 0.35f;
                reach = 1.0f - k * 0.6f;
                tuck = std::sin(k * kPi);
            } else {
                crouch = std::sin((u - 0.7f) / 0.3f * kPi) * 0.8f;
            }
            Frame f;
            restStance(f, rig, p);
            const float knee = 55.0f * crouch + 50.0f * tuck;
            const float hip = 30.0f * crouch + 45.0f * tuck;
            poseLeg(f, rig, true, hip, knee, -14.0f * crouch);
            poseLeg(f, rig, false, hip, knee, -14.0f * crouch);
            // The arms swing back into the crouch and then all the way UP over
            // the launch - 100 degrees would leave them pointing straight
            // ahead, which is a zombie, not a jump.
            const float armSwing = -55.0f * crouch + 155.0f * reach;
            poseArm(f, true, armSwing, 26.0f * crouch + 14.0f * reach);
            poseArm(f, false, armSwing, 26.0f * crouch + 14.0f * reach);
            f.put(Spine, euler(16.0f * crouch - 6.0f * reach, 0.0f, 0.0f));
            f.put(Neck, mul(euler(-10.0f * crouch, 0.0f, 0.0f), f.get(Neck)));
            f.hipsOffset[1] = -0.14f * unit * crouch - 0.02f * unit * tuck;
            frames.push_back(f);
        }
        clips.push_back(buildClip("jump", rig, bindHips, times, frames));
    }

    skel.clips = std::move(clips);
}

// ---------------------------------------------------------------------------
// Host-side posing (the preview's twin of the console's VU0 skinning)

namespace {

struct M16 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

M16 mulM(const M16& a, const M16& b) {  // column-major, a * b
    M16 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

M16 trs(const float* t, const float* r, const float* s) {
    const float x = r[0], y = r[1], z = r[2], w = r[3];
    M16 m;
    m.m[0] = (1 - 2 * (y * y + z * z)) * s[0];
    m.m[1] = (2 * (x * y + z * w)) * s[0];
    m.m[2] = (2 * (x * z - y * w)) * s[0];
    m.m[4] = (2 * (x * y - z * w)) * s[1];
    m.m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m.m[6] = (2 * (y * z + x * w)) * s[1];
    m.m[8] = (2 * (x * z + y * w)) * s[2];
    m.m[9] = (2 * (y * z - x * w)) * s[2];
    m.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
    m.m[12] = t[0];
    m.m[13] = t[1];
    m.m[14] = t[2];
    return m;
}

// Samples one channel at `time`, holding the ends. LINEAR only - which is all
// addLocomotion emits, and all the .tskl runtime does between keys.
void sampleChannel(const glbparser::SkelChannel& ch, float time, float* out, int comps) {
    const size_t n = ch.times.size();
    if (!n || ch.values.size() < n * (size_t)comps) return;
    size_t hi = 0;
    while (hi < n && ch.times[hi] < time) ++hi;
    if (hi == 0) {
        std::memcpy(out, &ch.values[0], comps * sizeof(float));
        return;
    }
    if (hi >= n) {
        std::memcpy(out, &ch.values[(n - 1) * comps], comps * sizeof(float));
        return;
    }
    const size_t lo = hi - 1;
    const float t0 = ch.times[lo], t1 = ch.times[hi];
    const float f = t1 > t0 ? (time - t0) / (t1 - t0) : 0.0f;
    for (int c = 0; c < comps; ++c)
        out[c] = ch.values[lo * comps + c] * (1.0f - f) + ch.values[hi * comps + c] * f;
    if (comps == 4) {
        const float len =
            std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        if (len > 1e-8f)
            for (int c = 0; c < 4; ++c) out[c] /= len;
    }
}

}  // namespace

void poseMesh(const glbparser::Skel& skel, int clipIndex, float time,
              std::vector<float>& outInterleaved) {
    outInterleaved.clear();
    if (skel.parts.empty()) return;
    const glbparser::SkelPart& part = skel.parts[0];

    std::vector<M16> local(skel.nodes.size());
    for (size_t i = 0; i < skel.nodes.size(); ++i) {
        const glbparser::SkelNode& n = skel.nodes[i];
        if (n.hasMatrix)
            std::memcpy(local[i].m, n.matrix, sizeof(local[i].m));
        else
            local[i] = trs(n.t, n.r, n.s);
    }
    if (clipIndex >= 0 && clipIndex < (int)skel.clips.size()) {
        const glbparser::SkelClip& clip = skel.clips[clipIndex];
        std::vector<float> t((size_t)skel.nodes.size() * 3);
        std::vector<float> r((size_t)skel.nodes.size() * 4);
        std::vector<float> s((size_t)skel.nodes.size() * 3);
        for (size_t i = 0; i < skel.nodes.size(); ++i) {
            std::memcpy(&t[i * 3], skel.nodes[i].t, 12);
            std::memcpy(&r[i * 4], skel.nodes[i].r, 16);
            std::memcpy(&s[i * 3], skel.nodes[i].s, 12);
        }
        for (const glbparser::SkelChannel& ch : clip.channels) {
            if (ch.node < 0 || ch.node >= (int)skel.nodes.size()) continue;
            if (ch.path == 0) sampleChannel(ch, time, &t[ch.node * 3], 3);
            else if (ch.path == 1) sampleChannel(ch, time, &r[ch.node * 4], 4);
            else sampleChannel(ch, time, &s[ch.node * 3], 3);
        }
        for (size_t i = 0; i < skel.nodes.size(); ++i)
            if (!skel.nodes[i].hasMatrix) local[i] = trs(&t[i * 3], &r[i * 4], &s[i * 3]);
    }

    // Globals. chargen emits parents before children, but a hand-built rig
    // might not, so resolve each chain rather than assuming the order.
    std::vector<M16> global(skel.nodes.size());
    std::vector<char> done(skel.nodes.size(), 0);
    for (size_t i = 0; i < skel.nodes.size(); ++i) {
        std::vector<int> chain;
        int cur = (int)i;
        while (cur >= 0 && cur < (int)skel.nodes.size() && !done[cur]) {
            chain.push_back(cur);
            cur = skel.nodes[cur].parent;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const int parent = skel.nodes[*it].parent;
            global[*it] = (parent >= 0 && parent < (int)skel.nodes.size() && done[parent])
                              ? mulM(global[parent], local[*it])
                              : local[*it];
            done[*it] = 1;
        }
    }

    std::vector<M16> palette(skel.palette.size());
    for (size_t j = 0; j < skel.palette.size(); ++j) {
        const int node = skel.palette[j].node;
        M16 ibm;
        std::memcpy(ibm.m, skel.palette[j].ibm, sizeof(ibm.m));
        palette[j] = mulM(node >= 0 && node < (int)global.size() ? global[node] : M16(), ibm);
    }

    outInterleaved.resize((size_t)part.vertexCount * 8);
    const bool skinned = !palette.empty() && (int)part.joints.size() >= part.vertexCount * 4;
    for (int v = 0; v < part.vertexCount; ++v) {
        const float* p = &part.positions[v * 3];
        const float* n = &part.normals[v * 3];
        float op[3] = {0, 0, 0}, on[3] = {0, 0, 0};
        if (skinned) {
            for (int k = 0; k < 4; ++k) {
                const int slot = part.joints[v * 4 + k];
                const float w = part.weights[v * 4 + k] / 255.0f;
                if (w <= 0.0f || slot < 0 || slot >= (int)palette.size()) continue;
                const float* m = palette[slot].m;
                for (int c = 0; c < 3; ++c) {
                    op[c] += w * (m[c] * p[0] + m[4 + c] * p[1] + m[8 + c] * p[2] + m[12 + c]);
                    on[c] += w * (m[c] * n[0] + m[4 + c] * n[1] + m[8 + c] * n[2]);
                }
            }
        } else {
            std::memcpy(op, p, sizeof(op));
            std::memcpy(on, n, sizeof(on));
        }
        const float len = std::sqrt(on[0] * on[0] + on[1] * on[1] + on[2] * on[2]);
        if (len > 1e-8f)
            for (float& c : on) c /= len;
        float* dst = &outInterleaved[(size_t)v * 8];
        std::memcpy(dst, op, sizeof(op));
        std::memcpy(dst + 3, on, sizeof(on));
        dst[6] = part.uvs[v * 2];
        dst[7] = part.uvs[v * 2 + 1];
    }
}

}  // namespace charanim
