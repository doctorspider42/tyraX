// --vehicle-check: the drive model's property tests (docs/vehicles.md,
// "Verifying a drive without eyes").
//
// vehiclesim is the single source of truth for two consumers that must never
// disagree - the editor's test drive and the generated PS2 runtime - and this
// is the harness that keeps its PROPERTIES honest between hardware runs. It
// started as a scratchpad file and every failure it has caught is now a case
// here: the pre-powertrain regression (the gearbox must feed nothing back at
// the defaults), the gearbox hunting through its own shift cut, the head-on
// that ground in place at a phantom 5 u/s, and the kickdown that could not
// climb the hill it was written for.
//
// Host-only by construction (vehiclesim has no GL, no ImGui, no project.hpp),
// so this runs anywhere the editor compiles - CI included. Exit 0 = every
// property holds. What it CANNOT check is twin parity with the generated
// runtime (that is C++ inside a raw string in templates.cpp, compiled only by
// the PS2 toolchain) - the VEH telemetry on a real boot stays the check for
// that, and docs/vehicles.md says which lines to read.

#include <cmath>
#include <cstdio>

#include "vehiclesim.hpp"

namespace vehcheck {
namespace {

using namespace vehiclesim;

int failures = 0;

void verdict(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

float flat(float, float) { return 0.0f; }

// 1. The gear ratios are geometric, the top gear reaches the top speed, and
//    gearTorque is centred - the multipliers' geometric mean is 1, which is
//    what makes it a character knob rather than a power knob.
void gearGeometry() {
    std::printf("-- gear geometry --\n");
    DriveSpec s;
    verdict(std::fabs(gearTopSpeed(s, gearCount(s) - 1) - s.topSpeed) < 1e-3f,
            "top gear reaches topSpeed");
    bool mono = true;
    for (int g = 1; g < gearCount(s); ++g)
        if (gearTopSpeed(s, g) <= gearTopSpeed(s, g - 1)) mono = false;
    verdict(mono, "gear top speeds strictly increase");
    DriveSpec geared = s;
    geared.gearTorque = 1.0f;
    float prod = 1.0f;
    for (int g = 0; g < gearCount(geared); ++g) prod *= gearTorqueMul(geared, g);
    verdict(std::fabs(std::pow(prod, 1.0f / gearCount(geared)) - 1.0f) < 1e-3f,
            "gearTorque multipliers have geometric mean 1 (character, not power)");
    verdict(gearTorqueMul(s, 0) == 1.0f && gearTorqueMul(s, gearCount(s) - 1) == 1.0f,
            "gearTorque 0 is the identity in every gear");
}

// 2. THE REGRESSION PROPERTY: a default spec accelerates exactly as the
//    pre-powertrain model did - accel capped at topSpeed minus quadratic
//    drag, reproduced here independently. This is what "the gearbox is
//    derived, not simulated" MEANS, stated as arithmetic.
void preGearboxRegression() {
    std::printf("-- pre-powertrain regression --\n");
    DriveSpec s;
    DriveState st;
    st.pos[1] = s.rideHeight;
    DriveInput in;
    in.throttle = 1.0f;
    const float dt = 1.0f / 50.0f;
    float ref = 0.0f, worst = 0.0f;
    for (int i = 0; i < 700; ++i) {
        step(s, in, dt, flat, st);
        ref = std::min(ref + s.accel * dt, s.topSpeed);
        ref -= s.drag * ref * std::fabs(ref) * dt;
        worst = std::max(worst, std::fabs(ref - st.speed));
    }
    std::printf("  worst |speed - reference| = %.9f\n", worst);
    verdict(worst < 1e-4f, "default spec is bit-for-bit the pre-gearbox model");
}

// 3. The gearbox cannot hunt, even with contradictory authored thresholds -
//    the down-shift point is COMPUTED under where an up-shift lands.
void noHunting() {
    std::printf("-- anti-hunt --\n");
    DriveSpec s;
    s.shiftUpFrac = 0.55f;
    s.shiftDownFrac = 0.90f;  // asks for the impossible
    DriveState st;
    st.pos[1] = s.rideHeight;
    DriveInput in;
    in.throttle = 1.0f;
    int changes = 0, prev = 0;
    for (int i = 0; i < 700; ++i) {
        step(s, in, 1.0f / 50.0f, flat, st);
        if (st.gear != prev) ++changes;
        prev = st.gear;
    }
    std::printf("  %d gear change(s); a clean climb is %d\n", changes,
                gearCount(s) - 1);
    verdict(changes <= gearCount(s), "no hunting under contradictory thresholds");
}

// 4. Walls: a glancing hit GRINDS along the wall, a head-on STOPS - and does
//    not grind in place at a phantom speed, which is exactly how the first
//    slide implementation failed.
void walls() {
    std::printf("-- walls --\n");
    auto wall = [](float, float z, float) { return z > 10.0f; };
    {
        DriveSpec s;
        DriveState st;
        st.pos[1] = s.rideHeight;
        st.yaw = 60.0f;
        DriveInput in;
        in.throttle = 1.0f;
        float xAtTouch = 0.0f;
        bool touched = false;
        for (int i = 0; i < 600; ++i) {
            step(s, in, 1.0f / 50.0f, flat, st, wall);
            if (!touched && st.pos[2] > 8.5f) {
                touched = true;
                xAtTouch = st.pos[0];
            }
        }
        const float slid = st.pos[0] - xAtTouch;
        std::printf("  glancing: slid %.1f along the wall at end speed %.2f\n",
                    slid, st.speed);
        verdict(touched && slid > 20.0f && st.speed > 5.0f,
                "a glancing hit grinds instead of sticking");
    }
    {
        DriveSpec s;
        DriveState st;
        st.pos[1] = s.rideHeight;
        DriveInput in;
        in.throttle = 1.0f;
        for (int i = 0; i < 600; ++i)
            step(s, in, 1.0f / 50.0f, flat, st, wall);
        std::printf("  head-on: end speed %.2f at z %.2f\n", st.speed, st.pos[2]);
        verdict(st.speed < 3.0f && st.pos[2] < 10.0f,
                "a head-on stops at the wall (no phantom grind-in-place)");
    }
    // A pillar NARROWER than the corner spacing must still stop the car -
    // four corner samples alone let a pole pass between them and sit inside
    // the body, which is exactly how "wjechac w obiekt" was reported. The
    // edge midpoints are what catch it.
    {
        auto pillar = [](float x, float z, float) {
            return std::fabs(x) < 0.45f && z > 10.0f && z < 10.9f;
        };
        DriveSpec s;  // track 1.4: a 0.9-wide pillar fits between the corners
        DriveState st;
        st.pos[1] = s.rideHeight;
        DriveInput in;
        in.throttle = 1.0f;
        for (int i = 0; i < 600; ++i)
            step(s, in, 1.0f / 50.0f, flat, st, pillar);
        std::printf("  pillar: end z %.2f (pillar face at %.2f)\n", st.pos[2],
                    10.0f - 0.5f * s.wheelBase);
        verdict(st.pos[2] < 10.0f, "a pillar narrower than the track stops the car");
    }
    // A car that STARTS overlapping a wall (an old save, a spawn, a swept
    // corner) must never be trapped: reversing out has to work, and holding
    // the throttle INTO the wall must not push it any deeper.
    {
        DriveSpec s;
        DriveState st;
        st.pos[1] = s.rideHeight;
        st.pos[2] = 10.5f;  // nose corners past the z>10 face
        DriveInput in;
        in.throttle = 1.0f;
        float deepest = st.pos[2];
        for (int i = 0; i < 200; ++i) {
            step(s, in, 1.0f / 50.0f, flat, st, wall);
            deepest = std::max(deepest, st.pos[2]);
        }
        const float pushed = deepest - 10.5f;
        in.throttle = -1.0f;
        bool out = false;
        for (int i = 0; i < 400 && !out; ++i) {
            step(s, in, 1.0f / 50.0f, flat, st, wall);
            out = st.pos[2] + 0.5f * s.wheelBase < 10.0f;
        }
        std::printf("  overlapped: pushed %.2f deeper, reversed out: %s\n",
                    pushed, out ? "yes" : "no");
        verdict(pushed < 0.05f, "throttle into the wall gains no depth at all");
        verdict(out, "a car spawned inside a wall reverses out (never trapped)");
    }
    // A THIN wall must hold a car that grinds it while turning hard - the
    // tunneling case: with "no deeper" judged by an equal blocked count, the
    // nose's sample points left the far side exactly as the tail's entered,
    // and a car swept in by its own (unchecked) rotation drove clean through
    // the 1.5-thick arena wall on the example map.
    {
        auto slab = [](float, float z, float) {
            return z > 10.0f && z < 11.5f;
        };
        DriveSpec s;
        DriveState st;
        st.pos[1] = s.rideHeight;
        st.pos[2] = 8.0f;  // right against the slab, about to grind
        DriveInput in;
        in.throttle = 1.0f;
        float worstZ = st.pos[2];
        for (int i = 0; i < 1500; ++i) {
            in.steer = (i / 150) % 2 ? 1.0f : -1.0f;  // saw at the wall
            step(s, in, 1.0f / 50.0f, flat, st, slab);
            worstZ = std::max(worstZ, st.pos[2]);
        }
        std::printf("  thin wall: deepest centre z %.2f (far face at 11.50)\n",
                    worstZ);
        verdict(worstZ < 10.6f, "a thin wall cannot be tunneled by grinding");
    }
}

// 5. Weight transfer: bounded, settles at a cruise, and the roll flips with
//    the steer direction. The absolute sign is a screen question the harness
//    cannot ask - the screenshots in docs/vehicles.md are that check.
void lean() {
    std::printf("-- weight transfer --\n");
    DriveSpec s;
    s.gearTorque = 1.0f;
    DriveState st;
    st.pos[1] = s.rideHeight;
    DriveInput in;
    in.throttle = 1.0f;
    float squat = 0.0f;
    for (int i = 0; i < 300; ++i) {
        step(s, in, 1.0f / 50.0f, flat, st);
        squat = std::max(squat, st.leanPitch);
    }
    for (int i = 0; i < 200; ++i) step(s, in, 1.0f / 50.0f, flat, st);
    const float cruise = st.leanPitch;
    in.throttle = 0.0f;
    in.brake = 1.0f;
    float dive = 0.0f;
    for (int i = 0; i < 100; ++i) {
        step(s, in, 1.0f / 50.0f, flat, st);
        dive = std::min(dive, st.leanPitch);
    }
    in.brake = 0.0f;
    in.throttle = 1.0f;
    in.steer = 1.0f;
    for (int i = 0; i < 300; ++i) step(s, in, 1.0f / 50.0f, flat, st);
    const float rollA = st.leanRoll;
    in.steer = -1.0f;
    for (int i = 0; i < 300; ++i) step(s, in, 1.0f / 50.0f, flat, st);
    const float rollB = st.leanRoll;
    std::printf("  squat %.2f, cruise %.3f, dive %.2f, rolls %.2f / %.2f\n",
                squat, cruise, dive, rollA, rollB);
    verdict(squat > 0.3f && squat <= 4.01f, "squat within (0.3, 4]");
    verdict(std::fabs(cruise) < 0.35f, "lean settles at a cruise");
    verdict(dive < -0.5f && dive >= -4.01f, "brake dive within [-4, -0.5)");
    verdict(std::fabs(rollA) > 0.5f && std::fabs(rollA) <= 6.01f &&
                rollA * rollB < 0.0f,
            "corner roll bounded and flips with steer direction");
    // leanAmount 0 must kill the lean outright.
    DriveSpec kart = s;
    kart.leanAmount = 0.0f;
    DriveState k2;
    k2.pos[1] = kart.rideHeight;
    in.steer = 1.0f;
    float worst = 0.0f;
    for (int i = 0; i < 300; ++i) {
        step(kart, in, 1.0f / 50.0f, flat, k2);
        worst = std::max(worst, std::fabs(k2.leanRoll) + std::fabs(k2.leanPitch));
    }
    verdict(worst < 1e-3f, "leanAmount 0 is a kart on rails");
}

// 6. The hill: full throttle up a 15-degree grade in the harshest gearing
//    must KICKDOWN and keep climbing - and the kickdown must not hunt on the
//    FLAT, which is how its first landing margin failed (the shift cut itself
//    decays the speed, and the car crawled 170 units in 50 s on open ground).
void hill() {
    std::printf("-- hill kickdown --\n");
    DriveSpec s;
    s.gearTorque = 1.0f;
    s.shiftTime = 0.18f;
    auto ramp = [](float, float z) { return z > 0.0f ? z * 0.2679f : 0.0f; };
    DriveState st;
    st.pos[1] = s.rideHeight;
    st.pos[2] = -200.0f;
    DriveInput in;
    in.throttle = 1.0f;
    bool sawTop = false, kicked = false;
    int prev = 0;
    float minSpd = 1e9f;
    for (int i = 0; i < 2500; ++i) {
        step(s, in, 1.0f / 50.0f, ramp, st);
        if (st.pos[2] < 0.0f) {
            if (st.gear == gearCount(s) - 1) sawTop = true;
            continue;
        }
        if (st.gear < prev) kicked = true;
        prev = st.gear;
        minSpd = std::min(minSpd, st.speed);
    }
    std::printf("  top gear on the flat: %s, kicked down: %s, min %.2f u/s, "
                "end z %.0f\n",
                sawTop ? "yes" : "no", kicked ? "yes" : "no", minSpd, st.pos[2]);
    verdict(sawTop, "reaches top gear on the flat (no hunting)");
    verdict(kicked && minSpd > 3.0f && st.pos[2] > 150.0f,
            "kicks down on the grade and climbs");
}

// 7. THE SPRUNG RIG: full throttle across a field of sharp ridges must never
//    teleport the body - the per-frame height step stays bounded, the
//    attitude stays sane, and the car keeps making progress. This is the
//    property behind every "the car breaks apart on a bump" report: the old
//    snap-to-plane rig jumped the body half a unit in one frame wherever the
//    four-sample mean crossed a ridge.
void roughRide() {
    std::printf("-- sprung rig over ridges --\n");
    auto ridges = [](float, float z) {
        // Sharp triangular ridges, 0.8 tall every 9 units - a washboard.
        const float t = z / 9.0f - std::floor(z / 9.0f);
        return 0.8f * (t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f);
    };
    DriveSpec s;
    DriveState st;
    st.pos[1] = ridges(0, 0) + s.rideHeight;
    DriveInput in;
    in.throttle = 1.0f;
    float prevY = st.pos[1];
    float worstStep = 0.0f, worstPitch = 0.0f;
    for (int i = 0; i < 50 * 30; ++i) {
        step(s, in, 1.0f / 50.0f, ridges, st);
        worstStep = std::max(worstStep, std::fabs(st.pos[1] - prevY));
        prevY = st.pos[1];
        worstPitch = std::max(worstPitch,
                              std::max(std::fabs(st.pitch), std::fabs(st.roll)));
    }
    std::printf("  worst frame height step %.3f, worst attitude %.1f deg, "
                "end z %.0f\n",
                worstStep, worstPitch, st.pos[2]);
    verdict(worstStep < 0.30f, "the body never teleports over a ridge");
    verdict(worstPitch < 35.0f, "the attitude stays sane on a washboard");
    verdict(st.pos[2] > 350.0f, "the car keeps its pace across the ridges");
}

}  // namespace

int run() {
    std::printf("vehicle-check: vehiclesim property tests\n");
    gearGeometry();
    preGearboxRegression();
    noHunting();
    walls();
    lean();
    hill();
    roughRide();
    if (failures) {
        std::printf("vehicle-check: %d FAILURE(S)\n", failures);
        return 1;
    }
    std::printf("vehicle-check: all properties hold\n");
    return 0;
}

}  // namespace vehcheck
