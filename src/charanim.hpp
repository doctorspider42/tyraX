#pragma once

#include <memory>
#include <string>
#include <vector>

#include "glbparser.hpp"

// Retargeting and host skinning for a Mixamo-named rig. Host-only, no GL, no
// Project - exercisable from a host harness.
//
// The Character Generator's own clips are retargeted OFFLINE onto its rig
// (tools/chargen-kit/anim_retarget.py) and ship in the character kit; what
// lives here is the runtime half: importing someone else's clips (a Mixamo
// download, a phone .tmocap take) onto a character, the live phone link, and
// poseMesh, which lets the editor preview play any of them.
//
// Bones are found BY NAME ("mixamorig:LeftUpLeg", ...), not by index. The
// target rig's BIND ROTATIONS must be identity (a bone's local axes are the
// world axes), which is true of everything the generator writes.
namespace charanim {

// Puts the feet on the floor. Motion capture gives joint ROTATIONS, and a
// rotation-only pose has no idea where the ground is: a source that never
// solves the ankle (ARKit does not - see mocap.hpp) leaves the foot rigidly
// following the shin, so a lifted knee comes with a pointed toe and a standing
// foot skates across the floor by however much the hips estimate wobbles.
//
// This is not a fudge over bad data - EVERY rotation-driven retarget needs it,
// including a clean Mixamo clip on a character of different proportions, where
// the same rotations put a shorter leg through the floor.
struct GroundOptions {
    bool enabled = true;
    // A foot below `plantHeight` and slower than `plantSpeed` is standing on
    // something. It stays planted until it rises past `releaseHeight` - the gap
    // is hysteresis, and without it a foot hovering at the threshold flickers
    // between planted and free every other frame.
    float plantHeight = 0.045f;    // metres
    float releaseHeight = 0.090f;  // metres
    float plantSpeed = 0.45f;      // metres/second
    float blend = 0.10f;           // seconds to ease a plant in or out
    // The leg may not straighten completely: at full extension the two-bone
    // solve has no knee direction left and the joint snaps through.
    float maxReach = 0.985f;
    float floorY = 0.0f;           // the character's own space; chargen binds feet here
};

struct RetargetOptions {
    // Mixamo exports at 24-30 fps with a key on every frame for all ~65 bones.
    // The PS2 evaluates keys on the EE, so resampling to a rate a PS2 game
    // would actually use is most of the win here.
    float fps = 15.0f;
    // Strip horizontal root motion: the clip animates in place and the game
    // moves the character, which is what every locomotion system here expects.
    bool inPlace = true;
    // Drop clips shorter than this (Mixamo merges leave a 0-second "mixamo.com"
    // clip that is just the bind pose).
    float minSeconds = 0.05f;
    GroundOptions ground;
};

// Retargets every clip of `source` onto `skel`, replacing its clips. Both rigs
// are matched BY BONE NAME (the Mixamo names chargen writes), and only the
// bones `skel` actually has are driven - a source rig's fingers and twist
// bones are simply not sampled, which is most of why a 156-channel Mixamo clip
// lands as a ~23-channel one.
//
// The conversion is a rotation DELTA: the source bone's animated global
// rotation relative to its own BIND global rotation is applied to the target's
// bind orientation. That is what makes an authored rig (whose bind rotations
// are not identity) drive a generated one (whose are), and a constant root
// transform on the source - the -90 degree X flip an FBX conversion leaves
// behind, a 0.01 unit scale - cancels out of the delta for free.
//
// Returns the number of clips written; 0 means nothing matched (with a note in
// `warnings`), and the caller should keep whatever clips it had.
int retarget(const glbparser::Skel& source, glbparser::Skel& skel, const RetargetOptions& opts,
             std::vector<std::string>& warnings);

// ---------------------------------------------------------------------------
// The same retarget, one frame at a time - for a pose arriving live from a
// phone rather than a clip read off disk.
//
// `retarget()` above is implemented ON TOP of this, so the two cannot drift:
// whatever a recorded take produces, the live stream produces from the same
// numbers. (The harness asserts it: posing the clip and posing the live path at
// the same instants gives identical vertices.)

// A prepared source-rig -> character binding. Build it once when a device
// connects and feed it frames; it holds the source's bind pose, the joint
// mapping and the height ratio, none of which change while a session lasts.
struct LiveRetarget {
    struct State;
    std::shared_ptr<State> state;
    bool valid() const { return state != nullptr; }
    // How many bones of the character the source can actually drive.
    int matchedBones() const;
    // The source rig's node count - `applyLive` expects this many rotations.
    int sourceNodeCount() const;
};

// `source` needs no clips: only its skeleton and bind pose are used. Returns an
// invalid binding (and a note) when the two rigs share no bones.
LiveRetarget prepareLive(const glbparser::Skel& source, const glbparser::Skel& target,
                         const RetargetOptions& opts, std::vector<std::string>& warnings);

// Applies one source frame to `target`, writing the live pose into its NODE
// transforms - `poseMesh(target, -1, 0, ...)` then skins exactly that pose.
//
// `srcLocalRot` is 4 floats per source node (x, y, z, w), in the order
// `source.nodes` had at prepare time. `hipsWorld` is an optional 3-float world
// translation for the hips; the first frame it is seen becomes the origin the
// rest are measured against, so a performer standing anywhere maps onto a
// character standing where it was placed.
// `timeSeconds` is the frame's own timestamp and drives the ground solve, which
// is stateful (a plant persists between frames and eases in and out). Pass the
// stream's or the clip's time; frames arriving out of order or with the clock
// jumping backwards simply restart the plant.
void applyLive(const LiveRetarget& binding, const float* srcLocalRot, const float* hipsWorld,
               glbparser::Skel& target, float timeSeconds = -1.0f);

// Forgets where the performer was, so the NEXT frame carrying a hips position
// becomes the new origin. Needed whenever the stream jumps rather than moves:
// tracking was lost and reacquired, somebody else stepped in front of the
// camera, or the operator simply wants the character back where it was placed.
// (The clip path calls this between clips - each take starts from its own first
// frame, which is why the two paths agree.) It also forgets which feet were
// planted, for the same reason: a jump is not a step.
void resetLiveOrigin(const LiveRetarget& binding);

// Linear-blend skinning on the host: poses EVERY part at `time` of `clipIndex`
// and writes one interleaved pos3 + normal3 + uv2 array per part, ready for
// Viewport::CharPreviewDesc. This is what lets the editor preview PLAY a
// generated cycle - the same evaluation the PS2 does on VU0, at a scale where
// doing it per frame on the CPU is free. Per part rather than concatenated
// because each carries its own texture (body skin, clothes, hair).
// clipIndex < 0 (or an empty clip list) writes the bind pose.
void poseMesh(const glbparser::Skel& skel, int clipIndex, float time,
              std::vector<std::vector<float>>& outParts);

}  // namespace charanim
