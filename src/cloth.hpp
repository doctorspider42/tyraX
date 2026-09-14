#pragma once

#include <vector>

// Cloth / soft body simulation (docs/cloth.md) - the HOST half of a twin.
//
// Host-only: no GL, no ImGui, no project.hpp - the treegen/dronegen/placement
// shape, so the whole solver runs from a 40-line harness and the editor
// viewport and the generated PS2 runtime are two consumers of one definition.
// The generated game carries a numeric twin (templates.cpp, `updateCloths` in
// TPL_GAME_CPP_SCENE) written with Tyra::Vec4, i.e. COP2 macro mode on VU0 -
// change the arithmetic here and it has to move there too, or the editor
// preview stops predicting the console.
//
// WHY IT IS SHAPED LIKE THIS - the three properties everything else leans on.
//
// (1) ONE PARTICLE IS ONE QUADWORD. `V4` is x/y/z/w, 16 bytes, and every step
//     of the solver is whole-vector arithmetic on it: no per-component code
//     anywhere in the hot path, so each line maps to one VU instruction rather
//     than three scalar ones. `w` is deliberately unused by the math (it is
//     the inverse mass slot a future VU0 microprogram would fill).
//
// (2) CONSTRAINTS ARE SOLVED IN FOUR INDEPENDENT BATCHES. A distance
//     constraint writes both of its endpoints, so a naive relaxation pass is a
//     serial dependency chain - the one shape a vector unit is worst at. The
//     grid is therefore split red/black per axis: horizontal constraints whose
//     left column is even, then odd, then vertical with even top row, then
//     odd. No particle appears twice inside a batch, so a batch is
//     order-independent: it can be evaluated in any order, on any number of
//     lanes, by any unit. That is what makes moving this to a VU0 kernel a
//     mechanical change rather than a rewrite (docs/cloth.md, "Moving it onto
//     VU0").
//
// (3) THE STEP IS FIXED. Verlet integration with a varying dt changes the
//     effective stiffness every frame and blows up on the first hitch, so
//     `advance` accumulates real time and runs whole `kStepSeconds` steps,
//     bounded per call. A frozen frame therefore costs a bounded amount of
//     catch-up instead of teleporting the cloth.
namespace cloth {

// One particle. Same layout as Tyra::Vec4 / a VU quadword on purpose.
struct V4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

// The whole vocabulary the solver uses. Every one of these is a single
// vector operation - on the console each is one COP2 instruction pair, and in
// a VU0 microprogram one instruction - which is the property that makes the
// solver's line count and its instruction count the same order.
inline V4 v4add(const V4& a, const V4& b) {
    return V4{a.x + b.x, a.y + b.y, a.z + b.z, 0.0f};
}
inline V4 v4sub(const V4& a, const V4& b) {
    return V4{a.x - b.x, a.y - b.y, a.z - b.z, 0.0f};
}
inline V4 v4scale(const V4& a, float k) {
    return V4{a.x * k, a.y * k, a.z * k, 0.0f};
}
// a + b * k - the fused form; the VU has it as one madd.
inline V4 v4madd(const V4& a, const V4& b, float k) {
    return V4{a.x + b.x * k, a.y + b.y * k, a.z + b.z * k, 0.0f};
}
inline float v4dot3(const V4& a, const V4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V4 v4normalize(const V4& a);

// The simulation's fixed tick. 60 Hz, and the rate is chosen rather than
// inherited: it is the FASTEST display this engine runs at, which is what
// makes it the only value at which no frame goes unsimulated in either region.
//
// It was 1/50 - the PAL field rate - and that is a real defect on NTSC, not a
// rounding detail. The accumulator keeps the SPEED right either way (60 frames
// of 1/60 s is exactly 50 steps, so a curtain swings at the same rate in both
// regions - measured), but at 1/50 the step pattern over six NTSC frames is
// 0,1,1,1,1,1: one frame in six runs no step, the mesh is not rebuilt, and the
// previous pose is displayed twice. The cloth animates at 50 Hz on a 60 Hz
// screen while everything around it moves at 60.
//
// At 1/60 neither region ever skips: NTSC runs exactly one step per frame, PAL
// runs 1,1,1,1,2. The price is 20 % more solver steps per second in PAL
// (measured: CLOTH 1.41 -> 1.69 ms on the three-sheet example), and it buys a
// slightly better simulation as well as the smoothness, since a shorter step
// stretches less.
//
// It is a CONSTANT and not `1 / refreshRate` on purpose: deriving it from the
// display would make damping (per step) and stiffness (sweeps per second)
// region-dependent, so the same curtain would settle faster on an NTSC
// console. A constant keeps the authored look portable.
inline constexpr float kStepSeconds = 1.0f / 60.0f;
// Catch-up bound per advance() call. Four steps = 67 ms of stall absorbed (a
// 15 FPS frame); past that the cloth runs in slow motion for a frame rather
// than exploding.
inline constexpr int kMaxStepsPerAdvance = 4;

// Grid caps. The cost is particles for the integrate pass and ~2x that in
// constraints per iteration, all of it on the EE, so the ceiling is a budget
// decision rather than a data-structure one. 32x32 = 1024 particles is already
// far past what a PS2 frame wants; the editor offers much less.
inline constexpr int kMinSide = 2;
inline constexpr int kMaxSide = 32;

// Which particles are nailed in place. The values are serialized and baked
// into the generated scene table, so this list is APPEND-ONLY.
enum class Pin {
    None = 0,       // free-falling sheet (a flag torn loose, a dropped rag)
    TopEdge = 1,    // a curtain on a rail - the whole top row
    TopCorners = 2, // a banner on two hooks
    TopBottom = 3,  // laced top and bottom - a tarpaulin
    LeftEdge = 4,   // a flag on a pole down the left side
    Corners = 5,    // all four corners - a hammock / trampoline sheet
};

struct Params {
    int cols = 9;  // particles across the sheet's local +X
    int rows = 7;  // particles down the sheet's local -Y
    // Rest spacing between neighbours, world units. Taken from the object's
    // scale at build/preview time, so a stretched curtain has a coarser grid
    // rather than a pre-stressed one.
    float restX = 0.25f;
    float restY = 0.25f;
    int iterations = 2;      // relaxation sweeps per step (1..4)
    float damping = 0.03f;   // fraction of the Verlet velocity dropped per step
    float gravity = 9.8f;    // world units/s^2, along -Y
    // The gust, as an ACCELERATION VECTOR rather than an amplitude and a
    // bearing. It is a vector because wind is a SUM now: the sheet's own
    // draught plus every Wind entity that reaches it (docs/cloth.md, "Wind
    // entities"), resolved once per sheet per frame by the caller. Zero = still
    // air, and the gust envelope is skipped entirely.
    V4 windAccel{0.0f, 0.0f, 0.0f, 0.0f};
    Pin pin = Pin::TopEdge;
};

// A collider the cloth is pushed out of: a CAPSULE, the segment a..b swept by
// radius r. One sphere was the first cut and it pushed a waist-high bulge
// through a curtain while the hem and the top stayed put; three stacked
// spheres fixed the look and introduced a new fault - pushing a particle out
// of one can push it into the next, so a single pass left particles inside the
// body. A capsule is ONE test, has no seams to be pushed into, and is cheaper
// than the three it replaces. See docs/cloth.md, "What the player is".
struct Capsule {
    V4 a, b;
    float r = 0.5f;
};

// The player, as the cloth sees them: a capsule from these fractions of their
// own eye height - ankles to just over the head. Twin of CLOTH_BODY_LO/HI in
// the generated game.
inline constexpr float kBodyLo = 0.08f;
inline constexpr float kBodyHi = 1.00f;

// A WIND ENTITY (docs/cloth.md, "Wind entities"): a placed source that blows
// every sheet within its reach, instead of every sheet carrying its own
// draught. `dir` is a unit vector - the object's rotated local +Z - and
// `radius` 0 means the whole scene.
struct Wind {
    V4 pos;
    V4 dir;
    float strength = 0.0f;  // units/s^2 at the source
    float radius = 0.0f;    // 0 = unlimited reach, no falloff at all
};

// The wind acceleration a point feels: every source summed, each scaled by
// `1 - d^2/r^2` clamped at zero. That is the engine's own point-light falloff
// (see the dynamic lights in vendor/tyra) rather than a new curve, so a fan
// reaches the way a lamp lights.
//
// SAMPLED ONCE PER SHEET, at its origin, not per particle. A sheet is small
// against the distances a wind source works over, so per-particle sampling
// would cost the whole grid a subtract, a dot and a compare per source to
// produce a gradient nobody can see. Twin of clothWindAt in the generated
// game.
V4 windAt(const Wind* sources, int count, const V4& point);

struct State {
    std::vector<V4> pos;   // current positions, world space
    std::vector<V4> prev;  // previous positions - Verlet's velocity carrier
    std::vector<unsigned char> pinned;  // 1 = never integrated, never pushed
    // Per-ROW gust phase, baked by reset(): sin/cos of the row's own offset, so
    // the per-step wind needs ONE sinf/cosf pair for the whole sheet and the
    // rows still ripple out of phase (angle addition, not a sinf per row).
    std::vector<float> rowSin, rowCos;
    // Per-particle smoothed normals. Scratch owned by the state rather than
    // allocated per frame: buildMesh() refills it, nothing else reads it.
    std::vector<V4> nrm;
    float time = 0.0f;      // simulated seconds - the gust phase
    float carry = 0.0f;     // unconsumed real time, < kStepSeconds
};

// Vertex the mesh builder emits. One triangle list, no indices - both
// consumers (the editor's GL preview, the generated game's StaPipBag) want a
// flat array, and the PS2 side has no index buffer at all.
struct Vertex {
    V4 pos;
    V4 nrm;
    float u = 0.0f, v = 0.0f;
};

inline int clampSide(int n) {
    return n < kMinSide ? kMinSide : (n > kMaxSide ? kMaxSide : n);
}
inline int particleCount(const Params& p) {
    return clampSide(p.cols) * clampSide(p.rows);
}
// Triangles the sheet tessellates to - the Properties readout and the codegen
// budget line both ask this rather than re-deriving it.
inline int triangleCount(const Params& p) {
    return (clampSide(p.cols) - 1) * (clampSide(p.rows) - 1) * 2;
}

// Lay the sheet out flat and at rest. `origin` is the grid's (0,0) corner;
// `right` and `down` are UNIT vectors spanning it - the caller supplies the
// object's rotated local +X and -Y, so the cloth starts in exactly the plane
// the editor draws the object's quad in. Clears velocity (prev == pos), so a
// reset sheet hangs still for one step and then falls.
void reset(const Params& p, const V4& origin, const V4& right, const V4& down,
           State& s);

// Re-place the PINNED particles on a (possibly moved) rest frame. Called
// before every step by both twins, which is what lets a curtain be carried:
// move or rotate the object and its rail goes with it while the free part
// keeps swinging behind. Free particles are never touched - they follow
// through the constraints.
void anchor(const Params& p, const V4& origin, const V4& right, const V4& down,
            State& s);

// ONE fixed step. Integrate, relax, collide, in that order. The caller has
// already anchored this step's rest frame.
void step(const Params& p, const Capsule* bodies, int bodyCount, State& s);

// Real time in, whole steps out. Returns how many steps ran (0 when the frame
// was shorter than one tick). A negative or absurd dt is clamped rather than
// trusted - the editor's clock stalls behind modal dialogs and window drags.
int advance(const Params& p, const V4& origin, const V4& right, const V4& down,
            float dt, const Capsule* bodies, int bodyCount, State& s);

// Triangles for the current state, two per cell, wound CCW seen from the
// sheet's front (the side its local +Z faced at rest). Normals are the cell's
// own cross product - the PS2 draws this unlit and flat-shaded, and the
// preview shades per vertex, so both want the same normal per corner.
// KEEP IN SYNC with `buildClothMesh` in templates.cpp: the emission ORDER is
// what pairs an editor vertex with a console one. Refills State::nrm as
// scratch, which is why the state is not const.
void buildMesh(const Params& p, State& s, std::vector<Vertex>& out);

}  // namespace cloth
