#pragma once

#include <string>
#include <vector>

#include "glbparser.hpp"
#include "project.hpp"

// Foot IK, host side (docs/foot-ik.md).
//
// Host-only: no GL, no ImGui, no App - the aobake/placement shape, so the
// whole thing (auto-detection, resolution, the pose evaluator and the solver)
// runs from a 40-line harness against a real .glb.
//
// Three jobs, and they are deliberately separate:
//
//  1. AUTHORING. autoDetect() guesses the leg chains from bone names, and
//     resolve() turns the authored names into the indices the console binds
//     by, reporting every name it could not find rather than silently
//     dropping a leg.
//  2. VERIFICATION. evalPose() is a host twin of the engine's
//     SkelInstance::evalPose, and solve() of its FootIk - same math, same
//     order. That is what lets the editor answer "would this rig work" and,
//     more importantly, what lets the dataset generator produce IK-corrected
//     poses to train the gait net against (docs/neural-gait.md).
//  3. Nothing else. The solver never learns about the world here either -
//     it takes a ground callback exactly like the engine's does.
//
// The engine's vendor/tyra/.../skel_foot_ik.cpp is the twin of solve(). Change
// one, change both, or the trained net and the console disagree about what a
// corrected pose looks like.
namespace footik {

// A rig with every bone name looked up. -1 anywhere means "not found", which
// resolve() also records in `problems` so the UI can say WHICH bone.
struct Resolved {
    struct Leg {
        int hip = -1, knee = -1, ankle = -1, toe = -1;
    };
    std::vector<Leg> legs;
    int pelvis = -1;
    std::vector<int> netJoints;  // the net's extra joints, in output order
    std::vector<std::string> problems;

    // Solvable on the console: at least one complete leg and nothing dangling.
    bool ok() const { return problems.empty() && !legs.empty(); }
};

// Index of a node by exact name, then case-insensitively, then -1.
int findNode(const glbparser::Skel& skel, const std::string& name);

Resolved resolve(const AnimRig& rig, const glbparser::Skel& skel);

// Guesses a rig from bone names. Recognises the common conventions - Mixamo
// (LeftUpLeg/LeftLeg/LeftFoot/LeftToeBase), Blender Rigify (thigh/shin/foot),
// UE (thigh_l/calf_l/foot_l), and the plain thigh/knee/ankle spellings - and
// pairs sides by the left/right marker in the name. Returns a rig with
// `enabled` OFF: a guess is a starting point the author confirms, and turning
// on a solver nobody looked at is how a character ends up with a broken knee
// in a build.
AnimRig autoDetect(const std::string& modelRel, const glbparser::Skel& skel);

// Distance from the ankle down to the lowest vertex skinned to that leg's
// foot, in the bind pose - the real sole offset, measured instead of guessed.
// Falls back to the toe joint's drop, then to 0.08.
float measureSoleOffset(const glbparser::Skel& skel, const Resolved& r);

// --- the host twin of the runtime ---------------------------------------

// Model-space global transform per node, column-major 4x4 (16 floats each).
typedef std::vector<float> Pose;  // nodes * 16

// Samples clip `clip` at `time` seconds and walks the hierarchy - the same
// rules as SkelInstance (clamp outside the key range, STEP holds the left
// key, rotations slerp). Clip edits are NOT applied here; hand in a skeleton
// animedit::applyClipEdits has already folded, exactly as the bake does.
void evalPose(const glbparser::Skel& skel, int clip, float time, Pose& out);

// What the world answers under a traced point. Same contract as the engine's
// FootIk::GroundFn: false means "nothing to stand on" and the foot keeps the
// clip's own placement.
struct Ground {
    virtual ~Ground() {}
    virtual bool sample(const float world[3], float up, float down,
                        float* outY, float outNormal[3]) const = 0;
};

// A flat floor with one step in it - the ground the Foot IK tab previews
// against and the dataset generator's simplest terrain. `stepZ` is where the
// riser is, `stepHeight` how tall.
struct StepGround : public Ground {
    float baseY = 0.0f;
    float stepZ = 0.0f;
    float stepHeight = 0.0f;
    bool sample(const float world[3], float up, float down, float* outY,
                float outNormal[3]) const override;
};

// Per-instance solver state (the springs) - one per character, carried
// across frames exactly like the engine's FootIk.
struct State {
    float offset[4] = {0, 0, 0, 0};
    float offsetVel[4] = {0, 0, 0, 0};
    float normal[4][3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
    bool grounded[4] = {false, false, false, false};
    float pelvisOffset = 0.0f;
};

// Applies the solver to `pose` in place. `world` is the object-to-world
// matrix (column-major 4x4); `dt` drives the springs.
void solve(const glbparser::Skel& skel, const AnimRig& rig,
           const Resolved& resolved, const Ground& ground, const float world[16],
           float dt, State& state, Pose& pose);

}  // namespace footik
