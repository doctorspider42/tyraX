#include "motionnet.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "json.hpp"

namespace motionnet {

namespace {

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Deterministic per-index hash, the treegen/procgen rule: a dataset must be
// reproducible from its seed, and a running counter makes changing one option
// reshuffle every sample.
unsigned mix32(unsigned a, unsigned b) {
    unsigned h = a * 0x9E3779B1u ^ (b + 0x85EBCA6Bu);
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}
float rand01(unsigned a, unsigned b) {
    return (float)(mix32(a, b) & 0xFFFFFF) / 16777215.0f;
}

// Orthonormalized rotation part of a column-major 4x4. The pose carries scale
// (a rig may scale a bone), and a rotation delta taken from unnormalized
// columns encodes that scale as a bogus angle.
void rotationOf(const float* m, float* R) {
    float c0[3] = {m[0], m[1], m[2]};
    float c1[3] = {m[4], m[5], m[6]};
    float c2[3] = {m[8], m[9], m[10]};
    auto norm = [](float* v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 1e-8f) v[0] /= l, v[1] /= l, v[2] /= l;
    };
    norm(c0);
    // Gram-Schmidt the rest against it, so a slightly non-orthogonal basis
    // (which any chain of float matrix products becomes) still yields a
    // proper rotation instead of a shear.
    const float d1 = c1[0] * c0[0] + c1[1] * c0[1] + c1[2] * c0[2];
    for (int k = 0; k < 3; ++k) c1[k] -= d1 * c0[k];
    norm(c1);
    c2[0] = c0[1] * c1[2] - c0[2] * c1[1];
    c2[1] = c0[2] * c1[0] - c0[0] * c1[2];
    c2[2] = c0[0] * c1[1] - c0[1] * c1[0];
    R[0] = c0[0], R[1] = c0[1], R[2] = c0[2];
    R[3] = c1[0], R[4] = c1[1], R[5] = c1[2];
    R[6] = c2[0], R[7] = c2[1], R[8] = c2[2];
}

/** delta = solved * raw^T, as an exponential map (axis * angle). */
void rotationDelta(const float* solved, const float* raw, float* out) {
    float A[9], B[9];
    rotationOf(solved, A);
    rotationOf(raw, B);
    // D = A * B^T (column-major: D[c*3+r])
    float D[9];
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) {
            float acc = 0.0f;
            for (int k = 0; k < 3; ++k) acc += A[k * 3 + r] * B[k * 3 + c];
            D[c * 3 + r] = acc;
        }
    const float tr = D[0] + D[4] + D[8];
    float ang = std::acos(clampf((tr - 1.0f) * 0.5f, -1.0f, 1.0f));
    float ax[3] = {D[5] - D[7], D[6] - D[2], D[1] - D[3]};
    const float s = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
    if (s < 1e-7f || ang < 1e-7f) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    for (int k = 0; k < 3; ++k) out[k] = ax[k] / s * ang;
}

/**
 * The training terrain: a 1-D height profile along +Z, built from a seeded
 * sequence of flats, single steps, staircases and ramps. One dimension is
 * enough because the character walks in a straight line - and it keeps the
 * generator honest about what the net can learn, which is "the ground ahead
 * rises/falls", not "world position X means step".
 */
struct ProfileGround : public footik::Ground {
    struct Seg {
        float z0, z1;     // world Z span
        float y0, y1;     // height at each end (equal = flat, else a ramp)
        bool stepped;     // true = a hard edge at z0 rather than a slope
    };
    std::vector<Seg> segs;

    float heightAt(float z) const {
        if (segs.empty()) return 0.0f;
        if (z <= segs.front().z0) return segs.front().y0;
        for (const Seg& s : segs) {
            if (z > s.z1) continue;
            if (s.stepped || s.z1 <= s.z0) return s.y1;
            const float t = (z - s.z0) / (s.z1 - s.z0);
            return s.y0 + (s.y1 - s.y0) * t;
        }
        return segs.back().y1;
    }

    bool sample(const float world[3], float up, float down, float* outY,
                float outNormal[3]) const override {
        const float y = heightAt(world[2]);
        if (y > world[1] + up || y < world[1] - down) return false;
        *outY = y;
        // Central difference, so a ramp tilts the foot exactly the way the
        // generated game's terrain normal would.
        const float h = 0.15f;
        const float dy = heightAt(world[2] + h) - heightAt(world[2] - h);
        float n[3] = {0.0f, 2.0f * h, -dy};
        const float l = std::sqrt(n[1] * n[1] + n[2] * n[2]);
        outNormal[0] = 0.0f;
        outNormal[1] = n[1] / l;
        outNormal[2] = n[2] / l;
        return true;
    }
};

