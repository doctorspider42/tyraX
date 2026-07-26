#include "mocap.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

namespace mocap {

namespace {

// ARKit's body skeleton -> the Mixamo names the generated rig uses. Renaming
// here rather than teaching the retargeter about a second naming scheme is the
// point: charanim::retarget matches by name, so a take that speaks the same
// names as a Mixamo download goes down the identical path.
//
// ARKit's spine is seven joints and its neck four; the rig has three and one.
// The picks below are spread along each chain rather than taken from one end -
// spine_1/4/7 bends like a back, spine_1/2/3 bends like a neck brace. Every
// joint NOT listed (fingers, toes beyond the ball, face, twist joints) is
// still loaded and still parents its children correctly; it simply never
// matches a bone, which is where a 91-joint take loses two thirds of itself.
struct Rename {
    const char* arkit;
    const char* mixamo;
};
const Rename kRenames[] = {
    {"hips_joint", "mixamorig:Hips"},
    {"spine_1_joint", "mixamorig:Spine"},
    {"spine_4_joint", "mixamorig:Spine1"},
    {"spine_7_joint", "mixamorig:Spine2"},
    {"neck_1_joint", "mixamorig:Neck"},
    {"head_joint", "mixamorig:Head"},
    {"left_shoulder_1_joint", "mixamorig:LeftShoulder"},
    {"left_arm_joint", "mixamorig:LeftArm"},
    {"left_forearm_joint", "mixamorig:LeftForeArm"},
    {"left_hand_joint", "mixamorig:LeftHand"},
    {"right_shoulder_1_joint", "mixamorig:RightShoulder"},
    {"right_arm_joint", "mixamorig:RightArm"},
    {"right_forearm_joint", "mixamorig:RightForeArm"},
    {"right_hand_joint", "mixamorig:RightHand"},
    {"left_upLeg_joint", "mixamorig:LeftUpLeg"},
    {"left_leg_joint", "mixamorig:LeftLeg"},
    {"left_foot_joint", "mixamorig:LeftFoot"},
    {"left_toes_joint", "mixamorig:LeftToeBase"},
    {"right_upLeg_joint", "mixamorig:RightUpLeg"},
    {"right_leg_joint", "mixamorig:RightLeg"},
    {"right_foot_joint", "mixamorig:RightFoot"},
    {"right_toes_joint", "mixamorig:RightToeBase"},
};



struct Reader {
    const unsigned char* p = nullptr;
    const unsigned char* end = nullptr;
    bool ok = true;

