#pragma once

#include <string>
#include <vector>

// Head and wrist orientation from what Apple's Vision framework saw, solved on
// the HOST.
//
// ARKit's body tracking reports a head joint and two wrists and solves none of
// them: measured over a 277-frame take, their local rotations did not change by
// a single float bit (see mocap.hpp). Hand and face tracking on iOS live in
// Vision, not in ARKit - a different framework, running over the same camera
// frames from the same session - so the data exists, it is simply somewhere
// else.
//
// **The phone sends observations, this module solves.** That split is
// deliberate and it is the whole reason this file is C++ and not Swift:
//
//  - it can be TESTED. A harness synthesizes a known hand orientation, projects
//    it through a synthetic camera, and checks that the solver gets it back.
//    Nothing equivalent is possible for Swift here - there is no Mac in this
//    loop, and CI only proves it compiles.
//  - a fix does not need a release. Getting an axis convention wrong is the
//    likeliest failure, and correcting it must not mean a build, a tag, an
//    AltStore round trip and a reinstall on somebody's phone.
//
// So the phone stays a sensor: Vision's raw landmarks, the face's angles, the
// camera's pose. Everything below is geometry over those numbers.
namespace visionpose {

// One frame of what the phone saw, beside the body skeleton it already sends.
// Absent parts are simply not `have`.
struct Observation {
    // The camera's orientation in ARKit world space. Vision reports the face's
    // angles RELATIVE TO THE CAMERA, so without this a head is oriented in a
    // frame that moves whenever the operator does.
    bool haveCamera = false;
    float cameraRot[4] = {0, 0, 0, 1};

    // Vision's face angles, radians, as VNFaceObservation reports them: yaw
    // about the image's vertical, pitch about its horizontal, roll about the
    // view direction.
    bool haveFace = false;
    float faceYaw = 0.0f, facePitch = 0.0f, faceRoll = 0.0f;

    // A hand, as four palm landmarks in NORMALIZED IMAGE COORDINATES with the
    // origin top-left and +y down. Four is enough: the wrist and three
    // knuckles fix the palm's plane, and they are the largest, steadiest points
    // Vision returns - the fingertips are neither.
    struct Hand {
        bool have = false;
        float confidence = 0.0f;
        float wrist[2] = {0, 0};
        float indexMcp[2] = {0, 0};
        float middleMcp[2] = {0, 0};
        float littleMcp[2] = {0, 0};
        // The thumb's base, and it is not one landmark among four - it is the
        // one that OUT OF THE PALM'S PLANE. The wrist and three knuckles are
        // very nearly coplanar, and a plane's image is identical to its mirror
        // image, so those four alone leave two poses fitting equally well
        // forever. The thumb sits off that plane, so it tells the palm from the
        // back of the hand. Set `haveThumb` false when Vision was not sure.
        bool haveThumb = false;
        float thumbMcp[2] = {0, 0};
    };
    Hand left, right;
};

// How far a joint may be pushed away from following its parent. A single bad
// detection must not spin a head backwards, and these are anatomy, not taste.
struct Limits {
    float headYaw = 80.0f;    // degrees
    float headPitch = 55.0f;
    float headRoll = 40.0f;
    float wristBend = 70.0f;
    float wristDeviate = 35.0f;
    float wristTwist = 85.0f;
    // Below this, an observation is ignored rather than believed weakly.
    float minConfidence = 0.4f;
};

// The head's LOCAL rotation, relative to the joint above it.
//
// `parentWorldRot` is that joint's world rotation in the same space the body
// skeleton is expressed in. Returns false when there is nothing to solve from,
// leaving `outLocal` untouched - the caller then keeps whatever the source had.
bool solveHead(const Observation& o, const float* parentWorldRot, const Limits& limits,
               float* outLocal);

// The wrist's LOCAL rotation, relative to the forearm.
//
// The 2D landmarks alone cannot fix a 3D orientation - depth is missing and a
// palm and its mirror project identically. What closes it is that the forearm's
// 3D orientation is already known from the body skeleton, so the search runs
// over the wrist's own anatomical range and picks the pose whose PROJECTION
// matches what Vision saw. `forearmWorldRot` is that known orientation;
// `bindPalm*` describe where the palm's landmarks sit in the wrist's own frame
// when the hand is flat, which is what the projection is compared against.
struct PalmShape {
    // In the wrist's local frame, hand flat, fingers along `forward`.
    float forward[3] = {0.0f, -1.0f, 0.0f};  // wrist -> middle knuckle
    float across[3] = {1.0f, 0.0f, 0.0f};    // index knuckle -> little knuckle
    // Metres. These are not decoration: the LENGTHS are what make the solve
    // possible at all. Two projected directions give two constraints for three
    // unknowns, so direction alone leaves a whole family of orientations that
    // look identical - measured, that family is 60 to 160 degrees wide. What
    // closes it is foreshortening: a palm turned away from the camera projects
    // SHORTER, and by how much says how far it turned. Only the ratio matters,
    // so distance and focal length cancel and no lens data is needed.
    float forwardLen = 0.095f;
    float acrossLen = 0.080f;
    // Where the thumb's base sits, in multiples of the palm's own size: a bit
    // along the fingers, well over to the thumb side, and - the part that
    // matters - lifted OFF the palm's plane. That last number is the only thing
    // in this struct that distinguishes a hand from its mirror.
    float thumbForward = 0.30f;
    float thumbAcross = -0.45f;
    float thumbOutOfPlane = -0.28f;
};

// `preferLocal` breaks the tie that geometry cannot. Under this projection a
// palm and its MIRROR - the hand flipped front to back - produce identical
// images, so two poses always fit equally well and no amount of pixel accuracy
// separates them. Pass the previous frame's answer (or null for the rest pose)
// and the nearer one wins. Measured: without it, five of nine synthetic cases
// came back as the mirror, wrong by 63 to 117 degrees.
bool solveWrist(const Observation& o, bool leftHand, const float* forearmWorldRot,
                const float* cameraRot, const PalmShape& palm, const Limits& limits,
                float* outLocal, const float* preferLocal = nullptr);

// What carries between frames. Vision is noisy at the hand size a full-body
// shot leaves - measured on synthetic data, a solver run fresh every frame lands
// within 1.2 degrees on clean input and jitters by about 5 at realistic
// landmark noise - so the previous answer is worth keeping twice over: as the
// prior that breaks the mirror, and as the thing the new answer is blended
// toward.
//
// `smoothing` is the fraction of the OLD answer kept: 0 trusts every frame
// completely, 1 never moves. It is applied per joint, and a joint that goes
// unseen for a while simply keeps its last value rather than snapping to rest.
struct Tracker {
    float head[4] = {0, 0, 0, 1};
    float wrist[2][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}};
    bool haveHead = false;
    bool haveWrist[2] = {false, false};
    float smoothing = 0.5f;
    void reset() { *this = Tracker(); }
};

// Everything above, applied to one frame of source rotations in place: the
// joints ARKit leaves frozen are overwritten with what Vision saw, and the
// retarget downstream neither knows nor cares. `jointNames`/`parents` are the
// source skeleton's, `localRot` its 4-floats-per-joint frame.
//
// Returns how many joints were actually driven, and appends a line per joint it
// could not solve so the caller can say why on screen.
int applyToFrame(const Observation& o, const std::vector<std::string>& jointNames,
                 const std::vector<int>& parents, const float* restRot, const Limits& limits,
                 float* localRot, Tracker* tracker = nullptr,
                 std::vector<std::string>* notes = nullptr);

}  // namespace visionpose