ProfileGround buildProfile(unsigned seed, float length, float scale) {
    ProfileGround g;
    float z = -4.0f, y = 0.0f;
    unsigned i = 0;
    while (z < length) {
        const float kind = rand01(seed, i * 4 + 0);
        const float len = 1.0f + rand01(seed, i * 4 + 1) * 3.0f;
        ProfileGround::Seg s;
        s.z0 = z;
        s.z1 = z + len;
        s.y0 = y;
        if (kind < 0.30f) {          // flat - the case the net must not break
            s.y1 = y;
            s.stepped = false;
        } else if (kind < 0.70f) {   // a staircase: several hard risers
            const float rise = (0.08f + rand01(seed, i * 4 + 2) * 0.16f) * scale *
                               (rand01(seed, i * 4 + 3) < 0.5f ? -1.0f : 1.0f);
            s.y1 = y + rise;
            s.stepped = true;
        } else {                     // a ramp
            const float slope = (rand01(seed, i * 4 + 2) - 0.5f) * 0.5f;
            s.y1 = y + slope * len * scale;
            s.stepped = false;
        }
        y = s.y1;
        z = s.z1;
        g.segs.push_back(s);
        ++i;
    }
    return g;
}

}  // namespace

Params defaultParams(const glbparser::Skel& skel, const AnimRig& rig) {
    Params p;
    const float height = skel.max[1] - skel.min[1];
    const float unit = height > 1e-3f ? height : 1.8f;
    // Everything is expressed as a fraction of the character's own height, so
    // the same defaults fit a 1.8-unit human and a 3.4-unit one. A probe row
    // a third of a body-height ahead is roughly one stride.
    p.probeForward[0] = -0.25f * unit;
    p.probeForward[1] = 0.20f * unit;
    p.probeForward[2] = 0.60f * unit;
    p.probeLateral[0] = -0.17f * unit;
    p.probeLateral[1] = 0.0f;
    p.probeLateral[2] = 0.17f * unit;
    p.probeScale = 0.5f * unit;
    p.refSpeed = 1.7f * unit;
    p.outScale = 0.35f;
    p.phaseRateRange = 0.35f;
    (void)rig;
    return p;
}

std::vector<int> outputJoints(const AnimRig& rig, const footik::Resolved& r) {
    std::vector<int> out;
    for (const footik::Resolved::Leg& leg : r.legs) {
        out.push_back(leg.hip);
        out.push_back(leg.knee);
        out.push_back(leg.ankle);
    }
    for (int j : r.netJoints)
        if (j >= 0 && std::find(out.begin(), out.end(), j) == out.end())
            out.push_back(j);
    (void)rig;
    return out;
}

void buildFeatures(const Params& p, const footik::Ground& ground, float phase,
                   float speed, float turn, float strafe, float yaw,
                   const float rootWorld[3], const float* legOffset,
                   const bool* legPlanted, int legCount, float* f) {
    const float invScale = p.probeScale > 1e-4f ? 1.0f / p.probeScale : 1.0f;
    const float tau = 6.28318531f;
    f[0] = std::sin(phase * tau);
    f[1] = std::cos(phase * tau);
    f[2] = p.refSpeed > 1e-4f ? clampf(speed / p.refSpeed, -2.0f, 2.0f) : 0.0f;
    f[3] = clampf(turn * 0.3183f, -2.0f, 2.0f);
    f[4] = clampf(strafe, -1.0f, 1.0f);

    const float cy = std::cos(yaw), sy = std::sin(yaw);
    int i = 5;
    for (int r = 0; r < kProbeForward; ++r)
        for (int c = 0; c < kProbeLateral; ++c) {
            const float fwd = p.probeForward[r], lat = p.probeLateral[c];
            float q[3] = {rootWorld[0] + sy * fwd + cy * lat, rootWorld[1],
                          rootWorld[2] + cy * fwd - sy * lat};
            float gy = rootWorld[1], gn[3] = {0, 1, 0};
            if (!ground.sample(q, 2.0f, 3.0f, &gy, gn)) gy = rootWorld[1];
            f[i++] = clampf((gy - rootWorld[1]) * invScale, -2.0f, 2.0f);
        }

    for (int l = 0; l < kMaxLegs; ++l) {
        const bool live = l < legCount;
        f[i++] = live ? clampf(legOffset[l] * invScale, -2.0f, 2.0f) : 0.0f;
        f[i++] = live && legPlanted[l] ? 1.0f : 0.0f;
    }
}

