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

#include <algorithm>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "project.hpp"
#include "roadbridge.hpp"
#include "roadfurniture.hpp"
#include "roadstream.hpp"
#include "roadfile.hpp"
#include "pngquant.hpp"
#include "roaddetail.hpp"
#include "roadgen.hpp"
#include "roadrail.hpp"
#include "roadtex.hpp"
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
    s.nosTopSpeed = 1.18f;
    s.nosCapacity = 4.0f;
    const int topGear = gearCount(s) - 1;
    verdict(rpmFor(s, s.topSpeed * 1.10f, topGear) > s.redlineRpm &&
                rpmFor(s, s.topSpeed * 1.18f, topGear) >
                    rpmFor(s, s.topSpeed * 1.10f, topGear),
            "nitrous speed past V max continues raising final-gear RPM");
    verdict(rpmFor(s, s.topSpeed * 1.5f, topGear) <=
                s.redlineRpm * s.nosTopSpeed + 0.01f &&
                rpmFor(s, s.topSpeed, topGear - 1) <= s.redlineRpm,
            "over-rev is bounded and lower gears retain their shift point");
    s.nosCapacity = 0.0f;
    verdict(rpmFor(s, s.topSpeed * 1.1f, topGear) <= s.redlineRpm,
            "a car without nitrous keeps the ordinary redline cap");
}

// 2. THE REGRESSION PROPERTY: with power fade disabled, a spec accelerates as the
//    pre-powertrain model did - accel capped at topSpeed minus quadratic
//    drag, reproduced here independently. This is what "the gearbox is
//    derived, not simulated" MEANS, stated as arithmetic.
void preGearboxRegression() {
    std::printf("-- pre-powertrain regression --\n");
    DriveSpec s;
    s.powerFade = 0.0f; // keep the historical arithmetic as an explicit control
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
    verdict(worst < 1e-4f, "powerFade 0 is bit-for-bit the pre-gearbox model");
}

