#include "animedit.hpp"

#include <algorithm>
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

}  // namespace

float projectTimeScale(const ProjectSettings& st) {
    const float src = st.animSourceFps > 0.01f ? st.animSourceFps : 24.0f;
    const float play = st.animPlayFps > 0.01f ? st.animPlayFps : 24.0f;
    return play / src;
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
