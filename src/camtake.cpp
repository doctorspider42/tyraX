#include "camtake.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

// See camtake.hpp for the pipeline overview. This file is deliberately free
// of editor dependencies (no ImGui/GLFW) so it links into a host-side test
// harness the same way project.cpp/templates.cpp do.

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool readFileText(const std::string& path, std::string& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "Cannot open \"" + path + "\"";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// --- minimal XML subset reader ----------------------------------------------
// Just enough for the .hfcs CameraLayer: elements never nest a same-named
// element inside themselves in the subset we read, attributes are
// double-quoted and contain no escapes. Not a general XML parser on purpose -
// see docs/camera-takes.md for what CamTrackAR emits.

using sv = std::string_view;

// Position of the next `<tag` whose name ends right there (space, '>', '/').
size_t findOpenTag(sv doc, sv tag, size_t from) {
    while (from < doc.size()) {
        const size_t p = doc.find('<', from);
        if (p == sv::npos || p + 1 + tag.size() > doc.size()) return sv::npos;
        if (doc.compare(p + 1, tag.size(), tag) == 0) {
            const char c = p + 1 + tag.size() < doc.size() ? doc[p + 1 + tag.size()] : '\0';
            if (c == ' ' || c == '>' || c == '/' || c == '\t' || c == '\n' || c == '\r')
                return p;
        }
        from = p + 1;
    }
    return sv::npos;
}

struct XmlElem {
    sv open;     // inside the angle brackets, incl. attributes
    sv body;     // between the open tag and the matching close (self-closing: empty)
    size_t end;  // offset just past the element, for iteration
};

bool findElem(sv doc, sv tag, size_t from, XmlElem& out) {
    const size_t p = findOpenTag(doc, tag, from);
    if (p == sv::npos) return false;
    const size_t gt = doc.find('>', p);
    if (gt == sv::npos) return false;
    out.open = doc.substr(p + 1, gt - p - 1);
    if (doc[gt - 1] == '/') {  // self-closing
        out.body = sv();
        out.end = gt + 1;
        return true;
    }
    const std::string close = "</" + std::string(tag) + ">";
    const size_t c = doc.find(close, gt + 1);
    if (c == sv::npos) return false;
    out.body = doc.substr(gt + 1, c - gt - 1);
    out.end = c + close.size();
    return true;
}

bool attrValue(sv open, sv name, sv& out) {
    // attributes are ` Name="value"`; search from 1 so the tag name itself
    // can't match
    std::string pat = " " + std::string(name) + "=\"";
    const size_t p = open.find(pat);
    if (p == sv::npos) return false;
    const size_t v0 = p + pat.size();
    const size_t v1 = open.find('"', v0);
    if (v1 == sv::npos) return false;
    out = open.substr(v0, v1 - v0);
    return true;
}

bool attrFloat(sv open, sv name, float& out) {
    sv v;
    if (!attrValue(open, name, v)) return false;
    out = std::strtof(std::string(v).c_str(), nullptr);
    return true;
}

// <Tag>text</Tag> inside `body`
bool childFloat(sv body, sv tag, float& out) {
    XmlElem e;
    if (!findElem(body, tag, 0, e)) return false;
    out = std::strtof(std::string(e.body).c_str(), nullptr);
    return true;
}

// --- small rotation math ------------------------------------------------------

struct Mat3 {
    float m[3][3];  // m[row][col], column vectors
};

Mat3 matMul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] +
                        a.m[i][2] * b.m[2][j];
    return r;
}

Mat3 rotX(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
}
Mat3 rotY(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
}
Mat3 rotZ(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

// Shepperd's method, m must be a proper rotation.
void matToQuat(const Mat3& r, float q[4]) {
    const auto& m = r.m;
    const float tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        float s = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        q[3] = (m[2][1] - m[1][2]) / s;
        q[0] = 0.25f * s;
        q[1] = (m[0][1] + m[1][0]) / s;
        q[2] = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        q[3] = (m[0][2] - m[2][0]) / s;
        q[0] = (m[0][1] + m[1][0]) / s;
        q[1] = 0.25f * s;
        q[2] = (m[1][2] + m[2][1]) / s;
    } else {
        float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        q[3] = (m[1][0] - m[0][1]) / s;
        q[0] = (m[0][2] + m[2][0]) / s;
        q[1] = (m[1][2] + m[2][1]) / s;
        q[2] = 0.25f * s;
    }
}