std::string generateDataset(const glbparser::Skel& skel, const AnimRig& rig,
                            const footik::Resolved& resolved, const Params& p,
                            DatasetOptions& opt) {
    const std::vector<int> joints = outputJoints(rig, resolved);
    if (joints.empty()) {
        opt.error = "the rig binds no joints";
        return std::string();
    }
    int clip = -1;
    for (size_t i = 0; i < skel.clips.size(); ++i)
        if (opt.clip.empty() || skel.clips[i].name == opt.clip) {
            clip = (int)i;
            break;
        }
    if (clip < 0) {
        opt.error = "no clip named \"" + opt.clip + "\" in this model";
        return std::string();
    }
    const float duration = skel.clips[clip].duration;
    if (duration <= 1e-4f) {
        opt.error = "clip \"" + skel.clips[clip].name + "\" has no motion";
        return std::string();
    }

    const float height = skel.max[1] - skel.min[1];
    const float scale = height > 1e-3f ? height / 1.8f : 1.0f;
    const float dt = 1.0f / (opt.fps > 1.0f ? opt.fps : 30.0f);

    std::ostringstream out;
    out << "# TyraX gait dataset, feature version " << (int)kFeatureVersion
        << ", " << joints.size() << " joints\n";
    for (int i = 0; i < kFeatureCount; ++i) out << (i ? "," : "") << "f" << i;
    for (size_t j = 0; j < joints.size(); ++j)
        out << ",r" << j << "x,r" << j << "y,r" << j << "z";
    out << ",rate\n";
    out.setf(std::ios::fixed);
    out.precision(5);

    footik::State state;
    float phase = 0.0f, z = 0.0f;
    ProfileGround ground = buildProfile(opt.seed, 0.0f, scale);
    int written = 0, run = 0;

    for (int frame = 0; written < opt.frames; ++frame) {
        // A fresh profile every few hundred frames, so the net sees many
        // terrains rather than one long one - and the springs are reset with
        // it, because a teleport to a new profile is not a walk.
        if (frame % 400 == 0) {
            ++run;
            ground = buildProfile(mix32(opt.seed, (unsigned)run),
                                  opt.speed * 400.0f * dt + 8.0f, scale);
            state = footik::State();
            z = 0.0f;
            phase = rand01(opt.seed, (unsigned)run * 7 + 3);
        }

        const float groundY = ground.heightAt(z);
        float rootWorld[3] = {0.0f, groundY, z};
        float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                           rootWorld[0], rootWorld[1], rootWorld[2], 1};

        footik::Pose raw;
        footik::evalPose(skel, clip, phase * duration, raw);
        footik::Pose solved = raw;
        footik::solve(skel, rig, resolved, ground, world, dt, state, solved);

        float legOffset[kMaxLegs] = {0, 0, 0, 0};
        bool legPlanted[kMaxLegs] = {false, false, false, false};
        for (int l = 0; l < kMaxLegs; ++l)
            legOffset[l] = state.offset[l], legPlanted[l] = state.grounded[l];

        float feat[kFeatureCount];
        // The IK state handed to the features is LAST frame's, exactly like
        // the runtime's - solve() has already advanced it, so this row would
        // otherwise carry an input the console cannot have yet.
        buildFeatures(p, ground, phase, opt.speed, 0.0f, 0.0f, 0.0f, rootWorld,
                      legOffset, legPlanted, (int)resolved.legs.size(), feat);

        for (int i = 0; i < kFeatureCount; ++i)
            out << (i ? "," : "") << feat[i];

        const float invOut = p.outScale > 1e-5f ? 1.0f / p.outScale : 1.0f;
        for (int node : joints) {
            float d[3];
            rotationDelta(&solved[(size_t)node * 16], &raw[(size_t)node * 16], d);
            for (int k = 0; k < 3; ++k)
                out << "," << clampf(d[k] * invOut, -3.0f, 3.0f);
        }

        // The stride rule, stated rather than distilled: a rising slope
        // shortens the step, and a shorter stride at the same ground speed
        // means the clip has to play faster. The solver has no opinion about
        // this at all, which is exactly why the net is worth having.
        const float ahead = ground.heightAt(z + p.probeForward[kProbeForward - 1]);
        const float slope = (ahead - groundY) /
                            (p.probeForward[kProbeForward - 1] + 1e-4f);
        const float rate = clampf(slope * 0.8f, -1.0f, 1.0f);
        out << "," << rate << "\n";

        phase += dt / duration;
        phase -= std::floor(phase);
        z += opt.speed * dt;
        ++written;
    }
    return out.str();
}

