#pragma once

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
