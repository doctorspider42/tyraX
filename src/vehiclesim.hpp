#pragma once

#include <functional>
#include <string>
#include <vector>

// Vehicles (docs/vehicles.md): wheel detection + the drive model.
//
// Host-only - no GL, no ImGui, no App, no project.hpp - the scrollsim/placement
// shape, so the whole thing runs from a 40-line harness against a real .fbx
// instead of being clicked through a GUI. That is not a nicety here: this file
// is the single source of truth for TWO consumers that must never disagree -
// the editor's test drive (App drives the viewport preview straight through
// `step`) and the generated PS2 runtime (src/gen/vehicle.gen.cpp is its
// per-frame twin). A formula that exists twice is a formula that drifts; the
// endless scroller learned that first and this follows its arrangement.
//
// Two rules the module is built around:
//
//   1. `step` NEVER reads a pad. Its input is `DriveInput`, four numbers a
//      caller fills in. The player controller fills them from the Input Map
//      today; a nav-driven AI controller fills the identical struct later.
//      That boundary costs nothing now and is a rewrite of every control path
//      if it is added afterwards.
//
//   2. Detection REPORTS what it could not decide instead of guessing quietly.
//      `Detection::notes` is what the Vehicle Editor prints, and
//      `frontAssumed` is why that panel has a Flip button.
namespace vehiclesim {

// ---------------------------------------------------------------------------
// Wheel detection
// ---------------------------------------------------------------------------
//
// The input is deliberately NOT a parser type. A vehicle can arrive as .glb,
// .fbx or a hand-built pile of .obj files, and the detector must not care -
// it needs a name, a size and a material list per mesh node, and nothing else.
// Everything is in the FILE's own units; scale correction (modelUnitMeters /
// unitsPerMeter) is the importer's job and happens after this runs.
struct MeshNode {
    std::string name;   // the authored node name - frequently junk ("Cylinder.001")
    int vertexCount = 0;
    float mn[3] = {0.0f, 0.0f, 0.0f};  // AABB in model space
    float mx[3] = {0.0f, 0.0f, 0.0f};
    std::vector<std::string> materials;  // material names used by this node

    float extent(int axis) const { return mx[axis] - mn[axis]; }
    float centre(int axis) const { return 0.5f * (mn[axis] + mx[axis]); }
};

struct Wheel {
    int node = -1;  // index into the MeshNode vector handed to detectWheels
    // That node's authored name. The index is only meaningful against the
    // vector this detection ran on, and the editor has to store a wheel's
    // identity in the .tyra across re-imports - so the NAME travels with it.
    std::string nodeName;
    float centre[3] = {0.0f, 0.0f, 0.0f};  // anchor: the node's own AABB centre
    float radius = 0.0f;
    float width = 0.0f;
    bool front = false;  // relative to the resolved forward axis
    bool left = false;
    bool steered = false;  // seeded front-only; the author may override
    bool driven = false;   // seeded rear-only
};

struct Detection {
    bool found = false;
    std::vector<Wheel> wheels;  // ordered front-left, front-right, rear-left, rear-right

    // The vehicle's own frame, derived from the wheels alone (see the .cpp -
    // no file axis metadata is consulted, because exporters disagree about it
    // and a wheel cluster does not).
    int forwardAxis = 0, upAxis = 1, axleAxis = 2;
    int forwardSign = 1;  // +1 = the vehicle's nose points along +forwardAxis

    // True when nothing in the model said which end is the nose. The detector
    // still picks one so the import produces a working vehicle, but the panel
    // must say so and offer the flip - a car driving backwards is the single
    // most likely wrong answer this file can produce.
    bool frontAssumed = true;

    float wheelBase = 0.0f;  // front-axle centre to rear-axle centre
    float track = 0.0f;      // left wheel centre to right wheel centre
    float radius = 0.0f;     // mean wheel radius

    // Mesh nodes that are not wheels and carry geometry - the body bake.
    // Cameras, lights and empties never reach here (they carry no vertices).
    std::vector<int> bodyNodes;

    // What was decided and why, one line each, in the order decided. Printed
    // verbatim by the Vehicle Editor: a detection nobody can check is a
    // detection nobody should trust.
    std::vector<std::string> notes;
};

// Finds the wheel cluster in a parsed model. Never throws and never fails
// destructively - a model with no recognisable wheels comes back with
// `found == false`, every geometry node in `bodyNodes` and a note saying so,
// which the importer turns into a wheel-less vehicle that still drives.
Detection detectWheels(const std::vector<MeshNode>& nodes);

// ---------------------------------------------------------------------------
// The drive model
// ---------------------------------------------------------------------------
//
// A kinematic chassis on four height samples, not a rigid-body solver: the
// era-correct arrangement and the only one that fits the EE budget next to
// everything else a scene does. Lateral behaviour is one slip-angle term with
// a friction cap, which is what makes the difference between "on rails" and
// "drifting" a single authored number instead of a tyre model.
//
// Distances are WORLD UNITS and times are SECONDS. The generated runtime
// converts to its per-1/50 s step unit at the boundary, exactly like the
// player walk speeds do.
struct DriveSpec {
    // Geometry - seeded by detectWheels, then editable.
    float wheelBase = 2.0f;
    float track = 1.4f;
    float wheelRadius = 0.32f;

