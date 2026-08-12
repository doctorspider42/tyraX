#pragma once

#include <string>
#include <vector>

#include "glbparser.hpp"
#include "project.hpp"

// Foot IK authoring, host side (docs/foot-ik.md).
//
// Host-only: no GL, no ImGui, no App - the placement/aobake shape, so bone
// auto-detection and rig validation run from a 40-line harness against a real
// skeleton instead of only from a docked panel. The SOLVER is not here: it
// lives in the generated game (templates.cpp's applyFootIk) over the engine's
// generic SkelPoseAdjust, and the editor deliberately does not carry a second
// copy of it - a twin nobody could see drifting is worse than no twin.
//
// What this module owns is the part that decides whether the console can solve
// the rig at all: which bones are legs, and whether the six names still name a
// real hip > knee > ankle chain in the file. It takes the skeleton as two plain
// arrays (names + parent indices), which is what both the .glb/.fbx info cache
// and a harness can hand it.
namespace footik {

// The skeleton as the checker needs it: `parents[i]` is the parent node index
// of node i, or -1 for a root.
struct Skeleton {
    const std::vector<std::string>* names = nullptr;
    const std::vector<int>* parents = nullptr;

    size_t size() const { return names ? names->size() : 0; }
    const std::string& name(size_t i) const { return (*names)[i]; }
    int parent(size_t i) const {
        return parents && i < parents->size() ? (*parents)[i] : -1;
    }
};

// One bone slot of a rig, for the reports and the widgets: which field it is
// and what the file says about the name currently in it.
enum class Slot {
    LeftHip, LeftKnee, LeftAnkle, RightHip, RightKnee, RightAnkle, Count
};

// Human label for a slot ("Left hip"), the one place those strings live.
const char* slotLabel(Slot s);

// The rig's field for a slot, so the tool renders six identical rows instead of
// six copies of one row.
std::string& slotField(FootIkRig& r, Slot s);
const std::string& slotField(const FootIkRig& r, Slot s);

// Node index of `name`, or -1 when absent, or -2 when the file carries the name
// MORE THAN ONCE. Ambiguity is reported rather than resolved: the generated
// game binds by name, so a duplicate would silently pick whichever came first
// and could retarget to the other leg on the next export.
int findNode(const Skeleton& skel, const std::string& name);

// What is wrong with a rig, if anything.
struct Report {
    int node[(int)Slot::Count] = {-1, -1, -1, -1, -1, -1};
    bool complete = false;  // all six named bones exist and are unambiguous
    bool chains = false;    // ... and each is a real hip > knee > ankle chain
    // One line per problem, already phrased for a person ("Left knee: 'shin.L'
    // is not in the model"). Empty when the rig is solvable.
    std::vector<std::string> problems;

    // Solvable on the console. An unsolvable rig is not an error - the game
    // leaves the animation untouched - but it is worth saying out loud in the
    // tool rather than discovering it on a console.
    bool ok() const { return complete && chains; }
};

Report validate(const FootIkRig& r, const Skeleton& skel);

// Guesses the six leg bones from their names. Recognises the conventions that
// actually turn up: Mixamo (LeftUpLeg / LeftLeg / LeftFoot), Blender Rigify
// (thigh.L / shin.L / foot.L), Unreal (thigh_l / calf_l / foot_l), the plain
// English spellings (UpperLeg.L / LowerLeg.L / Ankle.L) and the numbered
// Quaternius style (Leg_Left_1..3). Sides are paired by the left/right marker
// in the name.
//
// Only the six names are written; `enabled` is left alone, because a guess is a
// starting point the author confirms - turning on a solver nobody looked at is
// how a character ends up with a broken knee in a build. Returns how many slots
// it managed to fill.
int autoDetect(const Skeleton& skel, FootIkRig& out);

// The rig's sole offset, MEASURED from the model instead of guessed: how high
// the ankle stands in the model's own BIND pose, above the model's own floor.
// `bindY` is per bone (the .glb/.fbx info cache fills it during its ordinary
// parse) and `floorY` is the bottom of the model's bind bounds.
//
// Why the ankle's height and not "the lowest vertex of the foot", which is the
// obvious formulation and the one this function had first: a part's stored
// vertex positions are NOT necessarily in the same space as the composed node
// hierarchy - on the FBX path they are pre-skin control points, and the skin
// matrix is what places them. Comparing the two answered 0.227 on a rig whose
// ankle really stands at 0.104, i.e. it would have written a number that sinks
// every foot by 12 cm. The ankle's own height needs no mesh at all and is right
// for any model whose bind pose stands on the floor, which is every character
// rig in practice.
//
// Why it matters: this number is a CONSTANT offset under every planted foot, so
// a wrong one is not a stairs bug or a slope bug - it is a shoe sunk (or
// floating) by that much on flat ground, everywhere. The SMALLER of the two
// ankles wins, so an asymmetric bind pose cannot lift the character.
//
// Returns 0 when it cannot be measured (no complete rig); the caller keeps
// whatever it had.
float measuredSoleOffset(const Report& report, const std::vector<float>& bindY,
                         float floorY);

// Fills the per-bone bind-pose heights measuredSoleOffset consumes.
//
// The composition is full 4x4 - translation, rotation and scale, parent then
// child - and that is not a detail: a bind pose with rotated bones is the normal
// case (every Unreal-style rig has them), and summing local translations instead
// answered 1.031 where the ankle really stands at 0.104.
void bindHeights(const glbparser::Skel& skel, std::vector<float>& bindY);

}  // namespace footik
