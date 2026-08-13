#include "animedit.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace animedit {

namespace {

// Linear sample of a channel at `t` seconds, honoring STEP interpolation.
// `out` takes stride floats (4 for a rotation quaternion, 3 otherwise).
void sampleChannel(const glbparser::SkelChannel& ch, int stride, float t,
                   float* out) {
    const size_t keys = ch.times.size();
    if (keys == 0) {
        for (int i = 0; i < stride; ++i) out[i] = 0.0f;
        if (stride == 4) out[3] = 1.0f;
        return;
    }
    if (keys == 1 || t <= ch.times.front()) {
        for (int i = 0; i < stride; ++i) out[i] = ch.values[i];
        return;
    }
    if (t >= ch.times.back()) {
        const size_t base = (keys - 1) * (size_t)stride;
        for (int i = 0; i < stride; ++i) out[i] = ch.values[base + i];
        return;
    }
    size_t hi = 1;
    while (hi < keys && ch.times[hi] < t) ++hi;
    const size_t lo = hi - 1;
    const float span = ch.times[hi] - ch.times[lo];
    const float a = span > 1e-9f ? (t - ch.times[lo]) / span : 0.0f;
    const float* p0 = &ch.values[lo * (size_t)stride];
    const float* p1 = &ch.values[hi * (size_t)stride];
    if (ch.step) {  // STEP holds the left key until the next one
        for (int i = 0; i < stride; ++i) out[i] = p0[i];
        return;
    }
    if (stride == 4) {
        // Quaternion nlerp on the shorter arc - the same thing the engine's
        // pose evaluator does between two keys, so a boundary key inserted
        // here lands exactly where playback would have been.
        float dot = 0.0f;
        for (int i = 0; i < 4; ++i) dot += p0[i] * p1[i];
        const float sign = dot < 0.0f ? -1.0f : 1.0f;
        float len = 0.0f;
        for (int i = 0; i < 4; ++i) {
            out[i] = p0[i] + (p1[i] * sign - p0[i]) * a;
            len += out[i] * out[i];
        }
        len = std::sqrt(len);
        if (len > 1e-9f)
            for (int i = 0; i < 4; ++i) out[i] /= len;
        else
            out[0] = out[1] = out[2] = 0.0f, out[3] = 1.0f;
        return;
    }
    for (int i = 0; i < stride; ++i) out[i] = p0[i] + (p1[i] - p0[i]) * a;
}

// Cuts a channel down to [start, end] source seconds and rebases it to 0.
// Boundary keys are inserted (sampled) so the trimmed clip starts and ends on
// the exact pose playback would have shown at those instants.
void trimChannel(glbparser::SkelChannel& ch, float start, float end) {
    const int stride = ch.path == 1 ? 4 : 3;
    if (ch.times.empty()) return;

    std::vector<float> times;
    std::vector<float> values;
    auto push = [&](float t, const float* v) {
        times.push_back(t - start);
        values.insert(values.end(), v, v + stride);
    };

    float tmp[4];
    sampleChannel(ch, stride, start, tmp);
    push(start, tmp);
    for (size_t k = 0; k < ch.times.size(); ++k) {
        const float t = ch.times[k];
        // Strictly inside: the boundaries are already covered by the sampled
        // keys, and a duplicate time would give the engine a zero-length span.
        if (t <= start + 1e-6f || t >= end - 1e-6f) continue;
        push(t, &ch.values[k * (size_t)stride]);
    }
    if (end > start + 1e-6f) {
        sampleChannel(ch, stride, end, tmp);
        push(end, tmp);
    }
    ch.times.swap(times);
    ch.values.swap(values);
}

bool isAncestor(const glbparser::Skel& skel, int ancestor, int node) {
    while (node >= 0 && node < (int)skel.nodes.size()) {
        if (node == ancestor) return true;
        node = skel.nodes[node].parent;
    }
    return false;
}

int nodeDepth(const glbparser::Skel& skel, int node) {
    int depth = 0;
    while (node >= 0 && node < (int)skel.nodes.size()) {
        ++depth;
        node = skel.nodes[node].parent;
    }
    return depth;
}

struct Linear3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // column-major
};

Linear3 mul(const Linear3& a, const Linear3& b) {
    Linear3 r{};
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row) {
            r.m[c * 3 + row] = 0.0f;
            for (int k = 0; k < 3; ++k)
                r.m[c * 3 + row] +=
                    a.m[k * 3 + row] * b.m[c * 3 + k];
        }
    return r;
}

Linear3 localLinear(const glbparser::SkelNode& n) {
    Linear3 r{};
    if (n.hasMatrix) {
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                r.m[c * 3 + row] = n.matrix[c * 4 + row];
        return r;
    }
    const float x = n.r[0], y = n.r[1], z = n.r[2], w = n.r[3];
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x * x2, xy = x * y2, xz = x * z2;
    const float yy = y * y2, yz = y * z2, zz = z * z2;
    const float wx = w * x2, wy = w * y2, wz = w * z2;
    r.m[0] = (1.0f - (yy + zz)) * n.s[0];
    r.m[1] = (xy + wz) * n.s[0];
    r.m[2] = (xz - wy) * n.s[0];
    r.m[3] = (xy - wz) * n.s[1];
    r.m[4] = (1.0f - (xx + zz)) * n.s[1];
    r.m[5] = (yz + wx) * n.s[1];
    r.m[6] = (xz + wy) * n.s[2];
    r.m[7] = (yz - wx) * n.s[2];
    r.m[8] = (1.0f - (xx + yy)) * n.s[2];
    return r;
}

