#pragma once

#include <string>
#include <vector>

// Cutscene Director (Tools > Cutscene Director). A Sequence is a keyframe
// timeline that poses scene objects and (optionally) the game camera over
// time - cinematic cutscenes on the PS2. It is authored by scrubbing the
// playhead and snapshotting object poses / the editor camera, previewed live
// in the viewport, and compiled to a PS2 runtime player
// (src/scripts/sequences.gen.cpp - a global Script) driven from the flow graph
// (Play Sequence / Stop Sequence nodes).
//
// Sequences are project-wide (like color grading / ambience presets) and, like
// those, persist through save() but are NOT part of undo/redo (which snapshots
// only scenes). An object track references its target by NAME; at codegen the
// name resolves to (scene index, runtime object index) and the director only
// applies a track while its scene is the active one.

// One keyframe: a full pose snapshot of one object at a point in time. The
// track's channel flags decide which of these fields actually drive the
// object; `easing` controls interpolation of the OUTGOING segment (this key to
// the next one).
struct SeqObjectKey {
    float time = 0.0f;                    // seconds from the sequence start
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};  // degrees
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float color[3] = {0.6f, 0.6f, 0.6f};
    bool visible = true;
    int easing = 1;  // 0 linear, 1 smooth (smoothstep), 2 step (hold to next)
};

inline bool operator==(const SeqObjectKey& a, const SeqObjectKey& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.time == b.time && eq3(a.position, b.position) &&
           eq3(a.rotation, b.rotation) && eq3(a.scale, b.scale) &&
           eq3(a.color, b.color) && a.visible == b.visible && a.easing == b.easing;
}

// A track binds one scene object (by name) to a list of keyframes. The channel
// flags say which pose channels this track writes - so a track can animate
// only position while leaving color/scale under the object's static values.
struct SeqTrack {
    std::string target;               // scene object name
    bool animPos = true;
    bool animRot = false;
    bool animScale = false;
    bool animColor = false;
    bool animVis = false;
    std::vector<SeqObjectKey> keys;   // kept sorted by time
};

inline bool operator==(const SeqTrack& a, const SeqTrack& b) {
    return a.target == b.target && a.animPos == b.animPos && a.animRot == b.animRot &&
           a.animScale == b.animScale && a.animColor == b.animColor &&
           a.animVis == b.animVis && a.keys == b.keys;
}

// A camera keyframe: eye position, look-at target and field of view. When a
// sequence has a camera track and cameraEnabled is set, the runtime overrides
// the game camera (orbit / player) for the duration of playback and hands it
// back when the sequence ends. FOV drives the editor preview only (the PS2
// projection is fixed at build).
struct SeqCameraKey {
    float time = 0.0f;
    float eye[3] = {0.0f, 12.0f, 24.0f};
    float target[3] = {0.0f, 1.0f, 0.0f};
    float fov = 60.0f;  // degrees (editor preview only)
    int easing = 1;
};

inline bool operator==(const SeqCameraKey& a, const SeqCameraKey& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.time == b.time && eq3(a.eye, b.eye) && eq3(a.target, b.target) &&
           a.fov == b.fov && a.easing == b.easing;
}

struct Sequence {
    std::string name = "Cutscene";
    float duration = 5.0f;         // seconds; playback ends (or loops) here
    bool loop = false;             // restart at 0 instead of ending
    bool cameraEnabled = false;    // drive the game camera from cameraKeys
    std::vector<SeqTrack> tracks;
    std::vector<SeqCameraKey> cameraKeys;  // empty = no camera control
};

inline bool operator==(const Sequence& a, const Sequence& b) {
    return a.name == b.name && a.duration == b.duration && a.loop == b.loop &&
           a.cameraEnabled == b.cameraEnabled && a.tracks == b.tracks &&
           a.cameraKeys == b.cameraKeys;
}

// ---------------------------------------------------------------------------
// Shared interpolation helpers - the SAME math runs in the editor viewport
// scrub preview (app.cpp) and is emitted into the PS2 runtime player
// (templates.cpp sequencesScript). Keep the three in sync.

// Maps a 0..1 segment fraction through an easing curve. Step returns the
// start value (0) until the very end so a channel "holds" until the next key.
inline float seqEase(int easing, float u) {
    if (u <= 0.0f) return 0.0f;
    if (u >= 1.0f) return 1.0f;
    switch (easing) {
        case 1: return u * u * (3.0f - 2.0f * u);  // smoothstep (ease in/out)
        case 2: return 0.0f;                        // step / hold
        default: return u;                          // linear
    }
}

// Interpolates a scalar across a keyframe list given the per-key times, values
// and easings. Holds the first value before the first key and the last after
// the last. `n` is the key count.
inline float seqSample(const float* times, const float* values, const int* easings,
                       int n, float t) {
    if (n <= 0) return 0.0f;
    if (t <= times[0]) return values[0];
    if (t >= times[n - 1]) return values[n - 1];
    int i = 0;
    while (i < n - 1 && t >= times[i + 1]) ++i;
    const float span = times[i + 1] - times[i];
    const float u = span > 1e-6f ? (t - times[i]) / span : 0.0f;
    const float e = seqEase(easings[i], u);
    return values[i] + (values[i + 1] - values[i]) * e;
}