// v' = q * v * q^-1 for unit q (x,y,z,w)
void quatRotate(const float q[4], const float v[3], float out[3]) {
    const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    // t = 2 * cross(q.xyz, v)
    const float tx = 2.0f * (qy * v[2] - qz * v[1]);
    const float ty = 2.0f * (qz * v[0] - qx * v[2]);
    const float tz = 2.0f * (qx * v[1] - qy * v[0]);
    // v' = v + qw * t + cross(q.xyz, t)
    out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
    out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
    out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
}

}  // namespace

// --- .hfcs (CamTrackAR / FXhome HitFilm composite shot) -----------------------
// Semantics ground truth: FXhome's official Blender importer
// (blender_hitfilm_importer.py). Facts used here, straight from that script:
//   - positions are HitFilm "pixels": meters = px * 2.8352 / 1000, i.e. exactly
//     the FXhome importer's `blenderScale = (1/1000) * PixelsPerMM` applied to
//     the position (Blender units are meters). (An earlier reading divided
//     instead, which made every imported take ~8x too small - a real metre
//     walked came out a few centimetres in the game.)
//   - orientation Euler degrees were INVERTED on export - negate them back -
//     and apply in ZYX order (Z innermost): R = Rx * Ry * Rz
//   - HitFilm world axes match the canonical space one-for-one (compose the
//     script's axis_conversion with the standard Blender->Y-up mapping: the
//     result is the identity), and the effective camera convention is
//     looking down local -Z too: the script assigns R (axis-converted) as a
//     Blender camera's rotation, and Blender cameras look down -Z. So the
//     orientation matrix IS the canonical rotation - no extra flip. Verified
//     empirically on a real take: with -Z forward the recorded pitch runs
//     37-65 deg below the horizon (a phone picked up off a desk), with +Z it
//     would stare at the ceiling with the AR camera face-down (impossible).
//   - key times: the importer places key i at frame i (the Time attribute is
//     the same index in timeline ticks) -> t = i / FrameRate
//   - per-frame vertical FOV from the zoom channel (lens zoom in pixels):
//     fov = 2 * atan(0.5 * heightPx / zoomPx)
bool loadCamTakeHfcs(const std::string& path, CamTake& out, std::string& error) {
    std::string doc;
    if (!readFileText(path, doc, error)) return false;
    const sv d = doc;

    XmlElem av;
    float fps = 0.0f, heightPx = 0.0f;
    if (findElem(d, "AudioVideoSettings", 0, av)) {
        childFloat(av.body, "FrameRate", fps);
        childFloat(av.body, "Height", heightPx);
    }
    if (fps <= 0.0f) {
        error = "No AudioVideoSettings/FrameRate in \"" + path + "\"";
        return false;
    }

    XmlElem cam;
    if (!findElem(d, "CameraLayer", 0, cam)) {
        error = "No CameraLayer in \"" + path + "\" (not a CamTrackAR export?)";
        return false;
    }

    // channel -> its <Animation> key list
    auto animKeys = [&](sv channel, sv valueTag, std::vector<XmlElem>& keys) {
        keys.clear();
        XmlElem ch, anim;
        if (!findElem(cam.body, channel, 0, ch)) return;
        if (!findElem(ch.body, "Animation", 0, anim)) return;
        size_t at = 0;
        XmlElem k;
        while (findElem(anim.body, "Key", at, k)) {
            at = k.end;
            XmlElem val;
            if (findElem(k.body, valueTag, 0, val)) keys.push_back(val);
        }
    };

    std::vector<XmlElem> posKeys, rotKeys, zoomKeys;
    animKeys("position", "FXPoint3_32f", posKeys);
    animKeys("orientation", "Orientation3D", rotKeys);
    animKeys("zoom", "float", zoomKeys);

    if (posKeys.empty() || posKeys.size() != rotKeys.size()) {
        error = "CameraLayer animation missing or position/orientation key counts "
                "differ in \"" + path + "\"";
        return false;
    }
    const bool haveZoom = zoomKeys.size() == posKeys.size() && heightPx > 0.0f;

    // meters = px * PixelsPerMM / 1000, matching FXhome's Blender importer
    // (their `blenderScale`); Blender units there are real meters.
    constexpr float kMetersPerPixel = 2.8352f / 1000.0f;
    const float d2r = kPi / 180.0f;

    CamTake take;
    take.source = "CamTrackAR (.hfcs)";
    take.fps = fps;
    take.samples.reserve(posKeys.size());
    for (size_t i = 0; i < posKeys.size(); ++i) {
        CamTakeSample s;
        s.t = (double)i / fps;
        float px = 0, py = 0, pz = 0;
        attrFloat(posKeys[i].open, "X", px);
        attrFloat(posKeys[i].open, "Y", py);
        attrFloat(posKeys[i].open, "Z", pz);
        s.pos[0] = px * kMetersPerPixel;
        s.pos[1] = py * kMetersPerPixel;
        s.pos[2] = pz * kMetersPerPixel;
        float ex = 0, ey = 0, ez = 0;
        attrFloat(rotKeys[i].open, "X", ex);
        attrFloat(rotKeys[i].open, "Y", ey);
        attrFloat(rotKeys[i].open, "Z", ez);
        const Mat3 r = matMul(rotX(-ex * d2r), matMul(rotY(-ey * d2r), rotZ(-ez * d2r)));
        matToQuat(r, s.quat);
        if (haveZoom) {
            const float zoom = std::strtof(std::string(zoomKeys[i].body).c_str(), nullptr);
            if (zoom > 1.0f)
                s.fovDeg = 2.0f * std::atan(0.5f * heightPx / zoom) / d2r;
        }
        take.samples.push_back(s);
    }
    out = std::move(take);
    return true;
}

