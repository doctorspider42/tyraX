#pragma once

#include <string>
#include <vector>

#include "footik.hpp"
#include "glbparser.hpp"
#include "project.hpp"

// The gait net, host side (docs/neural-gait.md).
//
// Host-only: no GL, no ImGui, no App - so the feature vector, the dataset
// generator and the .tnet writer all run from a 40-line harness.
//
// This file is the SINGLE SOURCE of three things that must agree or the net
// reads garbage:
//
//   1. the feature layout, mirrored by the engine's motion_net.cpp;
//   2. the joint ORDER the outputs map onto, which the .tnet carries so the
//      runtime never has to derive it a second time;
//   3. the .tnet binary layout, mirrored by MotionNetLoader.
//
// The trainer (tools/motion-net/train.py) never touches any of them: it reads
// a CSV whose columns this file wrote and writes a weights JSON this file
// bakes. That is deliberate - a Python reimplementation of the binary format
// is exactly the kind of second answer that drifts.
namespace motionnet {

// Keep in step with Tyra::MotionFeatures. A change here is a format change:
// bump kFeatureVersion and every existing .tnet is refused at load rather
// than silently fed shifted columns.
enum {
    kFeatureVersion = 1,
    kProbeForward = 3,
    kProbeLateral = 3,
    kProbeCount = kProbeForward * kProbeLateral,
    kMaxLegs = 4,
    kFeatureCount = 5 + kProbeCount + 2 * kMaxLegs,
};

// How the probes are placed and how values are normalized. Baked into the
// .tnet, so a retrained net may change its own sampling pattern without a
// code change on either side.
struct Params {
    float probeForward[kProbeForward] = {-0.45f, 0.35f, 1.1f};
    float probeLateral[kProbeLateral] = {-0.3f, 0.0f, 0.3f};
    float probeScale = 1.0f;   // heights and offsets are divided by this
    float refSpeed = 3.0f;     // the speed that reads as 1.0
    float outScale = 0.35f;    // radians per unit of raw rotation output
    float phaseRateRange = 0.35f;  // how far the net may pull playback speed
};

// Sensible probe spacing and scales for one model - derived from its own size
// so the same defaults work for a 1.8-unit human and a 3.4-unit one.
Params defaultParams(const glbparser::Skel& skel, const AnimRig& rig);

// The joints the net writes a delta for, IN OUTPUT ORDER: every leg's
// hip/knee/ankle first, then the rig's extra joints (pelvis, spine). This one
// function is what the dataset columns, the trained net and the .tnet all
// agree through.
std::vector<int> outputJoints(const AnimRig& rig, const footik::Resolved& r);

// The feature vector, mirroring Tyra::MotionPoseCorrector::buildFeatures.
// `ground` answers the probes; `legOffset`/`legPlanted` are what the IK did
// to each foot last frame (zero/false when there is no previous frame).
void buildFeatures(const Params& p, const footik::Ground& ground,
                   float phase, float speed, float turn, float strafe,
                   float yaw, const float rootWorld[3],
                   const float* legOffset, const bool* legPlanted,
                   int legCount, float* outFeatures);

// --- dataset generation --------------------------------------------------

struct DatasetOptions {
    std::string clip;        // the locomotion clip to walk on; "" = the first
    float speed = 1.6f;      // world units per second the character travels
    int frames = 20000;      // rows to emit
    float fps = 30.0f;       // simulation rate
    unsigned seed = 1;
    std::string error;       // set on failure
};

// Walks the character over procedurally generated ground - flats, single
// steps, staircases and ramps - and writes one CSV row per frame: the
// features, then the targets.
//
// The targets are what the IK SOLVER produced, per joint, as an
// exponential-map rotation delta. So the net is trained to imitate a solver
// it will then run in front of - which sounds circular and is not: the solver
// only ever sees the ground under the foot NOW, while the net's inputs carry
// the ground AHEAD. Distilling one into the other with a wider input is how
// the net comes out able to anticipate a step the solver can only react to.
//
// The phase-rate target is the one thing the solver cannot supply, so it is a
// stated rule rather than a distillation: a rising slope shortens the stride,
// and a shorter stride at the same ground speed means the clip plays faster.
std::string generateDataset(const glbparser::Skel& skel, const AnimRig& rig,
                            const footik::Resolved& resolved, const Params& p,
                            DatasetOptions& opt);

// --- the trained net ------------------------------------------------------

struct Layer {
    int inCount = 0, outCount = 0;
    bool relu = true;
    std::vector<float> weights;  // outCount rows of inCount
    std::vector<float> biases;   // outCount
};

struct Net {
    std::vector<Layer> layers;
    Params params;
    std::vector<int> joints;  // node ids, in output order
};

// Reads the trainer's weights JSON (tools/motion-net/train.py --out). Returns
// false with `error` set; it validates the shapes against the joint list
// rather than trusting them, because a mismatch here becomes a confident,
// wrong pose on the console.
bool readWeightsJson(const std::string& json, Net& out, std::string& error);

// The .tnet bytes MotionNetLoader reads. Keep the layout in sync with
// vendor/tyra/.../motion_net.cpp.
std::string writeTnet(const Net& net);

}  // namespace motionnet
