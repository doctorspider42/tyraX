#include "mocap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ostream>
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

// a then b, both (x, y, z, w).
void quatMul(const float* a, const float* b, float* out) {
    const float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    const float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

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
    //
    // Composing the FULL transform matters and is not pedantry: ARKit expresses
    // a bone's offset in its parent's ROTATED frame, so the thigh-to-shin
    // offset reads (0.42, 0, 0) - along the bone's own X, not down. Adding up
    // the Y components (which is what this did) measured a 1.71 m performer as
    // 0.13 m, the hips translation came back thirteen times too big, and the
    // character flew off the top of the screen.
    std::vector<std::array<float, 3>> pos(n, {0.0f, 0.0f, 0.0f});
    std::vector<std::array<float, 4>> rot(n, {0.0f, 0.0f, 0.0f, 1.0f});
    float lo = 1e30f, hi = -1e30f;
    for (size_t i = 0; i < n; ++i) {
        const int par = out.nodes[i].parent;
        const float* t = out.nodes[i].t;
        const float* r = out.nodes[i].r;
        if (par < 0) {
            pos[i] = {t[0], t[1], t[2]};
            rot[i] = {r[0], r[1], r[2], r[3]};
        } else {
            const std::array<float, 4>& q = rot[par];
            // Rotate the local offset by the parent's global rotation, then
            // translate - the ordinary composition, done by hand to keep this
            // module free of a matrix type.
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            const float rx = (1 - 2 * (y * y + z * z)) * t[0] + 2 * (x * y - z * w) * t[1] +
                             2 * (x * z + y * w) * t[2];
            const float ry = 2 * (x * y + z * w) * t[0] + (1 - 2 * (x * x + z * z)) * t[1] +
                             2 * (y * z - x * w) * t[2];
            const float rz = 2 * (x * z - y * w) * t[0] + 2 * (y * z + x * w) * t[1] +
                             (1 - 2 * (x * x + y * y)) * t[2];
            pos[i] = {pos[par][0] + rx, pos[par][1] + ry, pos[par][2] + rz};
            rot[i] = {w * r[0] + x * r[3] + y * r[2] - z * r[1],
                      w * r[1] - x * r[2] + y * r[3] + z * r[0],
                      w * r[2] + x * r[1] - y * r[0] + z * r[3],
                      w * r[3] - x * r[0] - y * r[1] - z * r[2]};
        }
        lo = std::min(lo, pos[i][1]);
        hi = std::max(hi, pos[i][1]);
    }
    out.min[1] = lo;
    out.max[1] = hi;
    return true;
}

namespace {

// Little-endian primitives, the mirror of the app's Swift writer.
void putU16(std::ostream& f, uint16_t v) { f.write((const char*)&v, 2); }
void putI16(std::ostream& f, int16_t v) { f.write((const char*)&v, 2); }
void putU32(std::ostream& f, uint32_t v) { f.write((const char*)&v, 4); }
void putF32(std::ostream& f, float v) { f.write((const char*)&v, 4); }

// Column-major 4x4 from a translation and a quaternion.
void putTrs(std::ostream& f, const float* t, const float* r) {
    const float x = r[0], y = r[1], z = r[2], w = r[3];
    const float m[16] = {
        1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),     0,
        2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),     0,
        2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y), 0,
        t[0],                    t[1],                    t[2],                    1};
    f.write((const char*)m, sizeof(m));
}

}  // namespace