    bool has(size_t n) const { return ok && (size_t)(end - p) >= n; }
    uint16_t u16() {
        if (!has(2)) return (ok = false), 0;
        uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        p += 2;
        return v;
    }
    int16_t i16() { return (int16_t)u16(); }
    uint32_t u32() {
        if (!has(4)) return (ok = false), 0;
        uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24);
        p += 4;
        return v;
    }
    float f32() {
        const uint32_t bits = u32();
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    // Column-major 4x4, the order simd, glTF and this file all use.
    void mat4(float* m) {
        for (int i = 0; i < 16; ++i) m[i] = f32();
    }
    std::string str(size_t n) {
        if (!has(n)) return (ok = false), std::string();
        std::string s((const char*)p, n);
        p += n;
        return s;
    }
};

// Translation + rotation out of a local transform. Scale is dropped on
// purpose: ARKit's skeleton scale estimation puts the performer's limb lengths
// in here, and a retarget applies ROTATIONS to a body that has its own
// proportions - carrying the scale across would stretch the character to match
// whoever stood in front of the camera.
void decompose(const float* m, float* t, float* r) {
    t[0] = m[12];
    t[1] = m[13];
    t[2] = m[14];

    float c[3][3];
    for (int col = 0; col < 3; ++col) {
        float len = 0.0f;
        for (int row = 0; row < 3; ++row) len += m[col * 4 + row] * m[col * 4 + row];
        len = std::sqrt(len);
        if (len < 1e-8f) len = 1.0f;
        for (int row = 0; row < 3; ++row) c[row][col] = m[col * 4 + row] / len;
    }
    const float trace = c[0][0] + c[1][1] + c[2][2];
    float q[4];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q[0] = (c[2][1] - c[1][2]) / s;
        q[1] = (c[0][2] - c[2][0]) / s;
        q[2] = (c[1][0] - c[0][1]) / s;
        q[3] = 0.25f * s;
    } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
        const float s = std::sqrt(1.0f + c[0][0] - c[1][1] - c[2][2]) * 2.0f;
        q[0] = 0.25f * s;
        q[1] = (c[0][1] + c[1][0]) / s;
        q[2] = (c[0][2] + c[2][0]) / s;
        q[3] = (c[2][1] - c[1][2]) / s;
    } else if (c[1][1] > c[2][2]) {
        const float s = std::sqrt(1.0f + c[1][1] - c[0][0] - c[2][2]) * 2.0f;
        q[0] = (c[0][1] + c[1][0]) / s;
        q[1] = 0.25f * s;
        q[2] = (c[1][2] + c[2][1]) / s;
        q[3] = (c[0][2] - c[2][0]) / s;
    } else {
        const float s = std::sqrt(1.0f + c[2][2] - c[0][0] - c[1][1]) * 2.0f;
        q[0] = (c[0][2] + c[2][0]) / s;
        q[1] = (c[1][2] + c[2][1]) / s;
        q[2] = 0.25f * s;
        q[3] = (c[1][0] - c[0][1]) / s;
    }
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; ++i) r[i] = len > 1e-8f ? q[i] / len : (i == 3 ? 1.0f : 0.0f);
}

std::string stem(const std::string& path) {
    size_t a = path.find_last_of("/\\");
    a = a == std::string::npos ? 0 : a + 1;
    const size_t b = path.find_last_of('.');
    return path.substr(a, b == std::string::npos || b < a ? std::string::npos : b - a);
}

}  // namespace

const char* mixamoName(const std::string& arkitJoint) {
    for (const Rename& r : kRenames)
        if (arkitJoint == r.arkit) return r.mixamo;
    return nullptr;
}

// One node per joint, renamed where the rig has a bone for it, with the rest
// pose as its bind. No mesh and no clips - retargeting reads rotations, and the
// caller adds whatever frames it has.
bool buildSource(const std::vector<std::string>& jointNames, const std::vector<int>& parents,
                 const float* restPos, const float* restRot, glbparser::Skel& out,
                 std::string& error) {
    const size_t n = jointNames.size();
    if (!n || parents.size() != n || !restPos || !restRot) {
        error = "skeleton is incomplete (" + std::to_string(n) + " joints, " +
                std::to_string(parents.size()) + " parents)";
        return false;
    }
    out = glbparser::Skel();
    out.nodes.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const char* mapped = mixamoName(jointNames[i]);
        out.nodes[i].name = mapped ? mapped : jointNames[i];
        // A parent must come earlier: composing globals walks the array, and
        // ARKit orders its skeleton that way.
        const int par = parents[i];
        out.nodes[i].parent = (par >= 0 && par < (int)i) ? par : -1;
        std::memcpy(out.nodes[i].t, restPos + i * 3, 12);
        std::memcpy(out.nodes[i].r, restRot + i * 4, 16);
    }

    // The performer's height, from the rest pose - retargeting scales the hips
    // translation by it so a tall performer does not lift a short character off
    // the floor. There is no mesh to measure, so measure the skeleton.
    std::vector<float> gy(n, 0.0f);
    float lo = 1e30f, hi = -1e30f;
    for (size_t i = 0; i < n; ++i) {
        const int par = out.nodes[i].parent;
        gy[i] = out.nodes[i].t[1] + (par >= 0 ? gy[par] : 0.0f);
        lo = std::min(lo, gy[i]);
        hi = std::max(hi, gy[i]);
    }
    out.min[1] = lo;
    out.max[1] = hi;
    return true;
}

bool isTakePath(const std::string& path) {
    return path.size() > 7 && path.compare(path.size() - 7, 7, ".tmocap") == 0;
}

