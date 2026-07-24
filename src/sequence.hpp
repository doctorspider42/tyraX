#pragma once

#include <cmath>
#include <string>
#include <vector>

// Cutscene Director (Tools > Cutscene Director). A Sequence is a keyframe
// timeline that poses scene objects and (optionally) the game camera over
// time - cinematic cutscenes on the PS2. It is authored by scrubbing the
// playhead and snapshotting object poses / the editor camera, previewed live
// in the viewport, and compiled to a PS2 runtime player
// (src/gen/sequences.gen.cpp - a global Script) driven from the flow graph
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

// A camera keyframe: a shot. Either a free shot (explicit eye position +
// look-at target + FOV) or a shot bound to a Camera entity in the scene
// (`camera` = the entity's name): then eye/at/FOV come from the entity's
// transform + Camera FOV property - at runtime too, so a camera entity that is
// itself animated by an object track becomes a dolly/crane shot. When a
// sequence has a camera track and cameraEnabled is set, the runtime overrides
// the game camera (orbit / player) for the duration of playback and hands it
// back when the sequence ends. FOV is applied to the real PS2 projection
// (RendererCore3D::setFov) and restored afterwards. `shake` is a handheld
// noise amplitude (world units) interpolated like the other channels. Cut
// hard between two shots with Step easing; anything else blends the shots.
struct SeqCameraKey {
    float time = 0.0f;
    float eye[3] = {0.0f, 12.0f, 24.0f};
    float target[3] = {0.0f, 1.0f, 0.0f};
    float fov = 60.0f;   // degrees (free shots; bound shots use the entity's)
    float shake = 0.0f;  // camera shake amplitude, world units (0 = steady)
    int easing = 1;
    std::string camera;  // Camera entity name; empty = free shot
};

inline bool operator==(const SeqCameraKey& a, const SeqCameraKey& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.time == b.time && eq3(a.eye, b.eye) && eq3(a.target, b.target) &&
           a.fov == b.fov && a.shake == b.shake && a.easing == b.easing &&
           a.camera == b.camera;
}

// Widescreen mask styles (Sequence::bars): solid black masks composited over
// the frame while the sequence plays (over the 3D scene and the HUD, under
// the pause menus). They slide in/out over the sequence's barsSlideIn /
// barsSlideOut times (0 = appear/vanish instantly).
enum : int {
    kSeqBarsNone = 0,
    kSeqBarsCinema = 1,     // 2.39:1 letterbox (film scope)
    kSeqBarsWide = 2,       // 16:9 letterbox (TV widescreen)
    kSeqBarsPillar = 3,     // vertical pillarbox (dream / flashback)
    kSeqBarsFrame = 4,      // all four edges (vintage vignette frame)
};
constexpr int kSeqBarsStyleCount = 5;
constexpr float kSeqBarsSlideDefault = 0.4f;  // default bars slide time, seconds

struct Sequence {
    std::string name = "Cutscene";
    float duration = 5.0f;         // seconds; playback ends (or loops) here
    bool loop = false;             // restart at 0 instead of ending
    bool cameraEnabled = false;    // drive the game camera from cameraKeys
    bool hidePlayer = false;       // hide the third-person avatar while playing
    int bars = kSeqBarsNone;       // widescreen mask style while playing
    bool skippable = false;        // START ends the cutscene early
    float fadeIn = 0.0f;           // seconds: fade from black at the start
    float fadeOut = 0.0f;          // seconds: fade to black before the end
    // Widescreen bars slide-in/out times (seconds). 0 = the bars snap to full
    // coverage at once / stay until the very end; larger = a slower reveal,
    // authored just like fadeIn/fadeOut.
    float barsSlideIn = kSeqBarsSlideDefault;
    float barsSlideOut = kSeqBarsSlideDefault;
    std::vector<SeqTrack> tracks;
    std::vector<SeqCameraKey> cameraKeys;  // empty = no camera control
};