// --- canonical CSV (docs/camera-takes.md) -------------------------------------
// Per line: t,px,py,pz,qx,qy,qz,qw[,fovDeg]. '#' comments and blank lines
// ignored. Seconds / meters / ARKit-convention quaternion, see the header.
bool loadCamTakeCsv(const std::string& path, CamTake& out, std::string& error) {
    std::string doc;
    if (!readFileText(path, doc, error)) return false;

    CamTake take;
    take.source = "CSV";
    std::istringstream lines(doc);
    std::string line;
    int lineNo = 0;
    while (std::getline(lines, line)) {
        ++lineNo;
        size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos || line[b] == '#') continue;
        double f[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        int n = 0;
        const char* p = line.c_str() + b;
        while (n < 9) {
            char* end = nullptr;
            f[n] = std::strtod(p, &end);
            if (end == p) break;
            ++n;
            p = end;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p != ',') break;
            ++p;
        }
        if (n < 8) {
            error = "\"" + path + "\" line " + std::to_string(lineNo) +
                    ": expected t,px,py,pz,qx,qy,qz,qw[,fov]";
            return false;
        }
        CamTakeSample s;
        s.t = f[0];
        for (int i = 0; i < 3; ++i) s.pos[i] = (float)f[1 + i];
        float qlen = 0.0f;
        for (int i = 0; i < 4; ++i) {
            s.quat[i] = (float)f[4 + i];
            qlen += s.quat[i] * s.quat[i];
        }
        if (qlen < 1e-8f) {
            error = "\"" + path + "\" line " + std::to_string(lineNo) +
                    ": zero-length quaternion";
            return false;
        }
        qlen = std::sqrt(qlen);
        for (int i = 0; i < 4; ++i) s.quat[i] /= qlen;
        if (n >= 9) s.fovDeg = (float)f[8];
        take.samples.push_back(s);
    }
    if (take.samples.empty()) {
        error = "No samples in \"" + path + "\"";
        return false;
    }
    std::stable_sort(take.samples.begin(), take.samples.end(),
                     [](const CamTakeSample& a, const CamTakeSample& b) {
                         return a.t < b.t;
                     });
    if (take.samples.size() >= 2 && take.duration() > 0.0)
        take.fps = (float)((take.samples.size() - 1) / take.duration());
    out = std::move(take);
    return true;
}

bool loadCamTakeAuto(const std::string& path, CamTake& out, std::string& error) {
    std::string ext;
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot + 1);
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    }
    if (ext == "hfcs") return loadCamTakeHfcs(path, out, error);
    if (ext == "csv" || ext == "txt") return loadCamTakeCsv(path, out, error);
    error = "Unknown take format \"." + ext + "\" (expected .hfcs or .csv)";
    return false;
}

// --- take -> keys --------------------------------------------------------------