bool load(const std::string& path, glbparser::Skel& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    Reader r{bytes.data(), bytes.data() + bytes.size()};

    if (!r.has(4) || std::memcmp(r.p, "TMCP", 4) != 0) {
        error = "not a .tmocap take (missing TMCP magic)";
        return false;
    }
    r.p += 4;
    const uint32_t version = r.u32();
    if (version != 1) {
        error = "unsupported take version " + std::to_string(version);
        return false;
    }
    r.u32();  // flags
    r.f32();  // capture fps - informational; the times in the frames rule
    const uint32_t jointCount = r.u32();
    const uint32_t frameCount = r.u32();
    const float duration = r.f32();
    if (!r.ok || jointCount == 0 || jointCount > 4096 || frameCount == 0) {
        error = "take header is malformed (" + std::to_string(jointCount) + " joints, " +
                std::to_string(frameCount) + " frames)";
        return false;
    }

    std::vector<std::string> names(jointCount);
    std::vector<int> parent(jointCount, -1);
    for (uint32_t i = 0; i < jointCount; ++i) {
        const uint16_t nameLen = r.u16();
        names[i] = r.str(nameLen);
        parent[i] = r.i16();
        if (!r.ok) {
            error = "take truncated in the joint table (joint " + std::to_string(i) + ")";
            return false;
        }
    }

    std::vector<float> restPos((size_t)jointCount * 3), restRot((size_t)jointCount * 4);
    for (uint32_t i = 0; i < jointCount; ++i) {
        float m[16];
        r.mat4(m);
        if (!r.ok) {
            error = "take truncated in the rest pose (joint " + std::to_string(i) + ")";
            return false;
        }
        decompose(m, &restPos[i * 3], &restRot[i * 4]);
    }
    // Same builder the live link uses - a file and a socket must produce the
    // same source or the two paths quietly diverge.
    if (!buildSource(names, parent, restPos.data(), restRot.data(), out, error)) return false;

    int hips = -1;
    for (uint32_t i = 0; i < jointCount; ++i)
        if (out.nodes[i].name == "mixamorig:Hips") hips = (int)i;

    glbparser::SkelClip clip;
    clip.name = stem(path);
    clip.duration = duration;
    std::vector<glbparser::SkelChannel> rot(jointCount);
    for (uint32_t i = 0; i < jointCount; ++i) {
        rot[i].node = (int)i;
        rot[i].path = 1;
        rot[i].times.reserve(frameCount);
        rot[i].values.reserve((size_t)frameCount * 4);
    }
    glbparser::SkelChannel hipsMove;
    hipsMove.node = hips;
    hipsMove.path = 0;

    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        const float t = r.f32();
        float root[16];
        r.mat4(root);
        if (!r.ok) {
            error = "take truncated at frame " + std::to_string(frame) + " of " +
                    std::to_string(frameCount);
            return false;
        }
        for (uint32_t i = 0; i < jointCount; ++i) {
            float m[16];
            r.mat4(m);
            if (!r.ok) {
                error = "take truncated at frame " + std::to_string(frame);
                return false;
            }
            float tr[3], q[4];
            decompose(m, tr, q);
            rot[i].times.push_back(t);
            rot[i].values.insert(rot[i].values.end(), {q[0], q[1], q[2], q[3]});
            // The hips carry the body's motion through the world - it lives on
            // the anchor, not on the joint, which sits at the skeleton origin.
            if ((int)i == hips) {
                hipsMove.times.push_back(t);
                hipsMove.values.insert(hipsMove.values.end(),
                                       {root[12] + tr[0], root[13] + tr[1], root[14] + tr[2]});
            }
        }
    }

    for (glbparser::SkelChannel& ch : rot) clip.channels.push_back(std::move(ch));
    if (hips >= 0 && !hipsMove.times.empty()) clip.channels.push_back(std::move(hipsMove));
    out.clips.push_back(std::move(clip));

    if (hips < 0)
        out.warnings.push_back(
            "take has no hips_joint - it will not retarget (is it really an ARKit body take?)");
    return true;
}

}  // namespace mocap
