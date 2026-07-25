#pragma once

#include <string>
#include <vector>

#include "sequence.hpp"

// Camera takes: real 6DoF camera motion recorded on a phone (ARKit world
// tracking) and imported as free camera keys on a Cutscene Director camera
// track - "walk around a room looking around" becomes a PS2 cutscene move.
//
// The pipeline is split in two on purpose:
//   1. take ACQUISITION - anything that produces a CamTake. Two sources today:
//      the file loaders below (.hfcs from the CamTrackAR iPhone app, plus a
//      canonical CSV any tool can write - spec in docs/camera-takes.md), and
//      the live phone camera link (src/phonecam.hpp, docs/phone-camera.md),
//      which appends CamTakeSamples over the LAN while the editor runs.
//   2. take -> keys (bakeCamTake) - mapping into the scene, resampling and
//      decimation. Pure functions of (CamTake, CamTakeMapping); callable on a
//      partially filled take, so a live buffer can be re-baked as it grows.
//
// Canonical take space (what CamTakeSample stores, all loaders convert into
// it): the ARKit world convention - right-handed, Y up (gravity-aligned),
// positions in meters, quaternion (x, y, z, w) rotating camera-local axes
// into world axes, camera looks down its local -Z with +Y up. This matches
// the game world's axes (Y up, camera at +Z looking toward -Z), so mapping
// into the scene is only scale + yaw + origin - no handedness surgery.

struct CamTakeSample {
    double t = 0.0;       // seconds (loader-native epoch; bake rebases to 0)
    float pos[3] = {0.0f, 0.0f, 0.0f};   // meters, canonical space
    float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // x, y, z, w
    float fovDeg = 0.0f;  // vertical FOV in degrees; 0 = not recorded
};

struct CamTake {
    std::string source;   // human-readable origin ("CamTrackAR (.hfcs)", "CSV", ...)
    float fps = 0.0f;     // native sample rate; 0 = irregular/unknown
    std::vector<CamTakeSample> samples;  // sorted by t

    double duration() const {
        return samples.size() < 2 ? 0.0 : samples.back().t - samples.front().t;
    }
};

// Loaders. Return false and set `error` on failure; on success `out` is
// replaced. loadCamTakeAuto dispatches on the file extension.
bool loadCamTakeHfcs(const std::string& path, CamTake& out, std::string& error);
bool loadCamTakeCsv(const std::string& path, CamTake& out, std::string& error);
bool loadCamTakeAuto(const std::string& path, CamTake& out, std::string& error);

// How a take lands in the scene. Positions map as
//   eye = origin + yaw(pos - firstPos) * scale
// so the take's first sample sits exactly at `origin` and the extra yaw
// pivots the whole path around it.
struct CamTakeMapping {
    float scale = 1.0f;                    // game units per meter
    float yawDeg = 0.0f;                   // extra rotation about game +Y
    float origin[3] = {0.0f, 0.0f, 0.0f};  // where the first sample lands
    float timeOffset = 0.0f;   // seconds added to key times ("start at playhead")
    float tolerance = 0.05f;   // decimation error bound, game units (see below)
    // The canonical-space position that lands exactly on `origin`. Unset = the
    // take's FIRST sample, which is what a file import wants. The live phone
    // link pins it at Recenter instead: its stream has no meaningful "first"
    // sample, and without an explicit anchor the whole path would jump the
    // moment a recording starts mid-stream.
    bool hasAnchor = false;
    float anchor[3] = {0.0f, 0.0f, 0.0f};
    // Live recording: emit one key every 1/keyRate seconds (the Cutscene
    // Director's keyframe density) instead of RDP-decimating. 0 = decimate by
    // `tolerance`, which is what every file import does. The two are
    // deliberately exclusive - a density that is then decimated away is not a
    // density.
    float keyRate = 0.0f;
    // Roll (the Dutch angle - rotation about the lens axis). `anchorRoll` is
    // subtracted from every sample's measured roll, so Recentre makes whatever
    // way you are holding the phone count as level; `rollScale` then damps what
    // is left - 1 keeps the tilt as filmed, 0 pins the horizon level and throws
    // hand tremble away. Roll is invariant under `yawDeg` (a rotation about
    // world +Y turns the view and the up vector together), so it needs no
    // mapping of its own beyond these two.
    float anchorRoll = 0.0f;
    float rollScale = 1.0f;
};

struct CamTakeBakeStats {
    int sampleCount = 0;
    int keyCount = 0;
    float duration = 0.0f;  // seconds, of the baked keys
    float fovDeg = 0.0f;    // FOV written on the keys
};

// Maps ONE canonical sample into the scene: eye + look-at, exactly as
// bakeCamTake would place it. `anchor` is the canonical position that lands on
// map.origin (map.anchor when map.hasAnchor, else the take's first sample).
// Shared so the live phone-camera view and the baked keys cannot drift apart -
// what you frame through the phone is what the keys record.
// `outRoll` (optional) receives the mapped Dutch angle in degrees.
void mapCamSample(const CamTakeSample& s, const CamTakeMapping& map,
                  const float anchor[3], float outEye[3], float outTarget[3],
                  float* outRoll = nullptr);

// The sample's own roll in degrees - how far the device is tilted about its lens
// axis relative to the horizon - before anchorRoll/rollScale are applied. Used
// to capture the anchor roll at Recentre.
float camSampleRollDeg(const CamTakeSample& s);

// The anchor bakeCamTake will use for this (take, mapping) pair.
void camTakeAnchor(const CamTake& take, const CamTakeMapping& map, float out[3]);

// Bakes a take into free camera keys (empty `camera` binding, linear easing).
// The look-at target is placed kCamTakeLookDist meters in front of the eye
// along the sample's view direction, and the device's tilt about that axis lands
// in SeqCameraKey::roll (damped by CamTakeMapping::rollScale, zeroed against
// anchorRoll). Keys are decimated with time-parameterized
// Ramer-Douglas-Peucker on the (eye, target) curve: a sample is kept only if
// dropping it would move the interpolated eye or target by more than
// `tolerance` game units (the PS2 runtime lerps eye/target exactly like the
// decimator assumes) - or, when `map.keyRate` is set, resampled at that fixed
// density instead (live recording). FOV is the take's average (per-key FOV
// would fight the decimator); 60 if the take has none.
std::vector<SeqCameraKey> bakeCamTake(const CamTake& take, const CamTakeMapping& map,
                                      CamTakeBakeStats* stats = nullptr);

// Upper bound on the keys one bake may emit in fixed-rate mode. The keyframe
// tables are compiled into sequences.gen.cpp and live in the ELF, so a long
// recording at a high density is coarsened rather than allowed to blow the
// build up.
constexpr int kCamTakeMaxKeys = 2048;

// Eye -> look-at distance, meters (scaled like positions). 2 m keeps the
// tolerance meaningful for rotation too: at scale 1 a 0.05 u tolerance is
// ~1.4 deg of view direction.
constexpr float kCamTakeLookDist = 2.0f;

// Heading in degrees (atan2(fwd.x, fwd.z), same +Z-forward convention as the
// game) of one sample's view direction in canonical space, before any mapping
// yaw. Used to aim a path along the editor view: set CamTakeMapping::yawDeg =
// viewHeading - camSampleYawDeg(sample). The take form reads the FIRST sample
// (a file import aims by where the recording starts); the live phone link aims
// by the pose it is holding right now.
float camSampleYawDeg(const CamTakeSample& s);
float camTakeInitialYawDeg(const CamTake& take);  // 0 for an empty take