bool readWeightsJson(const std::string& text, Net& net, std::string& error) {
    json::Value root;
    if (!json::parse(text, root)) {
        error = "not valid JSON";
        return false;
    }
    const auto* fv = root.find("featureVersion");
    if (!fv || (int)fv->numberOr(0) != kFeatureVersion) {
        error = "weights were trained against a different feature layout";
        return false;
    }
    const auto* js = root.find("joints");
    if (!js || js->type != json::Value::Type::Array) {
        error = "no joint list";
        return false;
    }
    net.joints.clear();
    for (const auto& j : js->arr) net.joints.push_back((int)j.numberOr(-1));

    if (const auto* p = root.find("params")) {
        auto num = [&](const char* k, float def) {
            const auto* v = p->find(k);
            return v ? (float)v->numberOr(def) : def;
        };
        net.params.probeScale = num("probeScale", net.params.probeScale);
        net.params.refSpeed = num("refSpeed", net.params.refSpeed);
        net.params.outScale = num("outScale", net.params.outScale);
        net.params.phaseRateRange =
            num("phaseRateRange", net.params.phaseRateRange);
        auto arr = [&](const char* k, float* dst, int n) {
            const auto* v = p->find(k);
            if (!v || v->type != json::Value::Type::Array ||
                (int)v->arr.size() != n)
                return;
            for (int i = 0; i < n; ++i) dst[i] = (float)v->arr[i].number;
        };
        arr("probeForward", net.params.probeForward, kProbeForward);
        arr("probeLateral", net.params.probeLateral, kProbeLateral);
    }

    const auto* ls = root.find("layers");
    if (!ls || ls->type != json::Value::Type::Array || ls->arr.empty()) {
        error = "no layers";
        return false;
    }
    net.layers.clear();
    for (const auto& jl : ls->arr) {
        Layer L;
        const auto* w = jl.find("w");
        const auto* b = jl.find("b");
        if (!w || !b || w->type != json::Value::Type::Array ||
            b->type != json::Value::Type::Array) {
            error = "a layer is missing its weights or biases";
            return false;
        }
        L.outCount = (int)b->arr.size();
        if (L.outCount == 0 || (int)w->arr.size() % L.outCount != 0) {
            error = "a layer's weight count is not a multiple of its outputs";
            return false;
        }
        L.inCount = (int)w->arr.size() / L.outCount;
        const auto* relu = jl.find("relu");
        L.relu = relu ? relu->boolOr(true) : true;
        L.weights.reserve(w->arr.size());
        for (const auto& v : w->arr) L.weights.push_back((float)v.number);
        for (const auto& v : b->arr) L.biases.push_back((float)v.number);
        net.layers.push_back(std::move(L));
    }

    // Validate the shapes rather than trust them: a mismatch reaching the
    // console is a confident, plausible, WRONG pose, which is far harder to
    // diagnose than a refusal here.
    if (net.layers.front().inCount != kFeatureCount) {
        error = "the first layer does not take the feature vector";
        return false;
    }
    for (size_t i = 1; i < net.layers.size(); ++i)
        if (net.layers[i].inCount != net.layers[i - 1].outCount) {
            error = "layer shapes do not chain";
            return false;
        }
    if (net.layers.back().outCount != (int)net.joints.size() * 3 + 1) {
        error = "the last layer does not match the joint list (expected " +
                std::to_string(net.joints.size() * 3 + 1) + " outputs)";
        return false;
    }
    net.layers.back().relu = false;  // the output layer is never rectified
    return true;
}

namespace {
void putU32(std::string& s, unsigned v) {
    s.push_back((char)(v & 0xFF));
    s.push_back((char)((v >> 8) & 0xFF));
    s.push_back((char)((v >> 16) & 0xFF));
    s.push_back((char)((v >> 24) & 0xFF));
}
void putU16(std::string& s, unsigned v) {
    s.push_back((char)(v & 0xFF));
    s.push_back((char)((v >> 8) & 0xFF));
}
void putF32(std::string& s, float f) {
    unsigned u;
    std::memcpy(&u, &f, 4);
    putU32(s, u);
}
}  // namespace

std::string writeTnet(const Net& net) {
    std::string s;
    s += "TXNN";
    putU32(s, 1);  // format version
    putU32(s, (unsigned)kFeatureVersion);
    putF32(s, net.params.outScale);
    putF32(s, net.params.phaseRateRange);
    putF32(s, net.params.probeScale);
    putF32(s, net.params.refSpeed);
    for (int i = 0; i < kProbeForward; ++i) putF32(s, net.params.probeForward[i]);
    for (int i = 0; i < kProbeLateral; ++i) putF32(s, net.params.probeLateral[i]);
    putU32(s, (unsigned)net.joints.size());
    for (int j : net.joints) putU16(s, (unsigned)(j < 0 ? 0 : j));
    putU32(s, (unsigned)net.layers.size());
    for (const Layer& L : net.layers) {
        putU32(s, (unsigned)L.inCount);
        putU32(s, (unsigned)L.outCount);
        s.push_back((char)(L.relu ? 1 : 0));
    }
    // Weights are written at their REAL width; the loader pads each row to a
    // multiple of four on the way in, because that is a runtime concern
    // (lqc2) and not something the file should carry.
    for (const Layer& L : net.layers) {
        for (float w : L.weights) putF32(s, w);
        for (float b : L.biases) putF32(s, b);
    }
    return s;
}

}  // namespace motionnet