std::vector<SeqCameraKey> bakeCamTake(const CamTake& take, const CamTakeMapping& map,
                                      CamTakeBakeStats* stats) {
    std::vector<SeqCameraKey> keys;
    if (stats) *stats = CamTakeBakeStats{};
    const int n = (int)take.samples.size();
    if (stats) stats->sampleCount = n;
    if (n == 0) return keys;

    // 1) map every sample into the scene: eye + look-at + rebased time
    const float yaw = map.yawDeg * kPi / 180.0f;
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    auto yawRot = [&](const float v[3], float o[3]) {
        o[0] = v[0] * cy + v[2] * sy;
        o[1] = v[1];
        o[2] = -v[0] * sy + v[2] * cy;
    };
    struct Pt {
        float t;
        float eye[3];
        float at[3];
    };
    std::vector<Pt> pts(n);
    const CamTakeSample& first = take.samples[0];
    float fovSum = 0.0f;
    int fovCount = 0;
    for (int i = 0; i < n; ++i) {
        const CamTakeSample& s = take.samples[i];
        Pt& p = pts[i];
        p.t = (float)(s.t - first.t) + map.timeOffset;
        const float rel[3] = {s.pos[0] - first.pos[0], s.pos[1] - first.pos[1],
                              s.pos[2] - first.pos[2]};
        float relY[3];
        yawRot(rel, relY);
        for (int c = 0; c < 3; ++c) p.eye[c] = map.origin[c] + relY[c] * map.scale;
        const float minusZ[3] = {0.0f, 0.0f, -1.0f};
        float fwd[3], fwdY[3];
        quatRotate(s.quat, minusZ, fwd);
        yawRot(fwd, fwdY);
        for (int c = 0; c < 3; ++c)
            p.at[c] = p.eye[c] + fwdY[c] * (kCamTakeLookDist * map.scale);
        if (s.fovDeg > 0.0f) {
            fovSum += s.fovDeg;
            ++fovCount;
        }
    }
    const float fov = fovCount > 0 ? fovSum / (float)fovCount : 60.0f;

    // 2) decimate: time-parameterized RDP over the (eye, at) curve. Error of a
    // dropped sample = how far the PS2's linear eye/target interpolation
    // between the kept neighbours would land from the true sample.
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    if (map.tolerance > 0.0f && n > 2) {
        std::vector<std::pair<int, int>> stack{{0, n - 1}};
        while (!stack.empty()) {
            const auto [i0, i1] = stack.back();
            stack.pop_back();
            if (i1 - i0 < 2) continue;
            const Pt& a = pts[i0];
            const Pt& b = pts[i1];
            const float dt = b.t - a.t;
            float worst = -1.0f;
            int worstI = -1;
            for (int i = i0 + 1; i < i1; ++i) {
                const float u = dt > 1e-6f ? (pts[i].t - a.t) / dt : 0.0f;
                float e2 = 0.0f, a2 = 0.0f;  // eye / target squared deviation
                for (int c = 0; c < 3; ++c) {
                    const float de =
                        pts[i].eye[c] - (a.eye[c] + (b.eye[c] - a.eye[c]) * u);
                    const float da = pts[i].at[c] - (a.at[c] + (b.at[c] - a.at[c]) * u);
                    e2 += de * de;
                    a2 += da * da;
                }
                const float err = std::sqrt(e2 > a2 ? e2 : a2);
                if (err > worst) {
                    worst = err;
                    worstI = i;
                }
            }
            if (worstI >= 0 && worst > map.tolerance) {
                keep[worstI] = 1;
                stack.push_back({i0, worstI});
                stack.push_back({worstI, i1});
            }
        }
    } else if (map.tolerance <= 0.0f) {
        std::fill(keep.begin(), keep.end(), 1);
    }

    // 3) emit free camera keys, linear easing (the take IS the ease)
    for (int i = 0; i < n; ++i) {
        if (!keep[i]) continue;
        SeqCameraKey k;
        k.time = pts[i].t;
        for (int c = 0; c < 3; ++c) {
            k.eye[c] = pts[i].eye[c];
            k.target[c] = pts[i].at[c];
        }
        k.fov = fov;
        k.shake = 0.0f;
        k.easing = 0;
        k.camera.clear();
        keys.push_back(std::move(k));
    }
    if (stats) {
        stats->keyCount = (int)keys.size();
        stats->duration = keys.back().time - keys.front().time;
        stats->fovDeg = fov;
    }
    return keys;
}

float camTakeInitialYawDeg(const CamTake& take) {
    if (take.samples.empty()) return 0.0f;
    const float minusZ[3] = {0.0f, 0.0f, -1.0f};
    float fwd[3];
    quatRotate(take.samples.front().quat, minusZ, fwd);
    return std::atan2(fwd[0], fwd[2]) * 180.0f / 3.14159265f;
}
