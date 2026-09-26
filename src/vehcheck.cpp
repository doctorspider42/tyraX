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
#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

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
    // A wall at 45 degrees to the world axes. The old resolver slid along
    // world X or Z only, so a diagonal wall gave a stair-step or a dead stop;
    // the normal now comes from the blocked points, whatever the wall's angle.
    {
        auto diag = [](float x, float z, float) { return x + z > 14.0f; };
        DriveSpec s;
        DriveState st;
        st.pos[1] = s.rideHeight;
        st.yaw = 15.0f;  // 30 degrees off the wall's own direction
        DriveInput in;
        in.throttle = 1.0f;
        bool touched = false;
        float along0 = 0.0f;
        for (int i = 0; i < 400; ++i) {
            step(s, in, 1.0f / 50.0f, flat, st, diag);
            if (!touched && st.pos[0] + st.pos[2] > 12.0f) {
                touched = true;
                along0 = st.pos[2] - st.pos[0];
            }
        }
        const float along = (st.pos[2] - st.pos[0]) - along0;
        std::printf("  diagonal wall: slid %.1f along it at end speed %.2f, "
                    "depth %.2f\n", along * 0.7071f, st.speed,
                    (st.pos[0] + st.pos[2]) * 0.7071f);
        verdict(touched && along * 0.7071f > 20.0f && st.speed > 5.0f &&
                    st.pos[0] + st.pos[2] < 14.0f,
                "a diagonal wall slides the car along it, no stair-step");
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
    // A WEDGE (1.136.1): driven into the inside of a corner, both the move
    // and its redirect along either wall are refused. The car used to keep
    // the redirect's velocity anyway - 33.7 u/s on the console standing
    // still, which also hid it from the AI's unstick rule (it reads speed).
    {
        auto corner = [](float x, float z, float) { return z > 10.0f || x > 10.0f; };
        DriveSpec s;
        float worst = 0.0f, worstYaw = 0.0f;
        for (float yaw = 20.0f; yaw <= 70.0f; yaw += 5.0f) {
            DriveState st;
            st.pos[1] = s.rideHeight;
            st.yaw = yaw;
            DriveInput in;
            in.throttle = 1.0f;
            float moved = 0.0f, prevX = 0.0f, prevZ = 0.0f;
            for (int i = 0; i < 500; ++i) {
                step(s, in, 1.0f / 50.0f, flat, st, corner);
                // Standing still (under a unit in the last 4 s) with speed
                // on the clock is the failure; sliding out along a wall is not.
                if (i == 300) prevX = st.pos[0], prevZ = st.pos[2];
            }
            moved = std::hypot(st.pos[0] - prevX, st.pos[2] - prevZ);
            if (moved < 1.0f && std::fabs(st.speed) > worst)
                worst = std::fabs(st.speed), worstYaw = yaw;
        }
        std::printf("  wedge: worst speed while standing still %.2f u/s (yaw %.0f)\n",
                    worst, worstYaw);
        verdict(worst < 1.0f, "a car wedged in a corner stands still, it does not spin up");
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
    // A fast car on a slow frame: 90 u/s at 20 fps is 4.5 units a step, more
    // than the car is long, so a 0.3-unit wall between two frames used to be
    // jumped whole. The swept step stops it.
    {
        auto thin = [](float, float z, float) { return z > 20.0f && z < 20.3f; };
        DriveSpec s;
        s.topSpeed = 120.0f;
        DriveState st;
        st.pos[1] = s.rideHeight;
        st.pos[2] = 12.0f;
        st.speed = 90.0f;
        DriveInput in;
        in.throttle = 1.0f;
        float worstZ = -1e9f;
        for (int i = 0; i < 40; ++i) {
            step(s, in, 1.0f / 20.0f, flat, st, thin);
            worstZ = std::max(worstZ, st.pos[2]);
        }
        std::printf("  swept: deepest centre z %.2f (wall at 20.00-20.30)\n", worstZ);
        verdict(worstZ < 20.0f, "a fast car on a slow frame cannot jump a thin wall");
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

// 8. THE ANALYTIC FOUR-WHEEL RIG: wheel hardpoints are rigid children of the
//    full chassis attitude, suspension moves along chassis-up rather than
//    world Y, and the body footprint cannot pass through a crest beyond the
//    axle lines. These are geometry properties; no skeleton or IK is involved.
void analyticWheelRig() {
    std::printf("-- analytic four-wheel rig --\n");
    DriveSpec s;
    s.wheelBase = 2.4f;
    s.track = 1.5f;
    s.suspensionTravel = 0.4f;
    DriveState st;
    st.pos[0] = 3.0f;
    st.pos[1] = 4.0f;
    st.pos[2] = 5.0f;
    st.pitch = 18.0f;
    st.yaw = 37.0f;
    st.roll = -12.0f;
    float neutral[4][3], compressed[4][3];
    wheelAnchors(s, st, neutral);
    for (float& c : st.wheelCompress) c = 0.8f;
    wheelAnchors(s, st, compressed);
    auto dist = [](const float a[3], const float b[3]) {
        const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
        return std::sqrt(x * x + y * y + z * z);
    };
    const float frontTrack = dist(neutral[0], neutral[1]);
    const float leftBase = dist(neutral[0], neutral[2]);
    float worstTravel = 0.0f;
    for (int i = 0; i < 4; ++i)
        worstTravel = std::max(
            worstTravel,
            std::fabs(dist(neutral[i], compressed[i]) - 0.6f * s.suspensionTravel));
    std::printf("  rigid track %.3f, base %.3f, suspension error %.6f\n",
                frontTrack, leftBase, worstTravel);
    verdict(std::fabs(frontTrack - s.track) < 1e-4f &&
                std::fabs(leftBase - s.wheelBase) < 1e-4f,
            "full-attitude hardpoints preserve track and wheelbase");
    verdict(std::fabs(neutral[0][1] - neutral[3][1]) > 0.1f,
            "pitch and roll move wheel centres vertically with their arches");
    verdict(worstTravel < 1e-4f,
            "suspension displacement follows chassis-up at authored travel");

    DriveSpec crestSpec;
    crestSpec.wheelBase = 2.4f;
    crestSpec.bodyOverhang = 0.8f;
    crestSpec.rideHeight = 0.5f;
    crestSpec.suspensionTravel = 0.3f;
    auto crest = [](float, float z) {
        return z > 1.8f && z < 2.2f ? 1.0f : 0.0f;
    };
    DriveState crestState;
    crestState.pos[1] = crestSpec.rideHeight;
    step(crestSpec, {}, 1.0f / 50.0f, crest, crestState);
    std::printf("  sharp-crest body y %.3f (clearance floor 1.355)\n",
                crestState.pos[1]);
    verdict(crestState.pos[1] >= 1.354f,
            "body overhang cannot pass through a crest beyond the axles");
}

void terrainStability() {
    std::printf("-- banks, frame spikes and missing contacts --\n");
    auto bank = [](float x, float z) { return 0.3f * x + 0.2f * z; };
    float worstPlaneError = 0.0f;
    for (float heading : {0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 270.0f}) {
        for (float dt : {0.008333333f, 0.02f, 0.04f, 0.05f}) {
            DriveSpec s;
            DriveState st;
            st.yaw = heading;
            st.pos[1] = s.rideHeight;
            for (int i = 0; i < 1000; ++i) {
                step(s, {}, dt, bank, st);
                // Restrain translation like a parked-car fixture, retain
                // gravity so initial clearance can settle back onto tyres.
                st.pos[0] = st.pos[2] = st.speed = st.lateral = 0.0f;
            }
            st.leanPitch = st.leanRoll = 0.0f;
            for (float& c : st.wheelCompress) c = 0.5f;
            float a[4][3];
            wheelAnchors(s, st, a);
            const float base = a[0][1] - bank(a[0][0], a[0][2]);
            for (int w = 1; w < 4; ++w)
                worstPlaneError = std::max(worstPlaneError,
                    std::fabs(a[w][1] - bank(a[w][0], a[w][2]) - base));
        }
    }
    std::printf("  bank hardpoint plane error %.6f across headings and 20..120 Hz\n", worstPlaneError);
    verdict(worstPlaneError < 0.002f,
            "chassis follows the same bank in every heading and frame rate");

    float worstRenderError = 0.0f;
    for (float heading : {0.0f, 45.0f, 89.999f, 90.0f, 180.0f, 270.0f}) {
        for (float roll : {-25.0f, 0.0f, 25.0f}) {
            DriveSpec spec;
            DriveState pose;
            pose.pitch = 17.0f;
            pose.roll = roll;
            pose.yaw = heading;
            float e[3], a[4][3];
            bodyRotation(pose.pitch, pose.yaw, pose.roll, e);
            wheelAnchors(spec, pose, a);
            // Independently apply the generic renderer's X, Y, Z order to
            // the front-left arch, including the Euler singular headings.
            float x = -spec.track * 0.5f, y = 0.0f, z = spec.wheelBase * 0.5f;
            constexpr float d = 3.14159265358979f / 180.0f;
            float n = y * std::cos(e[0]*d) - z * std::sin(e[0]*d);
            z = y * std::sin(e[0]*d) + z * std::cos(e[0]*d); y = n;
            n = x * std::cos(e[1]*d) + z * std::sin(e[1]*d);
            z = -x * std::sin(e[1]*d) + z * std::cos(e[1]*d); x = n;
            n = x * std::cos(e[2]*d) - y * std::sin(e[2]*d);
            y = x * std::sin(e[2]*d) + y * std::cos(e[2]*d); x = n;
            worstRenderError = std::max(worstRenderError,
                std::fabs(x-a[0][0]) + std::fabs(y-a[0][1]) + std::fabs(z-a[0][2]));
        }
    }
    verdict(worstRenderError < 0.0001f,
            "generic body renderer and local wheel rig agree at every heading");

    DriveSpec s;
    DriveState st;
    st.pos[1] = s.rideHeight;
    auto edge = [](float x, float) { return x > 0.0f ? -1e9f : 0.0f; };
    for (int i = 0; i < 100; ++i) step(s, {}, 0.02f, edge, st);
    verdict(st.grounded && std::fabs(st.pos[1] - s.rideHeight) < 0.01f,
            "two missing wheel samples do not poison the support plane");

    // A stationary bumper on a raised patch needs a clearance correction,
    // never a launch. The old floor derivative kicked the body upward.
    st = {};
    st.pos[1] = s.rideHeight;
    auto patch = [](float, float z) { return z > 1.2f ? 0.65f : 0.0f; };
    float upward = 0.0f;
    for (int i = 0; i < 400; ++i) {
        step(s, {}, i % 2 ? 0.008333333f : 0.05f, patch, st);
        upward = std::max(upward, st.velY);
    }
    std::printf("  clearance-only upward speed %.6f\n", upward);
    verdict(upward < 0.01f, "body clearance cannot manufacture launch velocity");
}

// 10. Damage (docs/vehicles.md, "Damage"): the switch is honest (strength 0
//     means nothing ever changes), a head-on dents and a glancing grind does
//     not, a damaged car is slower, and the dent itself cannot tear the mesh
//     or push a vertex past its limit.
void damage() {
    std::printf("-- damage --\n");
    auto wall = [](float, float z, float) { return z > 10.0f; };
    auto headOn = [&](float strength, float yaw) {
        DriveSpec s;
        s.damage = strength;
        DriveState st;
        st.pos[1] = s.rideHeight;
        st.yaw = yaw;
        DriveInput in;
        in.throttle = 1.0f;
        for (int i = 0; i < 600; ++i) step(s, in, 1.0f / 50.0f, flat, st, wall);
        return st;
    };
    const DriveState off = headOn(0.0f, 0.0f);
    verdict(off.damage == 0.0f && off.impactSerial == 0,
            "damage 0: a crash changes nothing (every old definition)");
    const DriveState hit = headOn(1.0f, 0.0f);
    std::printf("  head-on: damage %.2f after %d hit(s)\n", hit.damage, hit.impactSerial);
    verdict(hit.impactSerial >= 1 && hit.damage > 0.05f, "a head-on at speed dents");
    const DriveState graze = headOn(1.0f, 60.0f);
    std::printf("  glancing: damage %.3f after %d hit(s)\n", graze.damage,
                graze.impactSerial);
    verdict(graze.damage < hit.damage, "a glancing grind hurts less than a head-on");

    // A wrecked car is slower on open ground by exactly the power loss.
    {
        DriveSpec s;
        s.damage = 1.0f;
        DriveState a, b;
        a.pos[1] = b.pos[1] = s.rideHeight;
        b.damage = 1.0f;
        DriveInput in;
        in.throttle = 1.0f;
        for (int i = 0; i < 1500; ++i) {
            step(s, in, 1.0f / 50.0f, flat, a);
            step(s, in, 1.0f / 50.0f, flat, b);
        }
        std::printf("  top speed: pristine %.2f, wrecked %.2f\n", a.speed, b.speed);
        verdict(b.speed < a.speed * (1.0f - 0.8f * s.damagePerfLoss),
                "a wrecked car loses its authored share of performance");
    }

    // The dent: a front hit, applied three times over a grid of rest vertices
    // in which every position appears TWICE (a welded seam).
    {
        DriveSpec s;
        s.damage = 1.0f;
        const float bmin[3] = {-0.9f, 0.0f, -2.0f}, bmax[3] = {0.9f, 1.2f, 2.0f};
        Impact im;
        float add = 0.0f;
        const bool ok = impactFromDelta(s, 0.0f, 0.0f, -20.0f, bmin, bmax, 1.0f, im, &add);
        verdict(ok && im.dir[2] > 0.99f && std::fabs(im.point[2] - 2.0f) < 1e-3f,
                "a push backwards dents the FRONT of the body");
        std::vector<float> rest, pos;
        for (int iz = 0; iz <= 20; ++iz)
            for (int iy = 0; iy <= 6; ++iy)
                for (int ix = 0; ix <= 9; ++ix)
                    for (int twin = 0; twin < 2; ++twin) {
                        rest.push_back(-0.9f + 0.2f * ix);
                        rest.push_back(0.2f * iy);
                        rest.push_back(-2.0f + 0.2f * iz);
                    }
        pos = rest;
        const int n = (int)rest.size() / 3;
        int moved = 0;
        for (int k = 0; k < 3; ++k)
            moved = applyDent(im, s.damageMaxDent, rest.data(), 3, pos.data(), 3, n, nullptr);
        float worst = 0.0f, split = 0.0f, rearMove = 0.0f;
        for (int v = 0; v < n; ++v) {
            float d2 = 0.0f;
            for (int a = 0; a < 3; ++a) {
                const float d = pos[v * 3 + a] - rest[v * 3 + a];
                d2 += d * d;
            }
            worst = std::max(worst, std::sqrt(d2));
            if (rest[v * 3 + 2] < 0.0f) rearMove = std::max(rearMove, std::sqrt(d2));
            if (v & 1)
                for (int a = 0; a < 3; ++a)
                    split = std::max(split, std::fabs(pos[v * 3 + a] - pos[(v - 1) * 3 + a]));
        }
        std::printf("  dent: %d vertices moved, deepest %.3f (limit %.3f)\n", moved,
                    worst, s.damageMaxDent);
        verdict(moved > 0 && worst <= s.damageMaxDent + 1e-5f,
                "no vertex moves past the deepest-dent limit");
        verdict(split == 0.0f, "welded corners move together (the mesh cannot tear)");
        verdict(rearMove == 0.0f, "a front hit leaves the rear untouched");
    }
}

// 11. Loose panels and glass: the classifier finds the obvious pieces on a
//     box-shaped body, and the break rule honours side, strength and switch.
void pieces() {
    std::printf("-- loose pieces --\n");
    const float bmin[3] = {-0.9f, -0.3f, -2.2f}, bmax[3] = {0.9f, 1.1f, 2.2f};
    auto kindOf = std::function<int(float, float, float, int, bool)>();
    // A small quad-ish triangle at a point with a given outward axis.
    kindOf = [&](float x, float y, float z, int axis, bool glass) {
        float a[3] = {x, y, z}, b[3] = {x, y, z}, c[3] = {x, y, z};
        const int u = (axis + 1) % 3, w = (axis + 2) % 3;
        b[u] += 0.1f;
        c[w] += 0.1f;
        return classifyTriangle(a, b, c, glass, bmin, bmax);
    };
    verdict(kindOf(0.0f, 0.8f, 1.8f, 1, false) == PieceHood, "a top face at the front is the bonnet");
    verdict(kindOf(0.0f, 0.8f, -1.9f, 1, false) == PieceTrunk, "a top face at the back is the boot");
    verdict(kindOf(-0.9f, 0.4f, 0.0f, 0, false) == PieceDoorL, "the left flank amidships is the left door");
    verdict(kindOf(0.9f, 0.4f, 0.0f, 0, false) == PieceDoorR, "the right flank amidships is the right door");
    verdict(kindOf(0.0f, 0.8f, 0.0f, 1, false) == PieceBody, "the roof stays on");
    verdict(kindOf(0.0f, 0.9f, 1.0f, 2, true) == PieceWindscreen, "glass facing forward is the windscreen");
    verdict(kindOf(-0.8f, 0.9f, 0.0f, 0, true) == PieceWindowL, "glass on the left is a left window");

    DriveSpec s;
    s.damage = 1.0f;
    Impact front;
    front.point[0] = 0.0f, front.point[1] = 0.3f, front.point[2] = 2.2f;
    front.dir[0] = 0.0f, front.dir[1] = 0.0f, front.dir[2] = 1.0f;
    front.radius = 1.1f;
    const float hood[6] = {-0.8f, 0.6f, 1.2f, 0.8f, 0.9f, 2.2f};
    const float doorL[6] = {-0.9f, 0.0f, -0.6f, -0.8f, 0.8f, 0.8f};
    float hp = 0.0f;
    verdict(!pieceTakesHit(s, PieceHood, front, 8.0f, hood, hood + 3, hp) &&
                pieceTakesHit(s, PieceHood, front, 11.0f, hood, hood + 3, hp),
            "a bonnet soaks up hits and comes off on the second");
    hp = 0.0f;
    verdict(pieceTakesHit(s, PieceHood, front, 13.0f, hood, hood + 3, hp),
            "one big hit tears a bonnet off at once");
    hp = 0.0f;
    verdict(!pieceTakesHit(s, PieceDoorL, front, 30.0f, doorL, doorL + 3, hp),
            "a head-on never takes a door off");
    DriveSpec off = s;
    off.damageLoose = 0.0f;
    hp = 0.0f;
    verdict(!pieceTakesHit(off, PieceHood, front, 40.0f, hood, hood + 3, hp),
            "Loose parts 0: nothing comes off");
}

// Handling: the body turns no faster than grip allows, and a car above its
// top speed coasts down instead of being clamped in one frame.
void handling() {
    DriveSpec s;
    auto flat = [](float, float) { return 0.0f; };
    // Full lock at top speed, the digital-steering case. The body's yaw rate
    // times its speed is the lateral acceleration it demands.
    DriveState st;
    st.pos[1] = s.rideHeight;
    st.speed = s.topSpeed;
    DriveInput in;
    in.throttle = 1.0f;
    in.steer = 1.0f;
    float worstDemand = 0.0f, worstSlip = 0.0f;
    float prevYaw = st.yaw;
    for (int i = 0; i < 150; ++i) {
        step(s, in, 1.0f / 50.0f, flat, st);
        const float yawRate = (st.yaw - prevYaw) * (3.14159265f / 180.0f) * 50.0f;
        prevYaw = st.yaw;
        worstDemand = std::max(worstDemand, std::fabs(yawRate * st.speed));
        worstSlip = std::max(worstSlip, std::fabs(st.lateral));
    }
    std::printf("  full lock at top speed: demand %.1f u/s^2 (grip %.1f), "
                "worst slip %.2f u/s\n", worstDemand, s.grip, worstSlip);
    verdict(worstDemand <= s.grip + 0.5f,
            "full lock at speed asks no more of the tyres than the grip cap");
    verdict(worstSlip < 1.0f, "full lock at speed pushes wide, it does not spin");

    // Above the cap (nitrous just ended): one frame of throttle costs drag,
    // not the whole excess.
    st = {};
    st.pos[1] = s.rideHeight;
    st.speed = s.topSpeed * 1.2f;
    in = {};
    in.throttle = 1.0f;
    const float before = st.speed;
    step(s, in, 1.0f / 50.0f, flat, st);
    std::printf("  above top speed, one throttle frame: %.2f -> %.2f u/s\n",
                before, st.speed);
    verdict(before - st.speed < 0.5f,
            "a car above its top speed coasts down, it is not clamped");

    // Handbrake flick: the rear steps out (the body rotates past its path),
    // then grip comes back over kHandbrakeRecover instead of in one frame.
    st = {};
    st.pos[1] = s.rideHeight;
    st.speed = 20.0f;
    in = {};
    in.throttle = 0.6f;
    in.steer = 1.0f;
    in.handbrake = true;
    const float yaw0 = st.yaw;
    for (int i = 0; i < 40; ++i) step(s, in, 1.0f / 50.0f, flat, st);
    const float turned = std::fabs(st.yaw - yaw0);
    const float slipHeld = std::fabs(st.lateral);
    in.handbrake = false;
    const float lat0 = std::fabs(st.lateral);
    step(s, in, 1.0f / 50.0f, flat, st);
    const float firstDrop = lat0 - std::fabs(st.lateral);
    std::printf("  handbrake flick: turned %.1f deg in 0.8 s, slip %.2f u/s, "
                "first frame after release sheds %.2f u/s (full grip %.2f)\n",
                turned, slipHeld, firstDrop, s.grip / 50.0f);
    verdict(turned > 40.0f && slipHeld > 3.0f,
            "a handbrake flick rotates the car into a drift");
    verdict(firstDrop < 0.5f * s.grip / 50.0f,
            "grip returns gradually after the handbrake, the drift does not snap");
}

// Off-road grip (1.136.0): the same drive on the road, on the grass, and
// straddling the edge.
void offroad() {
    auto flat = [](float, float) { return 0.0f; };
    const SurfaceFn allPaved = [](float, float) { return 1.0f; };
    const SurfaceFn noneRoad = [](float, float) { return -1.0f; };

    // The run the rest of the checks share: full throttle from rest, then a
    // full-lock corner at speed. Returns the speed after 3 s and the worst
    // lateral demand in the corner.
    auto drive = [&](const DriveSpec& s, const SurfaceFn& paved, float* speed3,
                     float* demand) {
        DriveState st;
        st.pos[0] = 50.0f;  // the half-paved case splits the track at x = 50
        st.pos[1] = s.rideHeight;
        DriveInput in;
        in.throttle = 1.0f;
        for (int i = 0; i < 150; ++i) step(s, in, 1.0f / 50.0f, flat, st, {}, 1.0f, paved);
        *speed3 = st.speed;
        in.steer = 1.0f;
        float prevYaw = st.yaw, worst = 0.0f;
        for (int i = 0; i < 100; ++i) {
            step(s, in, 1.0f / 50.0f, flat, st, {}, 1.0f, paved);
            const float yr = (st.yaw - prevYaw) * (3.14159265f / 180.0f) * 50.0f;
            prevYaw = st.yaw;
            worst = std::max(worst, std::fabs(yr * st.speed));
        }
        *demand = worst;
    };

    // A default car does not know the surface exists: every earlier car keeps
    // driving exactly as it did.
    DriveSpec base;
    float v0, d0, v1, d1;
    drive(base, allPaved, &v0, &d0);
    drive(base, noneRoad, &v1, &d1);
    std::printf("  default spec, paved vs grass: %.3f/%.3f u/s, %.3f/%.3f u/s^2\n",
                v0, v1, d0, d1);
    verdict(v0 == v1 && d0 == d1, "a default definition ignores the surface");

    DriveSpec rally = base;
    rally.offroadGrip = 0.5f;
    rally.offroadAccel = 0.7f;
    rally.offroadDrag = 3.0f;
    float vr, dr, vp, dp;
    drive(rally, noneRoad, &vr, &dr);
    drive(rally, allPaved, &vp, &dp);
    std::printf("  offroad 0.5/0.7/3: grass %.1f u/s after 3 s, corner %.1f u/s^2 "
                "(paved %.1f, %.1f; grip %.1f)\n", vr, dr, vp, dp, rally.grip);
    verdict(vr < vp - 1.0f, "off the road the car accelerates slower");
    verdict(dr <= 0.5f * rally.grip + 0.5f && dr < dp - 1.0f,
            "off the road the tyres hold half the corner");
    verdict(vp == v0 && dp == d0, "on the road the off-road fields change nothing");

    // Two wheels on each side: half the effect, not all or nothing.
    float vh, dh;
    drive(rally, [](float x, float) { return x < 50.0f ? 1.0f : -1.0f; }, &vh, &dh);
    std::printf("  half on the road: %.1f u/s after 3 s\n", vh);
    verdict(vh > vr + 0.2f && vh < vp - 0.2f,
            "a car half on the grass sits between the two surfaces");

    // Road grip (1.137.0): a gravel road at 0.5 halves the corner, and leaves
    // the acceleration alone - it is still a road, not the car's off-road.
    float vg, dg;
    drive(base, [](float, float) { return 0.5f; }, &vg, &dg);
    std::printf("  road grip 0.5: corner %.1f u/s^2 (asphalt %.1f), %.1f u/s after 3 s\n",
                dg, d0, vg);
    verdict(dg <= 0.5f * base.grip + 0.5f && dg < d0 - 1.0f,
            "a road's grip multiplier scales the tyres' hold");
    verdict(std::fabs(vg - v0) < 0.05f, "road grip leaves the acceleration alone");
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
    analyticWheelRig();
    terrainStability();
    handling();
    offroad();
    damage();
    pieces();
    if (failures) {
        std::printf("vehicle-check: %d FAILURE(S)\n", failures);
        return 1;
    }
    std::printf("vehicle-check: all properties hold\n");
    return 0;
}

}  // namespace vehcheck