bool writeTake(const std::string& path, const std::vector<std::string>& jointNames,
               const std::vector<int>& parents, const float* restPos, const float* restRot,
               const std::vector<float>& times, const std::vector<float>& rot,
               const std::vector<float>& hips, const std::vector<float>& rootRot,
               std::string& error) {
    const size_t n = jointNames.size();
    const size_t frames = times.size();
    if (!n || parents.size() != n || !restPos || !restRot || !frames ||
        rot.size() != frames * n * 4 || hips.size() != frames * 3 ||
        (!rootRot.empty() && rootRot.size() != frames * 4)) {
        error = "take is inconsistent (" + std::to_string(n) + " joints, " +
                std::to_string(frames) + " frames)";
        return false;
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        error = "could not write " + path;
        return false;
    }
    const float duration = times.back() - times.front();
    f.write("TMCP", 4);
    putU32(f, 1);                                        // version
    putU32(f, 0);                                        // flags
    putF32(f, duration > 0.001f ? (float)frames / duration : 30.0f);
    putU32(f, (uint32_t)n);
    putU32(f, (uint32_t)frames);
    putF32(f, duration);
    for (size_t i = 0; i < n; ++i) {
        putU16(f, (uint16_t)jointNames[i].size());
        f.write(jointNames[i].data(), (std::streamsize)jointNames[i].size());
        putI16(f, (int16_t)parents[i]);
    }
    for (size_t i = 0; i < n; ++i) putTrs(f, restPos + i * 3, restRot + i * 4);
    for (size_t fr = 0; fr < frames; ++fr) {
        putF32(f, times[fr] - times.front());            // rebased to zero
        const float ident[4] = {0, 0, 0, 1};
        // The anchor: where the body is AND which way it faces.
        putTrs(f, &hips[fr * 3], rootRot.empty() ? ident : &rootRot[fr * 4]);
        for (size_t i = 0; i < n; ++i)
            putTrs(f, restPos + i * 3, &rot[(fr * n + i) * 4]);
    }
    if (!f) {
        error = "could not write " + path;
        return false;
    }
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

    // The heading, measured against the take's FIRST frame rather than against
    // ARKit's world. That world's zero is wherever the phone happened to point
    // when the session started, so an absolute heading rotates the character by
    // an arbitrary amount - which is exactly what it looked like: a body turned
    // some random angle away from the camera. The hips TRANSLATION was already
    // rebased this way; the rotation was not, and the two have to agree.
    float headingBase[4] = {0, 0, 0, 1};
    bool haveHeadingBase = false;

    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        const float t = r.f32();
        float root[16];
        r.mat4(root);
        float rootTr[3], rootAbs[4];
        decompose(root, rootTr, rootAbs);
        if (!haveHeadingBase) {
            headingBase[0] = -rootAbs[0];
            headingBase[1] = -rootAbs[1];
            headingBase[2] = -rootAbs[2];
            headingBase[3] = rootAbs[3];
            haveHeadingBase = true;
        }
        // A_t * conj(A_0), NOT conj(A_0) * A_t. Quaternions do not commute and
        // the difference is exactly which frame the relative rotation lives in:
        // left-multiplying expresses it in the FIRST FRAME'S anchor basis, and
        // ARKit's anchor basis is not the world's - it is the same convention
        // that makes "hips -> spine" point sideways. The character's bind is in
        // world space, so a rotation handed to it about axes ninety degrees off
        // does not turn the body, it tumbles it.
        float rootQ[4];
        quatMul(rootAbs, headingBase, rootQ);
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
            // ARKit puts the body's HEADING on the anchor, not on the hips: the
            // hips joint's own rotation is constant to the last bit across a
            // take in which the performer turned a full circle. Composing the
            // anchor onto the hips is what makes the character turn at all -
            // without it every frame faces the direction the first one did, and
            // a performer who walks a circle is retargeted as one marching on
            // the spot. Everything below the hips inherits it for free.
            if ((int)i == hips) {
                float world[4];
                quatMul(rootQ, q, world);
                std::memcpy(q, world, sizeof(world));
            }
            rot[i].times.push_back(t);
            rot[i].values.insert(rot[i].values.end(), {q[0], q[1], q[2], q[3]});
            // The hips also carry the body's motion through the world, and that
            // lives on the anchor too - the joint sits at the skeleton origin.
            if ((int)i == hips) {
                hipsMove.times.push_back(t);
                hipsMove.values.insert(hipsMove.values.end(),
                                       {root[12] + tr[0], root[13] + tr[1], root[14] + tr[2]});
            }
        }
    }

    // Which mapped bones did the source never actually move? ARKit's body
    // tracking does not solve every joint it reports: across a nine-second take
    // in which the performer walked and turned, the wrists, ankles and the head
    // relative to the neck were constant TO THE BIT. That is not a bug here and
    // there is nothing to fix in the retarget - but it is the difference between
    // "the character's hands are broken" and "the source has no wrist data", so
    // it is measured out of each take rather than assumed or left to be
    // rediscovered.
    std::vector<std::string> frozen;
    for (const glbparser::SkelChannel& ch : rot) {
        if (ch.path != 1 || ch.node < 0 || ch.node >= (int)out.nodes.size()) continue;
        if (ch.values.size() < 8) continue;
        bool moved = false;
        for (size_t k = 4; k < ch.values.size() && !moved; ++k)
            if (std::fabs(ch.values[k] - ch.values[k % 4]) > 1e-4f) moved = true;
        const std::string& name = out.nodes[ch.node].name;
        if (!moved && name.rfind("mixamorig:", 0) == 0) frozen.push_back(name.substr(10));
    }
    if (!frozen.empty()) {
        std::string list;
        for (const std::string& f : frozen) list += (list.empty() ? "" : ", ") + f;
        out.warnings.push_back("the source never moves " + list +
                               " - ARKit does not solve those joints, so they follow their "
                               "parent bone rigidly");
    }

    for (glbparser::SkelChannel& ch : rot) clip.channels.push_back(std::move(ch));
    if (hips >= 0 && !hipsMove.times.empty()) clip.channels.push_back(std::move(hipsMove));
    out.clips.push_back(std::move(clip));

    if (hips < 0)
        out.warnings.push_back(
            "take has no hips_joint - it will not retarget (is it really an ARKit body take?)");

    // ARKit does not move hips_joint. Not "hardly" - measured across whole
    // takes in which the performer walked and turned a full circle, its own
    // rotation is constant to the last float bit, because the body's heading
    // lives on the ANCHOR. So a take whose hips joint rotates was written by
    // something that folded the heading into it, and since the anchor carries
    // the heading too, the loader is about to apply it twice. That does not
    // lean a character, it tumbles it. Say so rather than let it be rediscovered
    // from a screenshot.
    if (hips >= 0 && hips < (int)rot.size()) {
        const glbparser::SkelChannel& ch = rot[hips];
        float worst = 0.0f;
        for (size_t k = 4; k + 3 < ch.values.size(); k += 4) {
            float d = 0.0f;
            for (int c = 0; c < 4; ++c) d += ch.values[k + c] * ch.values[c];
            d = std::fabs(d);
            if (d > 1.0f) d = 1.0f;
            worst = std::max(worst, 2.0f * std::acos(d) * 57.2957795f);
        }
        if (worst > 5.0f) {
            // Repairable, and exactly so. ARKit holds this joint at its REST
            // value for the whole of any take, so whatever else is in the
            // channel is the heading that a buggy writer folded in - and the
            // anchor slot, which is untouched, still has it. Putting the rest
            // value back therefore removes the duplicate without guessing:
            // there is exactly one thing the channel is allowed to contain.
            //
            // Repairing rather than refusing, because the alternative is asking
            // somebody to perform a take again for a defect in the recorder.
            glbparser::SkelChannel& ch2 = rot[hips];
            for (size_t k = 0; k + 3 < ch2.values.size(); k += 4)
                std::memcpy(&ch2.values[k], out.nodes[hips].r, 4 * sizeof(float));
            char buf[224];
            std::snprintf(buf, sizeof(buf),
                          "hips_joint rotated %.0f degrees in this take, which ARKit never "
                          "does - an older recorder folded the heading into it while also "
                          "storing it on the anchor, so it would have been applied twice. "
                          "Repaired on load; the take itself is still the old one.",
                          worst);
            out.warnings.push_back(buf);
        }
    }
    return true;
}

}  // namespace mocap