void powerFadeResponse() {
    std::printf("-- high-speed power fade --\n");
    DriveSpec old, faded, boosted;
    old.powerFade = 0.0f;
    boosted.nosCapacity = 20.0f;
    boosted.nosBoost = 0.8f;
    DriveState a, b, c;
    a.pos[1] = old.rideHeight;
    b.pos[1] = faded.rideHeight;
    c.pos[1] = boosted.rideHeight;
    DriveInput normal, nos;
    normal.throttle = nos.throttle = 1.0f;
    nos.nos = true;
    for (int i = 0; i < 300; ++i) {
        step(old, normal, 1.0f / 50.0f, flat, a);
        step(faded, normal, 1.0f / 50.0f, flat, b);
        step(boosted, nos, 1.0f / 50.0f, flat, c);
    }
    std::printf("  6 s speeds: old %.2f, faded %.2f, nitrous %.2f\n",
                a.speed, b.speed, c.speed);
    verdict(b.speed < a.speed - 0.5f && b.speed < faded.topSpeed * 0.98f,
            "ordinary drive needs a long straight to approach redline");
    verdict(c.speed > b.speed + 2.0f, "nitrous restores high-speed pull");
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

    // Partial damage retains the authored power loss. A full wreck cannot
    // accelerate forward, reverse, or burn nitrous until repaired.
    {
        DriveSpec s;
        s.damage = 1.0f;
        DriveState a, b;
        a.pos[1] = b.pos[1] = s.rideHeight;
        b.damage = 0.75f;
        DriveInput in;
        in.throttle = 1.0f;
        for (int i = 0; i < 1500; ++i) {
            step(s, in, 1.0f / 50.0f, flat, a);
            step(s, in, 1.0f / 50.0f, flat, b);
        }
        std::printf("  top speed: pristine %.2f, damaged %.2f\n", a.speed, b.speed);
        verdict(std::fabs(damagePerformance(s, b.damage) -
                          (1.0f - 0.75f * s.damagePerfLoss)) < 1e-6f &&
                    b.speed < a.speed * 0.9f,
                "partial damage applies the authored power loss");
        DriveState wreck;
        wreck.pos[1] = s.rideHeight;
        wreck.damage = 1.0f;
        in.nos = true;
        for (int i = 0; i < 100; ++i) step(s, in, 1.0f / 50.0f, flat, wreck);
        verdict(std::fabs(wreck.speed) < 0.01f && !wreck.nosActive,
                "a wreck cannot drive forward or use nitrous");
        in.throttle = -1.0f;
        for (int i = 0; i < 100; ++i) step(s, in, 1.0f / 50.0f, flat, wreck);
        verdict(std::fabs(wreck.speed) < 0.01f,
                "a wreck cannot drive in reverse");
        wreck.damage = 0.0f;  // Repair Vehicle's drive-state reset.
        in.throttle = 1.0f;
        in.nos = false;
        for (int i = 0; i < 50; ++i) step(s, in, 1.0f / 50.0f, flat, wreck);
        verdict(wreck.speed > 1.0f, "a repaired wreck drives again");
        DriveSpec cosmetic = s;
        cosmetic.damageMechanical = 0.0f;
        DriveState full;
        full.pos[1] = cosmetic.rideHeight;
        full.damage = 1.0f;
        in.nos = true;
        cosmetic.nosCapacity = 4.0f;
        for (int i = 0; i < 100; ++i)
            step(cosmetic, in, 1.0f / 50.0f, flat, full);
        verdict(damagePerformance(cosmetic, 1.0f) == 1.0f &&
                    full.speed > 1.0f && full.nosActive,
                "visual-only wreck keeps throttle and nitrous");
        DriveSpec early = s, late = s;
        early.damagePerfCurve = 0.5f;
        late.damagePerfCurve = 2.0f;
        verdict(damagePerformance(early, 0.5f) < damagePerformance(s, 0.5f) &&
                    damagePerformance(s, 0.5f) < damagePerformance(late, 0.5f),
                "partial-loss curve moves power loss earlier or later");
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
        DriveSpec invisible = s;
        invisible.damageVisual = 0.0f;
        Impact hidden;
        float hiddenAdd = 0.0f;
        verdict(impactFromDelta(invisible, 0.0f, 0.0f, -20.0f, bmin, bmax,
                                1.0f, hidden, &hiddenAdd) &&
                    hidden.depth == 0.0f && hiddenAdd > 0.0f &&
                    damagePerformance(invisible, 0.5f) < 1.0f,
                "mechanical-only hit loses power without denting");
        invisible.damageMechanical = 0.0f;
        verdict(!impactFromDelta(invisible, 0.0f, 0.0f, -20.0f, bmin, bmax,
                                 1.0f, hidden, &hiddenAdd),
                "both damage switches off ignore impacts");
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

    // Handbrake HELD from top speed with a flick and no throttle (1.162.2):
    // the locked wheels are sliding friction on the whole ground velocity, so
    // the car comes to rest. It used to skate on - forward speed was scrubbed
    // but a car turned sideways kept its velocity as slip against half the
    // handbrake grip (the handbrake counted against its own friction circle),
    // over a hundred units on the default tuning.
    st = {};
    st.pos[1] = s.rideHeight;
    st.speed = s.topSpeed;
    in = {};
    in.handbrake = true;
    in.steer = 1.0f;
    in.throttle = 1.0f;  // the natural way to hold it: gas and handbrake together
    float slid = 0.0f;
    float slideTime = 0.0f;
    for (int i = 0; i < 50 * 20; ++i) {
        if (i == 15) in.steer = 0.0f;  // a flick, then hands off
        const float x0 = st.pos[0], z0 = st.pos[2];
        step(s, in, 1.0f / 50.0f, flat, st);
        slid += std::sqrt((st.pos[0] - x0) * (st.pos[0] - x0) +
                          (st.pos[2] - z0) * (st.pos[2] - z0));
        slideTime += 1.0f / 50.0f;
        if (std::sqrt(st.speed * st.speed + st.lateral * st.lateral) < 0.3f) break;
    }
    std::printf("  handbrake held from %.1f u/s: rests after %.1f units, %.2f s\n",
                s.topSpeed, slid, slideTime);
    verdict(slid < 60.0f && slideTime < 6.0f,
            "a held handbrake slides the car to a stop, not across the map");
}

// Off-road grip (1.136.0): the same drive on the road, on the grass, and
// straddling the edge.
void offroad() {
    auto flat = [](float, float) { return 0.0f; };
    const SurfaceFn allPaved = [](float, float) { return SurfaceSample{1.0f, 1.0f, 1.0f}; };
    const SurfaceFn noneRoad = [](float, float) { return SurfaceSample{0.0f, 1.0f, 1.0f}; };

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
    drive(rally, [](float x, float) { return SurfaceSample{x < 50.0f ? 1.0f : 0.0f, 1.0f, 1.0f}; },
          &vh, &dh);
    std::printf("  half on the road: %.1f u/s after 3 s\n", vh);
    verdict(vh > vr + 0.2f && vh < vp - 0.2f,
            "a car half on the grass sits between the two surfaces");

    // Road grip (1.137.0): a gravel road at 0.5 halves the corner, and leaves
    // the acceleration alone - it is still a road, not the car's off-road.
    float vg, dg;
    drive(base, [](float, float) { return SurfaceSample{1.0f, 0.5f, 1.0f}; }, &vg, &dg);
    std::printf("  road grip 0.5: corner %.1f u/s^2 (asphalt %.1f), %.1f u/s after 3 s\n",
                dg, d0, vg);
    verdict(dg <= 0.5f * base.grip + 0.5f && dg < d0 - 1.0f,
            "a road's grip multiplier scales the tyres' hold");
    verdict(std::fabs(vg - v0) < 0.05f, "road grip leaves the acceleration alone");

    // A painted terrain layer (1.142.0): mud at 0.5 multiplies ON TOP of the
    // car's off-road grip - 0.5 x 0.5 for the rally-tuned car below.
    float vm, dm;
    drive(rally, [](float, float) { return SurfaceSample{0.0f, 1.0f, 0.5f}; }, &vm, &dm);
    std::printf("  mud layer 0.5 on offroadGrip 0.5: corner %.1f u/s^2 (grass %.1f)\n",
                dm, dr);
    verdict(dm <= 0.25f * rally.grip + 0.5f && dm < dr - 1.0f,
            "a painted layer's grip multiplies the car's off-road grip");
}

// Crossings (1.143.0): a mud track (rank 0, grip 0.5, spill 2) crossing a
// main road (rank 2, grip 1) at right angles, assembled exactly as the test
// drive assembles its surface (vehicle_ui.cpp): the main road is on top, the
// track's surface trails 2 units onto it and its grip fades with the alpha.
void crossings() {
    std::printf("-- road crossings --\n");
    const std::vector<float> mainPts = {-50.0f, 0.0f, 50.0f, 0.0f};
    const std::vector<float> trackPts = {0.0f, -50.0f, 0.0f, 50.0f};
    const float mainW = 10.0f, trackW = 6.0f, spill = 2.0f;
    roadgen::Surface surf;
    std::vector<roadgen::Vertex> tris;
    roadgen::tessellate(mainPts, mainW, [](float, float) { return roadgen::rankLift(2); },
                        tris);
    surf.add(tris, 1.0f);
    roadgen::tessellate(trackPts, trackW, [](float, float) { return roadgen::rankLift(0); },
                        tris);
    surf.add(tris, 0.5f);
    std::vector<roadgen::SpillVertex> sv;
    roadgen::tessellateSpill(trackPts, trackW, 1.0f, mainPts, mainW, spill, sv);
    std::vector<roadgen::Vertex> st;
    std::vector<float> sg;
    for (const roadgen::SpillVertex& v : sv) {
        st.push_back({v.x, roadgen::kLift + roadgen::rankLift(2) + roadgen::kSpillLift,
                      v.z, v.u, v.v});
        sg.push_back(1.0f + (0.5f - 1.0f) * v.a);
    }
    surf.addBlended(st, sg);
    surf.build();
    auto gripAt = [&](float x, float z, float* y) {
        float g = -1.0f;
        *y = surf.at(x, z, &g);
        return g;
    };
    float y;
    const float gTrack = gripAt(0.0f, 20.0f, &y);
    const float gEdge = gripAt(0.0f, 4.9f, &y);
    const float gHalf = gripAt(0.0f, 4.0f, &y);
    const float gDeep = gripAt(0.0f, 2.0f, &y);
    float yCentre;
    const float gCentre = gripAt(0.0f, 0.0f, &yCentre);
    const float gMain = gripAt(20.0f, 0.0f, &y);
    std::printf("  spill %zu verts; grip track %.2f, edge %.2f, 1 in %.2f, 3 in %.2f, "
                "centre %.2f (y %.3f), main %.2f\n",
                sv.size(), gTrack, gEdge, gHalf, gDeep, gCentre, yCentre, gMain);
    verdict(!sv.empty() && sv.size() % 3 == 0, "a lower-rank crossing bakes a spill");
    verdict(std::fabs(gTrack - 0.5f) < 0.01f && std::fabs(gMain - 1.0f) < 0.01f,
            "each road keeps its own grip away from the crossing");
    verdict(gEdge < 0.6f && gHalf > gEdge + 0.1f && gHalf < 0.9f,
            "the spill's grip fades from the track's at the edge");
    verdict(std::fabs(gDeep - 1.0f) < 0.01f && std::fabs(gCentre - 1.0f) < 0.01f,
            "past the spill the main road's grip is back");
    verdict(std::fabs(yCentre - (roadgen::kLift + roadgen::rankLift(2))) < 0.001f,
            "the higher rank is the surface on top at the crossing");
    std::vector<roadgen::SpillVertex> none;
    roadgen::tessellateSpill(trackPts, trackW, 1.0f, {60.0f, 0.0f, 90.0f, 0.0f}, mainW,
                             spill, none);
    verdict(none.empty(), "roads that do not cross bake no spill");

    // Soft edges (1.144.0): a 9-wide road with a 1.5 fade. The core and the
    // bands must meet exactly, and the cover falls from 1 to 0 across a band.
    {
        const std::vector<float> pts = {0.0f, -40.0f, 0.0f, 40.0f};
        const roadgen::EdgeFade ef = roadgen::edgeFadeFor(9.0f, 1.5f);
        std::vector<roadgen::Vertex> core;
        roadgen::tessellate(pts, ef.coreWidth, nullptr, core, {}, 1.0f, ef.uInset);
        std::vector<roadgen::SpillVertex> ev;
        roadgen::tessellateEdges(pts, 9.0f, 1.0f, 1.5f, ev);
        roadgen::Surface es;
        es.add(core, 0.8f);
        std::vector<roadgen::Vertex> et;
        std::vector<float> cov;
        for (const roadgen::SpillVertex& v : ev) {
            et.push_back({v.x, roadgen::kLift, v.z, v.u, v.v});
            cov.push_back(v.a);
        }
        es.addEdge(et, 0.8f, cov);
        es.build();
        float coreMaxX = 0.0f, uMin = 1.0f, uMax = 0.0f;
        for (const roadgen::Vertex& v : core) {
            coreMaxX = std::max(coreMaxX, v.x);
            uMin = std::min(uMin, v.u);
            uMax = std::max(uMax, v.u);
        }
        float bandInner = 1e9f;
        for (const roadgen::SpillVertex& v : ev)
            if (v.a > 0.999f && v.x > 0.0f) bandInner = std::min(bandInner, v.x);
        float gc = 0, cc = 0, gm = 0, cm = 0, ce = 0;
        es.at(0.0f, 0.0f, &gc, &cc);
        es.at(3.75f, 0.0f, &gm, &cm);  // mid-band
        es.at(4.45f, 0.0f, nullptr, &ce);  // near the outer edge
        std::printf("  edge fade 1.5 on width 9: %d cols, core %.2f u %.3f..%.3f, "
                    "core edge %.4f band inner %.4f; cover centre %.2f mid %.2f "
                    "edge %.2f\n", ef.columns, ef.coreWidth, uMin, uMax, coreMaxX,
                    bandInner, cc, cm, ce);
        verdict(ef.columns == 3 && std::fabs(coreMaxX - bandInner) < 1e-4f,
                "the soft edge's bands meet the core with no crack");
        verdict(std::fabs(uMin - ef.uInset) < 1e-4f && std::fabs(uMax - (1.0f - ef.uInset)) < 1e-4f,
                "the core's texture still spans the full width");
        verdict(std::fabs(cc - 1.0f) < 1e-4f && cm > 0.3f && cm < 0.7f && ce < 0.15f,
                "the cover fades from 1 at the core to 0 at the edge");
    }
}

// Junction overrides (1.145.0, docs/roads.md "Junction overrides"): the ONE
// crossing planner the codegen, the viewport and the test drive share. Two
// Local roads, both naming the same intersection material, cross at right
// angles on flat ground: "a" along X (width 10, grip 1) and "b" along Z
// (width 6, grip 0.6). The surface is assembled exactly as the test drive
// assembles it (roadgen::addCrossingsToSurface).
void junctionOverrides() {
    std::printf("-- junction overrides --\n");
    std::vector<roadgen::CrossingRoad> roads(2);
    roads[0].id = "a";
    roads[0].points = {-50.0f, 0.0f, 50.0f, 0.0f};
    roads[0].width = 10.0f;
    roads[0].grip = 1.0f;
    roads[0].intersection = "res/materials/x.mtl";
    roads[1].id = "b";
    roads[1].points = {0.0f, -50.0f, 0.0f, 50.0f};
    roads[1].width = 6.0f;
    roads[1].grip = 0.6f;
    roads[1].spill = 2.0f;
    roads[1].intersection = "res/materials/x.mtl";
    const auto flat = [](float, float) { return 0.0f; };
    auto surfaceOf = [&](const roadgen::CrossingPlan& plan) {
        roadgen::Surface s;
        for (const roadgen::CrossingRoad& r : roads) {
            std::vector<roadgen::Vertex> tris;
            const float lift = roadgen::rankLift(r.rank);
            roadgen::tessellate(r.points, r.width, [&](float, float) { return lift; },
                                tris);
            s.add(tris, r.grip);
        }
        roadgen::addCrossingsToSurface(s, roads, plan, flat);
        s.build();
        return s;
    };

    // Auto: equal ranks + the same material = a patch at the lower grip.
    roadgen::CrossingPlan p0 = roadgen::planCrossings(roads, {});
    float g0 = 0.0f;
    const float y0 = surfaceOf(p0).at(0.0f, 0.0f, &g0);
    std::printf("  auto: %zu crossing(s), kind %d, grip %.2f, y %.3f\n",
                p0.crossings.size(), p0.crossings.empty() ? -1 : p0.crossings[0].kind,
                g0, y0);
    verdict(p0.crossings.size() == 1 && p0.crossings[0].kind == roadgen::kCrossPatch &&
                std::fabs(g0 - 0.6f) < 0.01f,
            "without an override the rank rule makes the patch at the lower grip");

    // Winner "b", stored with the ids REVERSED and 2.5 units off the crossing
    // (a point edit since): it still matches, suppresses the patch, lays b over
    // a, and a spills onto b one step higher.
    roadgen::JunctionOverride ow;
    ow.roadA = "b";
    ow.roadB = "a";
    ow.x = 1.5f;
    ow.z = -2.0f;
    ow.winner = roadgen::kWinnerRoadA;  // road "b" as stored
    roadgen::CrossingPlan p1 = roadgen::planCrossings(roads, {ow});
    const roadgen::Surface s1 = surfaceOf(p1);
    float gc = 0.0f, ge = 0.0f, gf = 0.0f;
    const float yc = s1.at(0.0f, 0.0f, &gc);
    const float ye = s1.at(2.9f, 0.0f, &ge);
    s1.at(20.0f, 0.0f, &gf);
    int overlays = 0, spills = 0;
    for (const roadgen::CrossingDecal& d : p1.decals) (d.overlay ? overlays : spills)++;
    std::printf("  winner b: matched %d, kind %d, winner %d, %d overlay(s) %d spill(s); "
                "centre grip %.2f y %.3f, edge grip %.2f y %.3f, a %.2f\n",
                p1.overrideCrossing.empty() ? -1 : p1.overrideCrossing[0],
                p1.crossings[0].kind, p1.crossings[0].winner, overlays, spills, gc, yc,
                ge, ye, gf);
    verdict(p1.overrideCrossing[0] == 0 && p1.orphans == 0,
            "an override matches its crossing across a small point edit and swapped ids");
    verdict(p1.crossings[0].kind == roadgen::kCrossThrough && p1.crossings[0].winner == 1,
            "a chosen winner suppresses the patch");
    verdict(overlays == 1 && std::fabs(gc - 0.6f) < 0.01f &&
                std::fabs(yc - (roadgen::kLift + roadgen::kSpillLift)) < 0.001f,
            "the winner's surface (and grip) is on top at the crossing");
    verdict(spills == 1 && ge > 0.9f &&
                std::fabs(ye - (roadgen::kLift + 2.0f * roadgen::kSpillLift)) < 0.001f,
            "the loser spills onto the winner, over the overlay");
    verdict(std::fabs(gf - 1.0f) < 0.01f, "away from the crossing each road is its own");

    // Forced patch across ranks, with its own material and grip: a is Main, b
    // is Track (which would spill onto a).
    roads[0].rank = 2;
    roads[1].rank = 0;
    roadgen::CrossingPlan pr = roadgen::planCrossings(roads, {});
    roadgen::JunctionOverride op;
    op.roadA = "a";
    op.roadB = "b";
    op.winner = roadgen::kWinnerAuto;
    op.material = "res/materials/cobble.mtl";  // a material alone forces a patch
    op.grip = 0.3f;
    roadgen::CrossingPlan p2 = roadgen::planCrossings(roads, {op});
    float gp = 0.0f;
    const float yp = surfaceOf(p2).at(0.0f, 0.0f, &gp);
    std::printf("  ranks 2/0: auto kind %d with %zu decal(s); forced kind %d material %s "
                "grip %.2f y %.3f, %zu decal(s)\n",
                pr.crossings[0].kind, pr.decals.size(), p2.crossings[0].kind,
                p2.crossings[0].material.c_str(), gp, yp, p2.decals.size());
    verdict(pr.crossings[0].kind == roadgen::kCrossThrough && pr.decals.size() == 1,
            "across ranks Auto keeps the rank rule (through + spill)");
    verdict(p2.crossings[0].kind == roadgen::kCrossPatch &&
                p2.crossings[0].material == op.material && std::fabs(gp - 0.3f) < 0.01f &&
                std::fabs(yp - (roadgen::kLift + roadgen::rankLift(2) + 0.02f)) < 0.001f,
            "a patch material forces a patch across ranks, at its own grip, on top");
    verdict(p2.decals.empty(), "a forced patch takes the crossing's spill away");

    // Orphans: an override far from any crossing of its pair, or naming a
    // road that is gone, matches nothing and is counted, not dropped.
    roadgen::JunctionOverride far = op;
    far.x = 30.0f;
    roadgen::JunctionOverride gone = op;
    gone.roadB = "deleted";
    roadgen::CrossingPlan p3 = roadgen::planCrossings(roads, {far, gone});
    std::printf("  orphans: %d (crossing %d, %d)\n", p3.orphans, p3.overrideCrossing[0],
                p3.overrideCrossing[1]);
    verdict(p3.orphans == 2 && p3.overrideCrossing[0] < 0 && p3.overrideCrossing[1] < 0 &&
                p3.crossings[0].kind == roadgen::kCrossThrough,
            "an override whose crossing is gone is reported as orphaned and changes nothing");
}

// Road nodes (1.170.0, docs/roads.md "Road nodes"): T's, forks at any angle,
// many-armed crossings and corners become ONE node each, with a filleted
// outline. Flat ground, every road naming the same intersection material, so
// every node is a patch and the assembled surface shows where it reaches.
void roadNodes() {
    std::printf("-- road nodes --\n");
    auto road = [](const char* id, std::vector<float> pts, float width) {
        roadgen::CrossingRoad r;
        r.id = id;
        r.points = std::move(pts);
        r.width = width;
        r.intersection = "res/materials/x.mtl";
        return r;
    };
    const auto flat = [](float, float) { return 0.0f; };
    auto surfaceOf = [&](const std::vector<roadgen::CrossingRoad>& roads,
                         const roadgen::CrossingPlan& plan) {
        roadgen::Surface s;
        for (const roadgen::CrossingRoad& r : roads) {
            std::vector<roadgen::Vertex> tris;
            roadgen::tessellate(r.points, r.width, flat, tris);
            s.add(tris, r.grip);
        }
        roadgen::addCrossingsToSurface(s, roads, plan, flat);
        s.build();
        return s;
    };
    auto on = [](const roadgen::Surface& s, float x, float z) {
        return s.at(x, z) != roadgen::Surface::kNone;
    };
    auto summary = [](const char* what, const roadgen::CrossingPlan& p) {
        std::printf("  %s: %zu node(s)", what, p.crossings.size());
        for (const roadgen::Crossing& c : p.crossings)
            std::printf(" [%zu roads, %d arms, kind %d, %zu outline pts]", c.roads.size(),
                        c.arms, c.kind, c.shape.outline.size() / 2);
        std::printf("\n");
    };

    // T: a stem ending on a through road. The fillet fills the inside corner
    // (3.5, 4.5) - on neither road - and leaves the far side of the through
    // road and the arc's outside alone.
    {
        std::vector<roadgen::CrossingRoad> r = {road("main", {-40, 0, 40, 0}, 8),
                                                road("stem", {0, 0, 0, 40}, 6)};
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        summary("T", p);
        const roadgen::Surface s = surfaceOf(r, p);
        verdict(p.crossings.size() == 1 && p.crossings[0].arms == 3 &&
                    p.crossings[0].kind == roadgen::kCrossPatch,
                "a road ending on another makes one three-armed node");
        verdict(on(s, 3.5f, 4.5f) && on(s, -3.5f, 4.5f) && !on(s, 5.5f, 6.5f) &&
                    !on(s, 0.0f, -4.5f),
                "the T's fillets pave both inside corners and nothing else");
    }
    // Y: one road splitting in two, 41 degrees apart, all ending at one spot.
    {
        std::vector<roadgen::CrossingRoad> r = {road("in", {0, -40, 0, 0}, 6),
                                                road("left", {0, 0, -15, 40}, 6),
                                                road("right", {0, 0, 15, 40}, 6)};
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        summary("Y", p);
        const roadgen::Surface s = surfaceOf(r, p);
        verdict(p.crossings.size() == 1 && p.crossings[0].arms == 3 &&
                    p.crossings[0].roads.size() == 3,
                "a three-way fork is one node holding all three roads");
        verdict(on(s, 0.0f, 0.5f) && !on(s, 0.0f, 30.0f),
                "the fork's patch covers the split and stops short of the gore");
    }
    // A slip road leaving at 14 degrees: the old crossing finder refused it.
    {
        std::vector<roadgen::CrossingRoad> r = {road("main", {0, -40, 0, 40}, 8),
                                                road("slip", {0, 0, 10, 40}, 6)};
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        summary("slip road", p);
        verdict(p.crossings.size() == 1 && p.crossings[0].arms == 3,
                "a shallow fork off a through road is a node");
    }
    // Three roads crossing at one point, 60 degrees apart: six arms, one node.
    {
        std::vector<roadgen::CrossingRoad> r;
        const char* ids[3] = {"r0", "r1", "r2"};
        for (int k = 0; k < 3; ++k) {
            const float a = (float)k * 1.0471976f;
            r.push_back(road(ids[k], {-40 * std::cos(a), -40 * std::sin(a),
                                      40 * std::cos(a), 40 * std::sin(a)}, 6));
        }
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        summary("six-way", p);
        verdict(p.crossings.size() == 1 && p.crossings[0].arms == 6,
                "three roads through one spot are one six-armed node");
        // The same node on rolling ground (the scratch fixture that found it):
        // a sliver piece of the grid cut once read as a 1e30 clearance deficit
        // and lifted the whole patch out of the world.
        // A real heightfield (33 x 33 nodes over 160 units, the scratch
        // project's), sampled the way the renderer draws it.
        std::vector<float> hm(33 * 33);
        for (int k = 0; k < 33; ++k)
            for (int i = 0; i < 33; ++i) {
                const float x = -80.0f + 5.0f * (float)i, z = -80.0f + 5.0f * (float)k;
                hm[(size_t)k * 33 + i] = 1.2f * std::sin(x * 0.21f) * std::cos(z * 0.17f) +
                                         0.6f * std::sin(z * 0.37f + 1.0f);
            }
        const roadgen::HeightFn hills = [&](float x, float z) {
            return roadgen::terrainHeight(hm, 33, 33, 160.0f, 160.0f, x, z);
        };
        const roadgen::TerrainGrid grid = roadgen::terrainGridOf(33, 33, 160.0f, 160.0f);
        std::vector<roadgen::Vertex> roadTris;
        for (const roadgen::CrossingRoad& rr : r) {
            std::vector<roadgen::Vertex> m;
            roadgen::tessellate(rr.points, rr.width, hills, m);
            roadTris.insert(roadTris.end(), m.begin(), m.end());
        }
        float worst = 0.0f;
        size_t verts = 0;
        for (const roadgen::Crossing& c : p.crossings) {
            std::vector<roadgen::Vertex> patch;
            roadgen::tessellateJunctionSurface(c.shape, roadTris, hills, c.lift, patch, grid);
            verts += patch.size();
            for (const roadgen::Vertex& v : patch)
                worst = std::max(worst, std::fabs(v.y - hills(v.x, v.z)));
        }
        std::printf("  six-way on hills: %zu verts, worst height over ground %.3f\n", verts,
                    worst);
        verdict(verts > 0 && worst < 0.5f, "a node patch on rolling ground stays on the ground");
    }
    // L: two roads ending at one spot at right angles. The outside corner is
    // on neither road; the node rounds it.
    {
        std::vector<roadgen::CrossingRoad> r = {road("a", {40, 0, 0, 0}, 8),
                                                road("b", {0, 0, 0, 40}, 8)};
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        summary("L", p);
        const roadgen::Surface s = surfaceOf(r, p);
        verdict(p.crossings.size() == 1 && p.crossings[0].arms == 2 &&
                    on(s, -2.0f, -2.0f) && !on(s, -3.5f, -3.5f),
                "a corner of two road ends is rounded on its outside");
    }
    // A width change where two roads join in line is a TRANSITION node: its
    // patch is the taper, laid on the narrow road (1.171.0).
    {
        std::vector<roadgen::CrossingRoad> r = {road("wide", {-40, 0, 0, 0}, 12),
                                                road("narrow", {0, 0, 40, 0}, 6)};
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        summary("transition", p);
        const roadgen::Surface s = surfaceOf(r, p);
        // Halfway along the taper (18 units: 3 per unit of width lost) the
        // patch is ~4.5 wide each side: on it at 4.2, off the narrow road there.
        verdict(p.crossings.size() == 1 && p.crossings[0].transition &&
                    on(s, 9.0f, 4.2f) && !on(s, 9.0f, 5.2f) && !on(s, 25.0f, 4.0f),
                "a width change in line tapers from the wide road to the narrow one");
    }
    // Markings: a T paints a stop line on the stem only; zebras when asked;
    // none when a road says none.
    {
        std::vector<roadgen::CrossingRoad> r = {road("main", {-40, 0, 40, 0}, 8),
                                                road("stem", {0, 0, 0, 40}, 6)};
        auto paintOf = [&](const std::vector<roadgen::CrossingRoad>& rr) {
            const roadgen::CrossingPlan p = roadgen::planCrossings(rr, {});
            const roadgen::Surface s = surfaceOf(rr, p);
            std::vector<roadgen::Vertex> paint;
            roadgen::bakeMarkings(p, rr, s, paint);
            return paint;
        };
        const std::vector<roadgen::Vertex> stop = paintOf(r);
        // Triangles by where their centre lies: the stem's incoming lane
        // (x < 0 - right-hand traffic coming south), its outgoing lane, and
        // the painted edge line the node carries round its fillets.
        int incoming = 0, outgoing = 0, edges = 0;
        for (size_t t = 0; t + 2 < stop.size(); t += 3) {
            const float cx = (stop[t].x + stop[t + 1].x + stop[t + 2].x) / 3.0f;
            const float cz = (stop[t].z + stop[t + 1].z + stop[t + 2].z) / 3.0f;
            if (cz > 2.0f && cz < 15.0f && cx > -2.4f && cx < -0.1f) ++incoming;
            if (cz > 2.0f && cz < 15.0f && cx > 0.1f && cx < 2.4f) ++outgoing;
            if (cz < -3.0f && cz > -4.0f) ++edges;  // along the main road's far edge
        }
        r[0].markings = r[1].markings = roadgen::kMarkCrossings;
        const std::vector<roadgen::Vertex> zebra = paintOf(r);
        r[0].markings = r[1].markings = roadgen::kMarkNone;
        const std::vector<roadgen::Vertex> none = paintOf(r);
        std::printf("  markings: T %zu verts (stop line %d tris, outgoing lane %d, far edge "
                    "line %d), + zebras %zu, none %zu\n",
                    stop.size(), incoming, outgoing, edges, zebra.size(), none.size());
        verdict(incoming == 2 && outgoing == 0,
                "a T paints one stop line, on the stem's incoming lane");
        verdict(edges > 0, "the node carries the road's edge line along its patch");
        verdict(zebra.size() >= stop.size() + 19 * 6 && none.empty(),
                "zebras are added on request, and a road can ask for no paint");
    }
    // A road simply continuing into another is not a junction.
    {
        std::vector<roadgen::CrossingRoad> r = {road("a", {-40, 0, 0, 0}, 8),
                                                road("b", {0, 0, 40, 0}, 8)};
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        summary("continuation", p);
        verdict(p.crossings.empty(), "two roads joined end to end in line make no node");
    }
}

// Kerbs (docs/roads.md "Kerbs"): a kerbed T on flat ground. The kerb must stop
// where a road enters the node patch, run around both fillets and along the
// far side, never stand on a road (an arm cap is where the road carries on),
// merge a straight edge to a few points, and pack into valid strip runs.
// Road textures (docs/road-textures.md): the weathering knobs are additive, so
// wear = grime = cracks = 0 must still be the clean texture BIT FOR BIT - the
// golden hashes are the pixels of every preset before weathering existed
// (FNV-1a 64 of generate()). No libm call reaches a pixel on that path except
// through a factor of exactly 0, so the hashes are the same at -O1 and -O3.
void roadTextures() {
    std::printf("-- road textures --\n");
    struct Golden {
        const char* name;
        unsigned long long hash;
    };
    const Golden golden[] = {{"road-2lane", 0x9036849f596a4a86ull},
                             {"road-4lane", 0x378e108cdafceafeull},
                             {"road-dirt", 0xaa4a5058e2cf2ef0ull},
                             {"road-cobble", 0xa4d8fb81c69f1013ull},
                             {"road-junction", 0xec01f4be25364863ull},
                             {"pavement-slabs", 0xb448c8362828f2eeull}};
    auto fnv = [](const std::vector<unsigned char>& px) {
        unsigned long long h = 1469598103934665603ull;
        for (unsigned char b : px) h = (h ^ b) * 1099511628211ull;
        return h;
    };
    int matched = 0, roundTrips = 0, stable = 0, total = 0;
    for (const roadtex::Preset& pr : roadtex::presets()) {
        ++total;
        roadtex::RoadTexParams clean = pr.params;
        clean.wear = clean.grime = clean.cracks = 0.0f;
        const unsigned long long h = fnv(roadtex::generate(clean));
        for (const Golden& g : golden)
            if (std::string(g.name) == pr.name && g.hash == h) ++matched;
        if (roadtex::fromText(roadtex::toText(pr.params)) == pr.params) ++roundTrips;
        if (roadtex::generate(pr.params) == roadtex::generate(pr.params)) ++stable;
    }
    char what[160];
    std::snprintf(what, sizeof what, "weathering 0 = the clean texture bit for bit (%d/%d presets)",
                  matched, (int)(sizeof golden / sizeof golden[0]));
    verdict(matched == (int)(sizeof golden / sizeof golden[0]), what);
    verdict(roundTrips == total, "every preset's recipe survives toText -> fromText");
    verdict(stable == total, "generate() is deterministic (two calls, same bytes)");
    roadtex::RoadTexParams worn = roadtex::presets()[0].params;
    roadtex::RoadTexParams clean = worn;
    clean.wear = clean.grime = clean.cracks = 0.0f;
    verdict(roadtex::generate(worn) != roadtex::generate(clean),
            "the default weathering changes the texture");
}

void roadKerbs() {
    std::printf("-- road kerbs --\n");
    auto road = [](const char* id, std::vector<float> pts, float width, bool kerb) {
        roadgen::CrossingRoad r;
        r.id = id;
        r.points = std::move(pts);
        r.width = width;
        r.intersection = "res/materials/x.mtl";
        r.kerb = kerb;
        return r;
    };
    const auto flat = [](float, float) { return 0.0f; };
    auto plan = [&](const std::vector<roadgen::CrossingRoad>& r,
                    std::vector<roadgen::KerbPiece>& pieces) {
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        roadgen::Surface s;
        for (const roadgen::CrossingRoad& rd : r) {
            std::vector<roadgen::Vertex> tris;
            roadgen::tessellate(rd.points, rd.width, flat, tris);
            s.add(tris, rd.grip);
        }
        roadgen::addCrossingsToSurface(s, r, p, flat);
        s.build();
        pieces = roadgen::planKerbs(r, p, [&](float x, float z) { return s.at(x, z); }, flat);
    };
    std::vector<roadgen::KerbPiece> k;
    plan({road("main", {-40, 0, 40, 0}, 8, true), road("stem", {0, 0, 0, 40}, 6, true)}, k);
    int chains = 0, roadPts = 0, farPts = 0;
    float stemMinZ = 1e30f, worstOnRoad = 1e30f;
    bool fillet = false, farChain = false;
    for (const roadgen::KerbPiece& p : k) {
        if (p.node >= 0) ++chains; else roadPts += p.points();
        for (int i = 0; i < p.points(); ++i) {
            const float x = p.pts[(size_t)i * 5], z = p.pts[(size_t)i * 5 + 2];
            // Margin into a road: the main is |z| < 4, the stem |x| < 3, z > 0.
            worstOnRoad = std::min({worstOnRoad, std::fabs(z) - 4.0f,
                                    z > 0.0f ? std::fabs(x) - 3.0f : 1e30f});
            if (p.node < 0 && p.road == 1) stemMinZ = std::min(stemMinZ, z);
            if (p.node < 0 && p.road == 0 && z < 0.0f) ++farPts;
            if (p.node >= 0 && x > 3.3f && z > 4.3f && x < 12.0f && z < 12.0f) fillet = true;
        }
        // The far side: one line along z = -4 straight across the stem's mouth.
        if (p.node >= 0 && p.points() >= 2) {
            bool along = true;
            float lo = 1e30f, hi = -1e30f;
            for (int i = 0; i < p.points(); ++i) {
                along &= std::fabs(p.pts[(size_t)i * 5 + 2] + 4.0f) < 0.01f;
                lo = std::min(lo, p.pts[(size_t)i * 5]);
                hi = std::max(hi, p.pts[(size_t)i * 5]);
            }
            farChain |= along && lo < -3.0f && hi > 3.0f;
        }
    }
    std::printf("  T: %zu kerb lines (%d around the node), %d road-edge points (%d on the far "
                "side, from 2 x 81 stations), stem kerb starts at z %.2f, closest to a road "
                "%.3f\n",
                k.size(), chains, roadPts, farPts, stemMinZ, worstOnRoad);
    verdict(chains == 3 && fillet && farChain,
            "the kerb runs around both fillets and along the T's far side");
    verdict(stemMinZ > 5.0f, "the stem's own kerb stops where it enters the node patch");
    verdict(worstOnRoad > -0.01f, "no kerb point stands on a road (none crosses an arm cap)");
    verdict(farPts <= 14, "a straight edge merges to a point every 8 units at most");
    {
        // Collision: the kerb tops join the drawn surface the height index reads.
        const std::vector<roadgen::CrossingRoad> r = {
            road("main", {-40, 0, 40, 0}, 8, true), road("stem", {0, 0, 0, 40}, 6, true)};
        const roadgen::CrossingPlan p = roadgen::planCrossings(r, {});
        roadgen::Surface s;
        for (const roadgen::CrossingRoad& rd : r) {
            std::vector<roadgen::Vertex> tris;
            roadgen::tessellate(rd.points, rd.width, flat, tris);
            s.add(tris, rd.grip);
        }
        roadgen::addCrossingsToSurface(s, r, p, flat);
        roadgen::addKerbsToSurface(s, r, p, flat);
        s.build();
        const float onKerb = s.at(-20.0f, -4.1f), onRoad = s.at(-20.0f, -3.5f),
                    past = s.at(-20.0f, -4.6f);
        std::printf("  kerb collision: road %.3f, kerb top %.3f, past the kerb %s\n", onRoad,
                    onKerb, past == roadgen::Surface::kNone ? "none" : "SURFACE");
        // Pavements: the same T with a 2.5-unit walk behind every kerb.
        std::vector<roadgen::CrossingRoad> rp = r;
        for (roadgen::CrossingRoad& rd : rp) rd.pavement = 2.5f;
        // Ground that rises past z = -9 (the main road's far side) by 1 unit.
        const auto slope = flat;
        std::vector<roadgen::KerbPiece> kp;
        {
            roadgen::Surface ks;
            for (const roadgen::CrossingRoad& rd : rp) {
                std::vector<roadgen::Vertex> tris;
                roadgen::tessellate(rd.points, rd.width, flat, tris);
                ks.add(tris, rd.grip);
            }
            roadgen::addCrossingsToSurface(ks, rp, p, flat);
            ks.build();
            kp = roadgen::planKerbs(rp, p, [&](float x, float z) { return ks.at(x, z); }, slope);
        }
        const std::vector<roadgen::PavementMesh> pave = roadgen::planPavements(rp, p, kp, slope);
        roadgen::Surface ps;
        roadgen::addPavementsToSurface(ps, pave);
        ps.build();
        size_t paveVerts = 0;
        float worstUv = 0.0f;
        for (const roadgen::PavementMesh& pm : pave)
            for (const roadgen::Vertex& v : pm.tris) {
                ++paveVerts;
                worstUv = std::max({worstUv, std::fabs(v.u), std::fabs(v.v)});
            }
        // On the main road's far side, 1.5 units behind the kerb (z = -5.75):
        // the kerb top's height. Past the walk (z = -7): nothing.
        const float walk = ps.at(-20.0f, -5.75f), beyond = ps.at(-20.0f, -7.0f);
        // Into the stem's mouth: the walk must not reach the stem (|x| < 3).
        bool onStem = false;
        for (const roadgen::PavementMesh& pm : pave)
            for (const roadgen::Vertex& v : pm.tris)
                onStem |= v.z > 4.5f && std::fabs(v.x) < 2.95f;
        // The inside corner of a fillet closes: a point in the block corner,
        // diagonal to the T's right fillet, is covered.
        const float corner = ps.at(5.45f, 6.45f);
        std::printf("  pavements: %zu vertices in %zu meshes, walk %.3f (kerb top %.3f), past it %s, "
                    "fillet corner %s, max |uv| %.1f\n",
                    paveVerts, pave.size(), walk, onKerb,
                    beyond == roadgen::Surface::kNone ? "none" : "SURFACE",
                    corner == roadgen::Surface::kNone ? "OPEN" : "covered", worstUv);
        verdict(std::fabs(walk - onKerb) < 0.02f && beyond == roadgen::Surface::kNone,
                "a pavement is the kerb top carried on, as wide as asked");
        verdict(!onStem, "a pavement narrows instead of running onto another road");
        verdict(corner != roadgen::Surface::kNone, "the inside of a fillet corner is paved");
        verdict(worstUv < 64.0f, "pavement UVs are rebased to small numbers");
        verdict(std::fabs(onKerb - onRoad - 0.15f) < 0.02f && onRoad < 0.2f &&
                    past == roadgen::Surface::kNone,
                "a kerb top is standable surface one kerb height above the road, nothing past it");
    }
    // Every fillet end meets a road kerb end in one point.
    int met = 0, ends = 0;
    for (const roadgen::KerbPiece& c : k) {
        if (c.node < 0) continue;
        for (int e : {0, c.points() - 1}) {
            ++ends;
            const float* q = &c.pts[(size_t)e * 5];
            bool found = false;
            for (const roadgen::KerbPiece& r : k)
                if (r.node < 0)
                    for (int f : {0, r.points() - 1}) {
                        const float* s = &r.pts[(size_t)f * 5];
                        found |= std::hypot(q[0] - s[0], q[2] - s[2]) < 0.01f;
                    }
            met += found ? 1 : 0;
        }
    }
    verdict(ends == 6 && met == 6, "every node kerb end meets its road's kerb end");
    // Strip runs: whole chunks, every run of 75 a valid strip, the same
    // triangles as the list.
    std::vector<roadgen::KerbVertex> sv, lv;
    std::vector<int> sizes;
    roadgen::kerbStrips(k, sv, sizes);
    for (const roadgen::KerbPiece& p : k) roadgen::kerbTriangles(p, lv);
    int sum = 0, tris = 0;
    bool mult3 = true;
    for (int sz : sizes) {
        sum += sz;
        mult3 &= sz % 3 == 0;
    }
    auto same = [](const roadgen::KerbVertex& a, const roadgen::KerbVertex& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    for (size_t at = 0, ci = 0; ci < sizes.size(); at += (size_t)sizes[ci], ++ci)
        for (size_t r0 = at; r0 < at + (size_t)sizes[ci]; r0 += 75) {
            const size_t len = std::min((size_t)75, at + (size_t)sizes[ci] - r0);
            for (size_t i = 0; i + 2 < len; ++i)
                if (!same(sv[r0 + i], sv[r0 + i + 1]) && !same(sv[r0 + i + 1], sv[r0 + i + 2]) &&
                    !same(sv[r0 + i], sv[r0 + i + 2]))
                    ++tris;
        }
    std::printf("  strips: %zu vertices in %zu chunks, %d triangles (list %zu)\n", sv.size(),
                sizes.size(), tris, lv.size() / 3);
    verdict(sum == (int)sv.size() && mult3 && tris == (int)(lv.size() / 3),
            "the strip runs hold exactly the list's triangles");
    // A kerbless stem: the fillets lose their kerb, the far side keeps it.
    plan({road("main", {-40, 0, 40, 0}, 8, true), road("stem", {0, 0, 0, 40}, 6, false)}, k);
    chains = 0;
    for (const roadgen::KerbPiece& p : k) chains += p.node >= 0 ? 1 : 0;
    verdict(chains == 1, "a fillet keeps its kerb only when both of its roads have kerbs");
}

// Rails and tram tracks (docs/roads.md "Rails and tram tracks"): a railway
// along z = 10 crossed at x = 0 by a kerbed, zebra-painted street, on flat
// ground. The rails must sit at the gauge on the ballast, run on unbroken
// across the street (flush there, with a crossing panel), never be kerbed or
// painted, pack into valid strips, and the ballast texture must be a pure,
// tiling function of its recipe with seven sleepers per repeat.
void roadRails() {
    std::printf("-- road rails --\n");
    auto object = [](const char* id, std::vector<float> pts, float width) {
        SceneObject o;
        o.type = PrimitiveType::Road;
        o.id = id;
        o.roadPoints = std::move(pts);
        o.roadWidth = width;
        o.roadIntersectionTexture = "res/materials/x.mtl";
        return o;
    };
    std::vector<SceneObject> objs;
    objs.push_back(object("rail", {-40, 10, 40, 10}, 3.6f));
    objs.back().roadKind = roadrail::kRail;
    objs.back().roadRank = 0;
    objs.back().roadSpill = 0.0f;
    objs.back().roadKerb = true;      // authored by mistake: a railway is never kerbed
    objs.back().roadMarkings = 2;     // ... nor painted
    objs.push_back(object("street", {0, -30, 0, 50}, 8.0f));
    objs.back().roadKerb = true;
    objs.back().roadMarkings = 2;
    const std::vector<roadgen::CrossingRoad> r = project::crossingRoads(objs);
    verdict(r.size() == 2 && r[0].kind == roadrail::kRail && !r[0].kerb && r[0].markings == 0 &&
                !r[0].edgeLine && r[1].kerb,
            "a railway road is never kerbed or painted, the street keeps both");
    auto build = [&](const std::vector<roadgen::CrossingRoad>& rs, roadgen::CrossingPlan& p,
                     roadgen::Surface& s) {
        p = roadgen::planCrossings(rs, {});
        for (const roadgen::CrossingRoad& rd : rs) {
            std::vector<roadgen::Vertex> tris;
            roadgen::tessellate(rd.points, rd.width,
                                [&](float, float) { return roadgen::rankLift(rd.rank); }, tris);
            s.add(tris, rd.grip);
        }
        roadgen::addCrossingsToSurface(s, rs, p, flat);
        s.build();
    };
    roadgen::CrossingPlan plan;
    roadgen::Surface surf;
    build(r, plan, surf);
    const auto at = [&](float x, float z) { return surf.at(x, z); };
    const std::vector<roadrail::RailPiece> rails = roadrail::planRails(r, plan, at, flat);
    // Gauge: every rail point of the railway sits on z = 10 +- (1.435 + 0.07) / 2.
    const float half = 0.5f * (1.435f + roadrail::kRailHeadWidth);
    float worstGauge = 0.0f, worstHeight = 0.0f;
    int raised = 0, flush = 0, panels = 0;
    float flushMinX = 1e30f, flushMaxX = -1e30f;
    std::vector<std::pair<float, float>> spans[2];  // per rail: x ranges covered
    for (const roadrail::RailPiece& p : rails) {
        if (p.road != 0) continue;
        if (p.profile == roadrail::kProfilePanel) {
            ++panels;
            continue;
        }
        (p.profile == roadrail::kProfileRaised ? raised : flush)++;
        const int side = p.pts[2] > 10.0f ? 1 : 0;
        float x0 = 1e30f, x1 = -1e30f;
        for (int i = 0; i < p.points(); ++i) {
            const float x = p.pts[(size_t)i * 5], y = p.pts[(size_t)i * 5 + 1],
                        z = p.pts[(size_t)i * 5 + 2];
            worstGauge = std::max(worstGauge, std::fabs(std::fabs(z - 10.0f) - half));
            // On the ballast the line stands on the rail road's own surface;
            // on the street, on the street's.
            const float expect = p.profile == roadrail::kProfileRaised
                                     ? roadgen::kLift + roadgen::rankLift(0)
                                     : roadgen::kLift + roadgen::rankLift(1);
            worstHeight = std::max(worstHeight, std::fabs(y - expect));
            x0 = std::min(x0, x), x1 = std::max(x1, x);
            if (p.profile == roadrail::kProfileFlush)
                flushMinX = std::min(flushMinX, x), flushMaxX = std::max(flushMaxX, x);
        }
        spans[side].push_back({x0, x1});
    }
    std::printf("  railway: %d raised, %d flush, %d panel line(s); gauge error %.5f, height "
                "error %.5f; flush over x %.2f..%.2f\n",
                raised, flush, panels, worstGauge, worstHeight, flushMinX, flushMaxX);
    verdict(worstGauge < 1e-3f, "the rails sit at the gauge (inner faces 1.435 apart)");
    verdict(worstHeight < 2e-3f, "each rail line stands on the drawn surface under it");
    // Unbroken: per rail, the spans chain from x = -40 to 40 with no gap.
    bool unbroken = true;
    for (auto& sp : spans) {
        std::sort(sp.begin(), sp.end());
        float reach = -40.0f;
        for (const auto& [a, b] : sp) {
            unbroken &= a <= reach + 1e-3f;
            reach = std::max(reach, b);
        }
        unbroken &= reach >= 40.0f - 1e-3f && !sp.empty() && sp.front().first <= -40.0f + 1e-3f;
    }
    verdict(raised == 4 && flush == 2 && unbroken,
            "the rails run on unbroken across the street (raised, flush, raised)");
    verdict(flushMinX > -4.05f && flushMaxX < 4.05f && flushMinX < -3.9f && flushMaxX > 3.9f,
            "they are flush exactly where the street is (|x| < 4)");
    verdict(panels == 1, "the level crossing gets one panel under the track");
    // Collision: a car on the crossing rides the flush head, one on the
    // ballast bumps a raised rail, and the step stays far below a walker's.
    {
        roadgen::Surface s2;
        roadgen::CrossingPlan p2;
        build(r, p2, s2);
        roadrail::addRailsToSurface(s2, r, p2, flat);
        s2.build();
        const float onFlush = s2.at(0.0f, 10.0f + half), onRaised = s2.at(-20.0f, 10.0f + half);
        const float bed = s2.at(-20.0f, 10.0f);
        std::printf("  rail collision: flush head %.3f, raised head %.3f over a bed at %.3f\n",
                    onFlush, onRaised, bed);
        verdict(std::fabs(onFlush - (roadgen::kLift + roadrail::kFlushLift)) < 2e-3f &&
                    std::fabs(onRaised - bed - roadrail::kRailHeight) < 2e-3f &&
                    roadrail::kRailHeight < 0.5f,
                "rail heads are standable surface, a step a walker clears");
    }
    // No kerb stands on the ballast and no paint lands on a railway's node.
    {
        const std::vector<roadgen::KerbPiece> kerbs = roadgen::planKerbs(r, plan, at, flat);
        bool onBallast = false, railKerb = false;
        for (const roadgen::KerbPiece& k : kerbs) {
            railKerb |= k.road == 0;
            for (int i = 0; i < k.points(); ++i)
                onBallast |= std::fabs(k.pts[(size_t)i * 5 + 2] - 10.0f) < 1.79f &&
                             std::fabs(k.pts[(size_t)i * 5]) < 4.1f;
        }
        std::vector<roadgen::Vertex> paint;
        roadgen::bakeMarkings(plan, r, surf, paint);
        int patchNodes = 0;
        for (const roadgen::Crossing& c : plan.crossings)
            patchNodes += c.kind == roadgen::kCrossPatch ? 1 : 0;
        std::printf("  %zu street kerb line(s), %zu paint vertices at %d patch node(s)\n",
                    kerbs.size(), paint.size(), patchNodes);
        verdict(!railKerb && !onBallast && !kerbs.empty(),
                "the street's kerbs stop at the ballast, the railway has none");
        verdict(paint.empty(), "no zebra or stop line is painted across the railway");
    }
    // Strips: the same triangles as the list, whole chunks, every size % 3.
    {
        std::vector<roadgen::KerbVertex> sv, lv;
        std::vector<int> sizes;
        roadrail::railStrips(rails, sv, sizes);
        for (const roadrail::RailPiece& p : rails) roadrail::railTriangles(p, lv);
        auto same = [](const roadgen::KerbVertex& a, const roadgen::KerbVertex& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };
        int sum = 0, tris = 0;
        bool mult3 = true, shades = true;
        for (int sz : sizes) sum += sz, mult3 &= sz % 3 == 0;
        for (const roadgen::KerbVertex& v : sv) shades &= v.shade >= 2.0f && v.shade < 6.0f;
        for (size_t a0 = 0, ci = 0; ci < sizes.size(); a0 += (size_t)sizes[ci], ++ci)
            for (size_t r0 = a0; r0 < a0 + (size_t)sizes[ci]; r0 += 75) {
                const size_t len = std::min((size_t)75, a0 + (size_t)sizes[ci] - r0);
                for (size_t i = 0; i + 2 < len; ++i)
                    if (!same(sv[r0 + i], sv[r0 + i + 1]) &&
                        !same(sv[r0 + i + 1], sv[r0 + i + 2]) && !same(sv[r0 + i], sv[r0 + i + 2]))
                        ++tris;
            }
        std::printf("  strips: %zu vertices in %zu chunks, %d triangles (list %zu)\n",
                    sv.size(), sizes.size(), tris, lv.size() / 3);
        verdict(sum == (int)sv.size() && mult3 && tris == (int)(lv.size() / 3) && shades,
                "the rail strips hold exactly the list's triangles, in palette colours");
    }
    // A tram street: two tracks, every rail flush, 3 m apart.
    {
        std::vector<SceneObject> t;
        t.push_back(object("tram", {-30, 0, 30, 0}, 12.0f));
        t.back().roadKind = roadrail::kTram;
        t.back().roadTracks = 2;
        const std::vector<roadgen::CrossingRoad> tr = project::crossingRoads(t);
        roadgen::CrossingPlan tp;
        roadgen::Surface ts;
        build(tr, tp, ts);
        const std::vector<roadrail::RailPiece> tramRails =
            roadrail::planRails(tr, tp, [&](float x, float z) { return ts.at(x, z); }, flat);
        std::vector<float> zs;
        bool allFlush = true;
        for (const roadrail::RailPiece& p : tramRails) {
            allFlush &= p.profile == roadrail::kProfileFlush;
            zs.push_back(p.pts[2]);
        }
        std::sort(zs.begin(), zs.end());
        const bool spaced = zs.size() == 4 && std::fabs(zs[0] + 1.5f + half) < 1e-3f &&
                            std::fabs(zs[1] + 1.5f - half) < 1e-3f &&
                            std::fabs(zs[2] - 1.5f + half) < 1e-3f &&
                            std::fabs(zs[3] - 1.5f - half) < 1e-3f;
        std::printf("  tram: %zu rail line(s)%s\n", tramRails.size(),
                    allFlush ? ", all flush" : "");
        verdict(allFlush && spaced, "a two-track tram street gets four flush rails, 3 m apart");
    }
    // The ballast texture: a pure function of the recipe that tiles along V,
    // seven sleepers per repeat down the track's centre.
    {
        roadtex::RoadTexParams b;
        b.surface = roadtex::kBallast;
        b.lanes = 0;
        b.edge.style = roadtex::kLineNone;
        const std::vector<unsigned char> a = roadtex::generate(b), a2 = roadtex::generate(b);
        roadtex::RoadTexParams c = b;
        c.sleepers = roadtex::kSleepersConcrete;
        const std::vector<unsigned char> cc = roadtex::generate(c);
        const int n = 128;
        auto px = [&](const std::vector<unsigned char>& img, int x, int y, int ch) {
            return (int)img[((size_t)y * n + x) * 4 + ch];
        };
        // Timber is warm brown, clearly redder than the grey stones; a
        // sleeper is ~8 texels long down V, a warm stone 2-3.
        int runs = 0, len = 0;
        for (int y = 0; y <= n; ++y) {
            const bool sleeper = y < n && px(a, n / 2, y, 0) - px(a, n / 2, y, 2) > 14;
            if (sleeper) {
                ++len;
            } else {
                runs += len >= 5 ? 1 : 0;
                len = 0;
            }
        }
        // Wrap seam: the first and last rows differ no more than neighbours do.
        auto rowDiff = [&](int y0, int y1) {
            long d = 0;
            for (int x = 0; x < n; ++x)
                for (int ch = 0; ch < 3; ++ch) d += std::abs(px(a, x, y0, ch) - px(a, x, y1, ch));
            return d;
        };
        long inner = 0;
        for (int y = 1; y < n; ++y) inner = std::max(inner, rowDiff(y - 1, y));
        const long seam = rowDiff(n - 1, 0);
        const std::string text = roadtex::toText(c);
        const roadtex::RoadTexParams back = roadtex::fromText(text);
        std::printf("  ballast: %d sleeper run(s) down the centre, seam diff %ld (rows %ld)\n",
                    runs, seam, inner);
        verdict(a == a2 && a != cc && runs == roadtex::kSleepersPerRepeat && seam <= inner &&
                    back == c,
                "the ballast texture is deterministic, tiles, and round-trips its recipe");
    }
}

// Road details (docs/roads.md "Road details"): a kerbed T with zebras plus a
// kerbless cross street, on rolling ground. The decals must be deterministic,
// follow the seed and the density, stay on their own road's opaque core, keep
// clear of node patches, paint and other roads, put gullies only along kerbs,
// and lie kDetailLift over the drawn surface everywhere, not just at corners.
void roadDetails() {
    std::printf("-- road details --\n");
    auto road = [](const char* id, std::vector<float> pts, float width, bool kerb,
                   float details) {
        roadgen::CrossingRoad r;
        r.id = id;
        r.points = std::move(pts);
        r.width = width;
        r.intersection = "res/materials/x.mtl";
        r.kerb = kerb;
        r.markings = roadgen::kMarkCrossings;
        r.details = details;
        return r;
    };
    // Rolling ground with a fold, so a decal must split to follow it.
    const roadgen::HeightFn ground = [](float x, float z) {
        return 0.8f * std::sin(x * 0.11f) + 0.5f * std::cos(z * 0.07f) +
               0.3f * std::fabs(std::sin(x * 0.05f + z * 0.03f));
    };
    struct Scene {
        roadgen::CrossingPlan plan;
        roadgen::Surface surface;  // roads + patches: what is drawn
        roadgen::Surface patchOnly, paintOnly;
        std::vector<roadgen::Vertex> patches, paint;
        roaddetail::Result res;
    };
    auto bake = [&](const std::vector<roadgen::CrossingRoad>& r, Scene& s) {
        s.plan = roadgen::planCrossings(r, {});
        std::vector<roadgen::Vertex> roadTris;
        for (const roadgen::CrossingRoad& rd : r) {
            std::vector<roadgen::Vertex> mesh;
            roadgen::tessellate(rd.points, rd.width,
                                [&](float x, float z) { return ground(x, z) + roadgen::rankLift(rd.rank); },
                                mesh, {}, rd.sampleStep);
            roadTris.insert(roadTris.end(), mesh.begin(), mesh.end());
        }
        s.patches.clear();
        for (const roadgen::Crossing& c : s.plan.crossings) {
            if (c.kind != roadgen::kCrossPatch || c.patchDuplicate) continue;
            std::vector<roadgen::Vertex> mesh;
            roadgen::tessellateJunctionSurface(c.shape, roadTris, ground, c.lift, mesh);
            s.patches.insert(s.patches.end(), mesh.begin(), mesh.end());
        }
        s.surface = roadgen::Surface();
        s.surface.add(roadTris);
        s.surface.add(s.patches);
        s.surface.build();
        roadgen::bakeMarkings(s.plan, r, s.surface, s.paint);
        s.patchOnly = roadgen::Surface();
        s.patchOnly.add(s.patches);
        s.patchOnly.build();
        s.paintOnly = roadgen::Surface();
        s.paintOnly.add(s.paint);
        s.paintOnly.build();
        roaddetail::SceneInput in;
        in.roads = &r;
        in.plan = &s.plan;
        in.ground = ground;
        in.patches = s.patches;
        in.paint = s.paint;
        s.res = roaddetail::build(in);
    };
    const std::vector<roadgen::CrossingRoad> roads = {
        road("main", {-120, 0, 0, 2, 120, 0}, 12, true, 1.0f),
        road("stem", {0, 0, 0, 90}, 9, true, 1.0f),
        road("cross", {60, -80, 62, 80}, 8, false, 1.0f)};
    Scene a, b;
    bake(roads, a);
    bake(roads, b);
    int perKind[roaddetail::kKindCount] = {};
    for (const roaddetail::Decal& d : a.res.decals) ++perKind[d.kind];
    std::printf("  %zu decals (%d manholes, %d gullies, %d patches, %d cracks, %d stains), %zu "
                "vertices in %zu chunks; %d of %d candidates rejected\n",
                a.res.decals.size(), perKind[roaddetail::kManholeRound] +
                                         perKind[roaddetail::kManholeSquare],
                perKind[roaddetail::kGully], perKind[roaddetail::kPatch],
                perKind[roaddetail::kCrack], perKind[roaddetail::kStain], a.res.tris.size(),
                a.res.chunkSizes.size(), a.res.rejected, a.res.candidates);
    bool same = a.res.tris.size() == b.res.tris.size();
    for (size_t i = 0; same && i < a.res.tris.size(); ++i)
        same = a.res.tris[i].x == b.res.tris[i].x && a.res.tris[i].y == b.res.tris[i].y &&
               a.res.tris[i].z == b.res.tris[i].z && a.res.tris[i].u == b.res.tris[i].u &&
               a.res.tris[i].v == b.res.tris[i].v;
    verdict(same && !a.res.tris.empty(), "the same roads bake the same decals, bit for bit");
    bool allKinds = true;
    for (int k = 0; k < roaddetail::kKindCount; ++k)
        if (k != roaddetail::kManholeSquare) allKinds &= perKind[k] > 0;
    verdict(allKinds, "every kind of detail is placed on a dense street");

    // On the road, off the patches and the paint, kDetailLift over the surface.
    float worstErr = 0.0f, lowest = 1e30f;
    int offRoad = 0, onPatch = 0, onPaint = 0;
    for (size_t t = 0; t + 2 < a.res.tris.size(); t += 3) {
        const roadgen::Vertex* q = &a.res.tris[t];
        static const float bary[10][3] = {
            {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.34f, 0.33f, 0.33f}, {0.5f, 0.5f, 0},
            {0, 0.5f, 0.5f}, {0.5f, 0, 0.5f}, {0.7f, 0.2f, 0.1f}, {0.1f, 0.7f, 0.2f},
            {0.2f, 0.1f, 0.7f}};
        for (const auto& w : bary) {
            const float x = w[0] * q[0].x + w[1] * q[1].x + w[2] * q[2].x;
            const float y = w[0] * q[0].y + w[1] * q[1].y + w[2] * q[2].y;
            const float z = w[0] * q[0].z + w[1] * q[1].z + w[2] * q[2].z;
            const float s = a.surface.at(x, z);
            if (s == roadgen::Surface::kNone) {
                ++offRoad;
                continue;
            }
            worstErr = std::max(worstErr, std::fabs(y - s - roaddetail::kDetailLift));
            lowest = std::min(lowest, y - s);
            onPatch += a.patchOnly.at(x, z) != roadgen::Surface::kNone ? 1 : 0;
            onPaint += a.paintOnly.at(x, z) != roadgen::Surface::kNone ? 1 : 0;
        }
    }
    std::printf("  surface: worst height error %.4f, lowest clearance %.4f; %d samples off the "
                "road, %d on a node patch, %d on paint\n",
                worstErr, lowest, offRoad, onPatch, onPaint);
    verdict(offRoad == 0, "no decal hangs off its road");
    verdict(onPatch == 0, "no decal lies on a node patch");
    verdict(onPaint == 0, "no decal lies under a stop line or a zebra");
    verdict(worstErr <= 0.01f && lowest > 0.015f,
            "decals follow the drawn surface kDetailLift above it (within 1 cm)");
    // Gullies only along kerbs, just inside the edge.
    bool gullyOk = true;
    for (const roaddetail::Decal& d : a.res.decals) {
        if (d.kind != roaddetail::kGully) continue;
        const roadgen::CrossingRoad& r = roads[(size_t)d.road];
        gullyOk &= r.kerb;
    }
    verdict(gullyOk && perKind[roaddetail::kGully] > 0, "gullies only on kerbed roads");
    // Chunks: whole triangles within the budget, summing to the list.
    int sum = 0;
    bool chunksOk = true;
    for (int sz : a.res.chunkSizes) {
        sum += sz;
        chunksOk &= sz % 3 == 0 && sz > 0 && sz <= roaddetail::kChunkBudget;
    }
    verdict(chunksOk && sum == (int)a.res.tris.size(), "chunks hold whole triangles within budget");
    // The seed and the density.
    std::vector<roadgen::CrossingRoad> reseeded = roads;
    for (roadgen::CrossingRoad& r : reseeded) r.detailSeed = 7;
    Scene c;
    bake(reseeded, c);
    bool differs = c.res.tris.size() != a.res.tris.size();
    for (size_t i = 0; !differs && i < a.res.tris.size(); ++i)
        differs = c.res.tris[i].x != a.res.tris[i].x;
    verdict(differs, "another seed is another arrangement");
    std::vector<roadgen::CrossingRoad> sparse = roads;
    for (roadgen::CrossingRoad& r : sparse) r.details = 0.25f;
    Scene d;
    bake(sparse, d);
    std::vector<roadgen::CrossingRoad> none = roads;
    for (roadgen::CrossingRoad& r : none) r.details = 0.0f;
    Scene e;
    bake(none, e);
    std::printf("  density 1: %zu decals, 0.25: %zu, 0: %zu\n", a.res.decals.size(),
                d.res.decals.size(), e.res.decals.size());
    verdict(d.res.decals.size() < a.res.decals.size() && !d.res.decals.empty() &&
                e.res.decals.empty() && e.res.tris.empty(),
            "fewer details at a lower density, none at 0");
    // The atlas: 16 RGBA entries, so the 4-bit bake is lossless.
    const std::vector<unsigned char> atlas = roaddetail::generateAtlas();
    const std::vector<unsigned char> q = pngquant::quantizePreviewRGBA(
        atlas.data(), roaddetail::kAtlasSize, roaddetail::kAtlasSize, 16,
        pngquant::Dither::FloydSteinberg);
    size_t diff = 0, used = 0;
    for (size_t i = 0; i < atlas.size() && i < q.size(); ++i) diff += atlas[i] != q[i] ? 1 : 0;
    for (size_t i = 3; i < atlas.size(); i += 4) used += atlas[i] != 0 ? 1 : 0;
    std::printf("  atlas: %d x %d, %zu of %d texels opaque or soft, %zu channel bytes changed by "
                "4-bit quantization\n",
                roaddetail::kAtlasSize, roaddetail::kAtlasSize, used,
                roaddetail::kAtlasSize * roaddetail::kAtlasSize, diff);
    verdict(q.size() == atlas.size() && diff == 0 && roaddetail::palette().size() == 16,
            "the details atlas survives the 4-bit bake unchanged");
}

// The pedals: R2 gas, L2 brake-then-reverse (vehiclesim::pedals, the rule the
// console's controller and the test drive share).
void pedalsCheck() {
    std::printf("-- pedals --\n");
    DriveSpec s;
    // From a standstill, holding L2 alone drives the car backwards.
    DriveState st;
    st.pos[1] = s.rideHeight;
    for (int i = 0; i < 150; ++i) {
        DriveInput in;
        pedals(st.speed, 0.0f, 1.0f, in);
        step(s, in, 1.0f / 50.0f, flat, st);
    }
    std::printf("  L2 from a standstill: speed %.2f after 3 s\n", st.speed);
    verdict(st.speed < -2.0f, "L2 held at a standstill reverses");
    // Rolling forward, L2 brakes to a stop FIRST, and only then reverses.
    DriveState f;
    f.pos[1] = s.rideHeight;
    for (int i = 0; i < 150; ++i) {
        DriveInput in;
        pedals(f.speed, 1.0f, 0.0f, in);
        step(s, in, 1.0f / 50.0f, flat, f);
    }
    const float v0 = f.speed;
    float minBack = 0.0f;
    int framesToStop = -1;
    for (int i = 0; i < 300; ++i) {
        DriveInput in;
        pedals(f.speed, 0.0f, 1.0f, in);
        step(s, in, 1.0f / 50.0f, flat, f);
        if (framesToStop < 0 && f.speed <= kPedalStop) framesToStop = i;
        minBack = std::min(minBack, f.speed);
        if (framesToStop < 0 && in.throttle < 0.0f) {
            verdict(false, "L2 never reverses while the car still rolls forward");
            return;
        }
    }
    std::printf("  forward %.1f u/s: stopped after %d frames, then reversed to %.2f\n", v0,
                framesToStop, minBack);
    verdict(framesToStop > 0 && framesToStop < 100, "L2 brakes a forward-rolling car");
    verdict(minBack < -2.0f, "and, once stopped, the same L2 reverses");
    // Rolling backwards, R2 is the brake.
    DriveInput in;
    pedals(-3.0f, 1.0f, 0.0f, in);
    verdict(in.brake > 0.9f && in.throttle == 0.0f, "R2 brakes a car rolling backwards");
}

}  // namespace

// 12. Speed feel (docs/vehicles.md, "Speed feel"): nothing below the start,
//     everything at the top speed and past it (a nitrous run), monotonic in
//     between, the same either way the car is moving, and a start at 0 still
//     begins at a standstill instead of dividing by nothing.
void speedFeelCurve() {
    std::printf("-- speed feel --\n");
    DriveSpec s;
    s.topSpeed = 30.0f;
    s.feelFrom = 0.5f;
    verdict(speedFeel(s, 0.0f) == 0.0f && speedFeel(s, 14.9f) == 0.0f,
            "no feel below feelFrom of the top speed");
    verdict(speedFeel(s, 30.0f) == 1.0f && speedFeel(s, 45.0f) == 1.0f,
            "full feel at the top speed and past it on nitrous");
    bool mono = true;
    float prev = 0.0f;
    for (int i = 0; i <= 60; ++i) {
        const float k = speedFeel(s, (float)i * 0.5f);
        if (k < prev - 1e-6f || k < 0.0f || k > 1.0f) mono = false;
        prev = k;
    }
    std::printf("  at 75%% of top: %.3f\n", speedFeel(s, 22.5f));
    verdict(mono, "the feel grows monotonically inside 0..1");
    verdict(speedFeel(s, -25.0f) == speedFeel(s, 25.0f), "reverse feels like forward");
    s.feelFrom = 0.0f;
    verdict(speedFeel(s, 0.0f) == 0.0f && speedFeel(s, 15.0f) > 0.4f,
            "feelFrom 0 starts at a standstill");
}

int run() {
    std::printf("vehicle-check: vehiclesim property tests\n");
    gearGeometry();
    preGearboxRegression();
    powerFadeResponse();
    noHunting();
    walls();
    lean();
    hill();
    roughRide();
    analyticWheelRig();
    terrainStability();
    pedalsCheck();
    handling();
    offroad();
    crossings();
    junctionOverrides();
    roadNodes();
    roadKerbs();
    roadTextures();
    roadRails();
    roadbridge::check(verdict);  // docs/roads.md "Bridges"
    roadDetails();
    roadfurn::check(verdict);  // docs/roads.md "Street furniture"
    roadstream::check(verdict);  // docs/roads.md "Road streaming"
    roadfile::check(verdict);  // docs/roads.md "Tables on disk"
    damage();
    pieces();
    speedFeelCurve();
    if (failures) {
        std::printf("vehicle-check: %d FAILURE(S)\n", failures);
        return 1;
    }
    std::printf("vehicle-check: all properties hold\n");
    return 0;
}

}  // namespace vehcheck