inline bool operator==(const Sequence& a, const Sequence& b) {
    return a.name == b.name && a.duration == b.duration && a.loop == b.loop &&
           a.cameraEnabled == b.cameraEnabled && a.hidePlayer == b.hidePlayer &&
           a.bars == b.bars &&
           a.skippable == b.skippable && a.fadeIn == b.fadeIn &&
           a.fadeOut == b.fadeOut && a.barsSlideIn == b.barsSlideIn &&
           a.barsSlideOut == b.barsSlideOut && a.tracks == b.tracks &&
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

// Screen fraction each widescreen mask covers per edge, assuming the 4:3
// display the PS2 outputs. Cinema/Wide letterbox to 2.39:1 / 16:9 inside the
// 4:3 image; Pillar/Frame are stylistic. Codegen bakes these numbers into the
// generated player, the editor overlays them on the viewport - one source.
inline void seqBarsFractions(int style, float& top, float& bottom, float& left,
                             float& right) {
    top = bottom = left = right = 0.0f;
    switch (style) {
        case kSeqBarsCinema: top = bottom = 0.5f * (1.0f - (4.0f / 3.0f) / 2.39f); break;
        case kSeqBarsWide: top = bottom = 0.5f * (1.0f - (4.0f / 3.0f) / (16.0f / 9.0f)); break;
        case kSeqBarsPillar: left = right = 0.13f; break;
        case kSeqBarsFrame: top = bottom = left = right = 0.08f; break;
        default: break;
    }
}

// Bars slide-in/out envelope (0..1 of the full coverage) at time t of a
// sequence lasting `duration`. slideIn/slideOut are the reveal times in
// seconds (0 = instant). Mirrored in the generated PS2 player.
inline float seqBarsAmount(float t, float duration, float slideIn, float slideOut) {
    float a = 1.0f;
    if (slideIn > 0.0f && t < slideIn) a = t / slideIn;
    if (slideOut > 0.0f) {
        const float left = duration - t;
        if (left < slideOut && left / slideOut < a) a = left / slideOut;
    }
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

// Fade-from/to-black overlay alpha (0..1) at time t. Mirrored in the
// generated PS2 player.
inline float seqFadeAlpha(float t, float duration, float fadeIn, float fadeOut) {
    float a = 0.0f;
    if (fadeIn > 0.0f && t < fadeIn) a = 1.0f - t / fadeIn;
    if (fadeOut > 0.0f) {
        const float o = 1.0f - (duration - t) / fadeOut;
        if (o > a) a = o;
    }
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

// Handheld camera-shake offset at time t for amplitude `amp` (world units).
// Three incommensurate sine bands so the motion never visibly repeats.
// Mirrored in the generated PS2 player - keep the frequencies in sync.
inline void seqShakeOffset(float t, float amp, float out[3]) {
    out[0] = amp * (0.6f * std::sin(t * 23.7f) + 0.4f * std::sin(t * 7.3f + 1.7f));
    out[1] = amp * (0.6f * std::sin(t * 19.1f + 0.9f) + 0.4f * std::sin(t * 9.7f));
    out[2] = amp * 0.3f * std::sin(t * 13.9f + 2.3f);
}

// The +Z "lens" direction of a Camera entity rotated by its Euler rotation
// (degrees, applied Z*Y*X like the editor gizmo / modelMatrix). The look-at
// of a bound shot is eye + this. Mirrored in the generated PS2 player.
inline void seqCameraForward(const float rotDeg[3], float out[3]) {
    const float d2r = 3.14159265f / 180.0f;
    const float sx = std::sin(rotDeg[0] * d2r), cx = std::cos(rotDeg[0] * d2r);
    const float sy = std::sin(rotDeg[1] * d2r), cy = std::cos(rotDeg[1] * d2r);
    const float sz = std::sin(rotDeg[2] * d2r), cz = std::cos(rotDeg[2] * d2r);
    // Rz * Ry * Rx * (0,0,1)
    out[0] = cx * sy * cz + sx * sz;
    out[1] = cx * sy * sz - sx * cz;
    out[2] = cx * cy;
}

// Inverse of seqCameraForward for the no-roll case: the Euler rotation (deg,
// Z=0) whose +Z lens points along `dir`. Used to bake an imported camera take
// into a Camera entity's rotation track, so a bound shot films exactly along
// the recorded path. `dir` need not be normalized.
inline void seqEulerFromForward(const float dir[3], float outRotDeg[3]) {
    const float r2d = 180.0f / 3.14159265f;
    float d[3] = {dir[0], dir[1], dir[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len > 1e-8f) {
        d[0] /= len;
        d[1] /= len;
        d[2] /= len;
    }
    // forward = (cx*sy, -sx, cx*cy)  =>  pitch from -sx, yaw from (sy, cy)
    float sx = -d[1];
    sx = sx < -1.0f ? -1.0f : (sx > 1.0f ? 1.0f : sx);
    const float rx = std::asin(sx);
    const float cx = std::cos(rx);
    const float ry = std::fabs(cx) > 1e-6f ? std::atan2(d[0], d[2]) : 0.0f;
    outRotDeg[0] = rx * r2d;
    outRotDeg[1] = ry * r2d;
    outRotDeg[2] = 0.0f;
}
