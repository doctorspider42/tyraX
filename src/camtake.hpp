#pragma once

#include <string>
#include <vector>

#include "sequence.hpp"

// Camera takes: real 6DoF camera motion recorded on a phone (ARKit world
// tracking) and imported as free camera keys on a Cutscene Director camera
// track - "walk around a room looking around" becomes a PS2 cutscene move.
//
// The pipeline is split in two on purpose:
//   1. take ACQUISITION - anything that produces a CamTake. Today that is the
//      file loaders below (.hfcs from the CamTrackAR iPhone app, plus a
//      canonical CSV any tool can write - spec in docs/camera-takes.md).
//      Phase 2 adds a live Wi-Fi/USB receiver that appends CamTakeSamples to
//      the same CamTake while the editor runs.
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
};

struct CamTakeBakeStats {
    int sampleCount = 0;
    int keyCount = 0;
    float duration = 0.0f;  // seconds, of the baked keys
    float fovDeg = 0.0f;    // FOV written on the keys
};

// Bakes a take into free camera keys (empty `camera` binding, linear easing).
// The look-at target is placed kCamTakeLookDist meters in front of the eye
// along the sample's view direction; phone roll has no representation in a
// SeqCameraKey and is dropped. Keys are decimated with time-parameterized
// Ramer-Douglas-Peucker on the (eye, target) curve: a sample is kept only if
// dropping it would move the interpolated eye or target by more than
// `tolerance` game units (the PS2 runtime lerps eye/target exactly like the
// decimator assumes). FOV is the take's average (per-key FOV would fight the
// decimator for phase 1); 60 if the take has none.
std::vector<SeqCameraKey> bakeCamTake(const CamTake& take, const CamTakeMapping& map,
                                      CamTakeBakeStats* stats = nullptr);

// Eye -> look-at distance, meters (scaled like positions). 2 m keeps the
// tolerance meaningful for rotation too: at scale 1 a 0.05 u tolerance is
// ~1.4 deg of view direction.
constexpr float kCamTakeLookDist = 2.0f;

// Heading in degrees (atan2(fwd.x, fwd.z), same +Z-forward convention as the
// game) of the take's FIRST sample view direction in canonical space, before
// any mapping yaw. Used to aim an imported path along the editor view: set
// CamTakeMapping::yawDeg = viewHeading - camTakeInitialYawDeg(take). Returns 0
// for an empty take.
float camTakeInitialYawDeg(const CamTake& take);