    // Longitudinal
    float topSpeed = 22.0f;         // units/s forward
    float reverseTopSpeed = 6.0f;
    float accel = 9.0f;             // units/s^2 at full throttle
    float brakeDecel = 18.0f;
    float engineBraking = 3.0f;     // deceleration with no throttle and no brake
    float drag = 0.0016f;           // quadratic, units/s^2 per (units/s)^2

    // Steering. maxSteer shrinks toward highSpeedSteer as the car approaches
    // topSpeed - without it a full-lock flick at speed spins the car instantly
    // and the vehicle is undriveable with a digital d-pad.
    float maxSteerDeg = 34.0f;
    float highSpeedSteerDeg = 11.0f;
    float steerRateDeg = 220.0f;    // how fast the wheels turn, deg/s
    float steerReturnDeg = 300.0f;  // self-centring with no steer input, deg/s

    // Lateral grip: the cap on how much sideways velocity the tyres kill per
    // second. Low = the car slides; high = it is on rails. handbrakeGrip
    // replaces it while the handbrake is held, which is the whole drift knob.
    float grip = 26.0f;
    float handbrakeGrip = 6.0f;

    // Ground contact
    float gravity = 24.0f;          // units/s^2 (vehicles want more than the walker's)
    float rideHeight = 0.35f;       // chassis origin above the contact plane
    float suspensionTravel = 0.18f;
    float suspensionRate = 8.0f;    // how fast compression follows the ground
    float maxSlopeCos = 0.5f;       // steeper than this and the wheels lose grip

    // Relative mass, used when the vehicle shoves an existing physics body.
    float mass = 12.0f;
};

// One tunable of a DriveSpec, with everything a serializer or a widget needs.
struct SpecField {
    const char* key;    // the .tyra JSON key - never rename one, it is format
    float* value;
    float min, max;
    const char* label;  // what the editor calls it
    const char* tip;    // one line, the panel's tooltip
};

// THE field list, and the only one. The .tyra writer, the reader, the Vehicle
// Editor's widget table and its tooltips all walk this, so a tunable added to
// DriveSpec is saved, loaded, editable and documented by appearing here once -
// and a field added to the struct and NOT here is silently never saved, which
// is the trap dronegen::visitParams exists to have already paid for.
std::vector<SpecField> specFields(DriveSpec& s);

// Everything a controller may say to a vehicle in one frame. The player
// controller and (later) the AI controller both fill exactly this.
struct DriveInput {
    float throttle = 0.0f;  // -1..1; negative reverses
    float brake = 0.0f;     // 0..1
    float steer = 0.0f;     // -1..1, positive = right
    bool handbrake = false;
};

struct DriveState {
    float pos[3] = {0.0f, 0.0f, 0.0f};  // chassis origin (between the axles, at ride height)
    float yaw = 0.0f;                   // degrees, around the up axis
    float pitch = 0.0f;                 // degrees, from the contact plane (visual)
    float roll = 0.0f;                  // degrees, from the contact plane (visual)
    float speed = 0.0f;                 // along forward, units/s (signed)
    float lateral = 0.0f;               // sideways, units/s (signed, + = right)
    float velY = 0.0f;                  // vertical, units/s
    float steerAngle = 0.0f;            // degrees, the wheels' actual angle
    bool grounded = false;

    // Presentation only - derived from the sim, costing it nothing.
    float wheelSpin[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // degrees, accumulated
    // 0..1 of suspensionTravel, 0.5 = neutral. It starts NEUTRAL, not at 0:
    // zero means fully extended, so a default-constructed vehicle spent its
    // first half second visibly settling onto its springs for no reason - and
    // that transient is large enough to hide any real suspension event a test
    // is looking for.
    float wheelCompress[4] = {0.5f, 0.5f, 0.5f, 0.5f};
};

// Ground height under a world XZ. Returns a very low finite value where there
// is no ground at all - the generated game's TERRAIN_VOID_Y convention, which
// is what lets "no terrain in this scene" need no branch of its own here.
using HeightFn = std::function<float(float x, float z)>;

// Is there something solid at this point (a wall, a pillar, another car)?
// `feetY` is the chassis underside, so a caller can ignore what the car
// clears. Optional: an empty function is open ground everywhere, which is
// what the harness and any caller that only cares about handling want.
using SolidFn = std::function<bool(float x, float z, float feetY)>;

// Advances one vehicle by `dt` seconds. `dt` is clamped internally so a paused
// editor or a stalled frame cannot tunnel the car through the world.
//
// Wall collision is the same rule the generated runtime runs (its resolver is
// collidePlayer, this one is the caller's `solid` - approximate on the editor
// side, but the same four corners and the same refusal): any corner landing in
// something solid refuses the WHOLE move and takes most of the speed, because
// resolving per corner would rotate a body a kinematic chassis cannot rotate.
void step(const DriveSpec& spec, const DriveInput& in, float dt,
          const HeightFn& height, DriveState& state, const SolidFn& solid = {});

// The four wheel anchors in WORLD space for the current state, in the same
// order as Detection::wheels. The viewport preview and the generated runtime
// both place their wheel geometry with this, so neither can invent a position
// the other does not use.
void wheelAnchors(const DriveSpec& spec, const DriveState& state, float out[4][3]);

}  // namespace vehiclesim