Linear3 parentLinear(const glbparser::Skel& skel, int node) {
    std::vector<int> chain;
    for (int n = node >= 0 && node < (int)skel.nodes.size()
                     ? skel.nodes[node].parent
                     : -1;
         n >= 0 && n < (int)skel.nodes.size(); n = skel.nodes[n].parent)
        chain.push_back(n);
    Linear3 out;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        out = mul(out, localLinear(skel.nodes[*it]));
    return out;
}

void transform(const Linear3& m, const float v[3], float out[3]) {
    for (int row = 0; row < 3; ++row)
        out[row] = m.m[row] * v[0] + m.m[3 + row] * v[1] +
                   m.m[6 + row] * v[2];
}

bool inverse(const Linear3& a, Linear3& out) {
    const float a00 = a.m[0], a01 = a.m[3], a02 = a.m[6];
    const float a10 = a.m[1], a11 = a.m[4], a12 = a.m[7];
    const float a20 = a.m[2], a21 = a.m[5], a22 = a.m[8];
    const float det = a00 * (a11 * a22 - a12 * a21) -
                      a01 * (a10 * a22 - a12 * a20) +
                      a02 * (a10 * a21 - a11 * a20);
    if (std::fabs(det) < 1e-10f) return false;
    const float d = 1.0f / det;
    out.m[0] = (a11 * a22 - a12 * a21) * d;
    out.m[3] = (a02 * a21 - a01 * a22) * d;
    out.m[6] = (a01 * a12 - a02 * a11) * d;
    out.m[1] = (a12 * a20 - a10 * a22) * d;
    out.m[4] = (a00 * a22 - a02 * a20) * d;
    out.m[7] = (a02 * a10 - a00 * a12) * d;
    out.m[2] = (a10 * a21 - a11 * a20) * d;
    out.m[5] = (a01 * a20 - a00 * a21) * d;
    out.m[8] = (a00 * a11 - a01 * a10) * d;
    return true;
}

bool movesHorizontally(const glbparser::Skel& skel,
                       const glbparser::SkelChannel& ch) {
    const Linear3 basis = parentLinear(skel, ch.node);
    const float base[3] = {ch.values[0], ch.values[1], ch.values[2]};
    for (size_t k = 3; k + 2 < ch.values.size(); k += 3) {
        const float local[3] = {ch.values[k] - base[0],
                                ch.values[k + 1] - base[1],
                                ch.values[k + 2] - base[2]};
        float model[3];
        transform(basis, local, model);
        if (std::fabs(model[0]) >= 1e-6f || std::fabs(model[2]) >= 1e-6f)
            return true;
    }
    return false;
}

// The motion root is the shallowest translating node that still owns the whole
// matrix palette (normally Root, or Hips when its parents stay put). This
// avoids pinning an animated foot merely because it travels farther than the
// pelvis. An odd multi-root asset with no common moving ancestor is left alone
// instead of corrupting an arbitrary limb track.
glbparser::SkelChannel* motionRoot(glbparser::Skel& skel,
                                  glbparser::SkelClip& clip) {
    glbparser::SkelChannel* bestCommon = nullptr;
    int commonDepth = INT_MAX;
    for (glbparser::SkelChannel& ch : clip.channels) {
        if (ch.path != 0 || ch.node < 0 ||
            ch.node >= (int)skel.nodes.size() || ch.values.size() < 3)
            continue;
        if (!movesHorizontally(skel, ch)) continue;

        const int depth = nodeDepth(skel, ch.node);
        bool common = !skel.palette.empty();
        for (const glbparser::SkelJoint& joint : skel.palette)
            if (!isAncestor(skel, ch.node, joint.node)) {
                common = false;
                break;
            }
        if (common && depth < commonDepth)
            bestCommon = &ch, commonDepth = depth;
    }
    return bestCommon;
}

void makeInPlace(glbparser::Skel& skel, glbparser::SkelClip& clip) {
    glbparser::SkelChannel* root = motionRoot(skel, clip);
    if (!root || root->values.size() < 3) return;
    const float base[3] = {root->values[0], root->values[1], root->values[2]};
    const Linear3 basis = parentLinear(skel, root->node);
    Linear3 inv;
    if (!inverse(basis, inv)) return;
    for (size_t k = 0; k + 2 < root->values.size(); k += 3) {
        const float local[3] = {root->values[k] - base[0],
                                root->values[k + 1] - base[1],
                                root->values[k + 2] - base[2]};
        float model[3];
        transform(basis, local, model);
        // Keep authored vertical bob in model space; remove only horizontal
        // travel, even when an FBX parent maps it onto local Y (or any other
        // oddly oriented axis).
        model[0] = 0.0f;
        model[2] = 0.0f;
        float kept[3];
        transform(inv, model, kept);
        for (int c = 0; c < 3; ++c) root->values[k + c] = base[c] + kept[c];
    }
}

}  // namespace

