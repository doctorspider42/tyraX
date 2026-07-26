#pragma once

#include <memory>
#include <string>
#include <vector>

#include "glbparser.hpp"

// Procedural locomotion for a Mixamo-named rig: idle / walk / run / jump,
// generated analytically instead of imported. Host-only, no GL, no Project -
// the treegen/chargen pattern, exercisable from a host harness.
//
// Why generate them at all: a character with no clips is a statue, and the
// generated bodies have no animation of their own. Analytic cycles cost
// nothing, carry no licence, are deterministic, and are exactly the era-
// correct look - a PS2 walk cycle is a dozen keys per bone. They also give
// every generated character the `idle` / `walk` / `run` / `jump` names the
// generated game's third-person locomotion already looks for, so a character
// dropped in as a Player avatar walks and runs with no further setup.
//
// Bones are found BY NAME ("mixamorig:LeftUpLeg", ...), not by index, so this
// module is independent of chargen's bone table - and would work just as well
// on an imported Mixamo rig. Missing bones are skipped rather than faked.
//
// Conventions this relies on (both true of chargen's output): the character
// faces +Z with +X to its own left, and every bone's BIND ROTATION IS
// IDENTITY, so a bone's local rotation axes are the world axes. Feeding a rig
// with rolled bind rotations through here would produce nonsense.
namespace charanim {

struct Params {
    // Cycle lengths in seconds. The walk is the reference; the run is not just
    // a faster walk (bigger amplitudes, forward lean, longer flight).
    float walkSeconds = 1.0f;
    float runSeconds = 0.62f;

    float stride = 1.0f;    // leg swing multiplier
    float armSwing = 1.0f;  // arm swing multiplier
    // -1 slouched (head forward, shoulders rolled), 0 neutral, 1 upright.
    float posture = 0.0f;
    // Idle liveliness: 0 = dead still, 1 = plenty of breathing and weight
    // shifting. Never a random walk - the same value always plays the same.
    float idleMotion = 1.0f;

    int fps = 15;  // keys per second sampled from the analytic curves

    bool operator==(const Params& o) const {
        return walkSeconds == o.walkSeconds && runSeconds == o.runSeconds &&
               stride == o.stride && armSwing == o.armSwing && posture == o.posture &&
               idleMotion == o.idleMotion && fps == o.fps;
    }
    bool operator!=(const Params& o) const { return !(*this == o); }
};

// Replaces `skel.clips` with idle / walk / run / jump. `idle` comes first so a
// scene object that plays "the model's first clip" idles rather than standing
// in the bind pose.
void addLocomotion(glbparser::Skel& skel, const Params& p);

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
void applyLive(const LiveRetarget& binding, const float* srcLocalRot, const float* hipsWorld,
               glbparser::Skel& target);

// Forgets where the performer was, so the NEXT frame carrying a hips position
// becomes the new origin. Needed whenever the stream jumps rather than moves:
// tracking was lost and reacquired, somebody else stepped in front of the
// camera, or the operator simply wants the character back where it was placed.
// (The clip path calls this between clips - each take starts from its own first
// frame, which is why the two paths agree.)
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