float projectTimeScale(const ProjectSettings& st) {
    const float src = st.animSourceFps > 0.01f ? st.animSourceFps : 24.0f;
    const float play = st.animPlayFps > 0.01f ? st.animPlayFps : 24.0f;
    return play / src;
}

std::vector<animmerge::ImportSpec> importsFor(const Project& p,
                                              const std::string& modelRel) {
    std::vector<animmerge::ImportSpec> out;
    for (const AnimImport& a : p.animImports) {
        if (a.model != modelRel || a.source.empty()) continue;
        animmerge::ImportSpec spec;
        spec.path = p.filePath(a.source);
        spec.clips = a.clips;
        spec.prefix = a.prefix;
        spec.options.boneMap = a.boneMap;
        spec.options.translation =
            a.translation == 2   ? animmerge::TranslationMode::CopyAll
            : a.translation == 1 ? animmerge::TranslationMode::AnimatedOnly
                                 : animmerge::TranslationMode::RootBonesOnly;
        spec.options.ignoreScale = a.ignoreScale;
        spec.options.retargetRoot = a.retargetRoot;
        spec.options.stripNamespace = a.stripNamespace;
        spec.options.caseInsensitive = a.caseInsensitive;
        spec.options.skeletonTracksOnly = a.skeletonTracksOnly;
        out.push_back(std::move(spec));
    }
    return out;
}

const AnimClipEdit* findEdit(const Project& p, const std::string& modelRel,
                             const std::string& sourceClip) {
    for (const AnimClipEdit& e : p.animClipEdits)
        if (e.model == modelRel && e.clip == sourceClip) return &e;
    return nullptr;
}

float totalTimeScale(const Project& p, const std::string& modelRel,
                     const std::string& sourceClip) {
    float s = projectTimeScale(p.settings);
    if (const AnimClipEdit* e = findEdit(p, modelRel, sourceClip))
        s *= e->timeScale > 0.001f ? e->timeScale : 1.0f;
    return s > 0.001f ? s : 0.001f;
}

void trimWindow(const AnimClipEdit* e, float duration, float& start,
                float& end) {
    start = 0.0f;
    end = duration;
    if (!e || duration <= 0.0f) return;
    float a = e->trimStart;
    // trimEnd 0 means "to the end" - the natural default for a field the user
    // has not touched, and it keeps following the clip if the asset is
    // re-exported longer.
    float b = e->trimEnd > 0.0f ? e->trimEnd : duration;
    a = std::clamp(a, 0.0f, duration);
    b = std::clamp(b, 0.0f, duration);
    // A degenerate window would bake a frozen clip; fall back to the whole
    // thing instead of silently producing something unplayable.
    if (b - a < 1e-4f) return;
    start = a;
    end = b;
}

std::string effectiveName(const Project& p, const std::string& modelRel,
                          const std::string& sourceClip) {
    const AnimClipEdit* e = findEdit(p, modelRel, sourceClip);
    return (e && !e->rename.empty()) ? e->rename : sourceClip;
}

std::string sourceName(const Project& p, const std::string& modelRel,
                       const std::string& effective) {
    for (const AnimClipEdit& e : p.animClipEdits)
        if (e.model == modelRel && e.rename == effective && !e.rename.empty())
            return e.clip;
    return effective;
}

void applyClipEdits(const Project& p, const std::string& modelRel,
                    glbparser::Skel& skel) {
    const float projScale = projectTimeScale(p.settings);
    for (glbparser::SkelClip& clip : skel.clips) {
        const AnimClipEdit* e = findEdit(p, modelRel, clip.name);
        const float scale =
            projScale * ((e && e->timeScale > 0.001f) ? e->timeScale : 1.0f);

        float start = 0.0f, end = clip.duration;
        trimWindow(e, clip.duration, start, end);
        if (start > 0.0f || end < clip.duration - 1e-6f) {
            for (glbparser::SkelChannel& ch : clip.channels)
                trimChannel(ch, start, end);
            clip.duration = end - start;
        }

        // Pin after trimming: the first pose of the selected window is the
        // place the character should stand, while vertical bob stays authored.
        if (e && e->inPlace) makeInPlace(skel, clip);

        if (std::fabs(scale - 1.0f) > 1e-6f) {
            const float inv = 1.0f / scale;
            for (glbparser::SkelChannel& ch : clip.channels)
                for (float& t : ch.times) t *= inv;
            clip.duration *= inv;
        }

        // Renaming last: the lookups above key on the SOURCE name, and the
        // .tskl's 32-byte name field is what the game resolves against.
        if (e && !e->rename.empty()) clip.name = e->rename;
    }
}

}  // namespace animedit
