#include "vehiclesim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vehiclesim {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;

// Two AABB extents count as the same shape within this relative tolerance.
// Mirrored left/right wheels are rarely bit-identical after an exporter has
// been through them, and a rim swapped between axles is a legitimate vehicle.
constexpr float kSizeTol = 0.10f;

// How fast the driven wheels' surface speed chases what the drive asks for,
// units/s per second. Presentation only - it shapes how quickly the engine note
// flares and dies, and nothing the car does depends on it.
constexpr float kWheelSpinRate = 40.0f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float approach(float v, float target, float rate) {
    if (v < target) return std::min(target, v + rate);
    return std::max(target, v - rate);
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// Does any of this node's text (name or material names) suggest a wheel?
// Only ever a BONUS: the first real asset this was written against names its
// wheels "Cylinder.001" and would score zero here, which is exactly why the
// geometric signals below have to be able to carry the decision alone.
bool textSaysWheel(const MeshNode& n) {
    const char* words[] = {"wheel", "tyre", "tire", "rim", "rueda", "kolo", "koło"};
    std::string all = lower(n.name);
    for (const std::string& m : n.materials) all += " " + lower(m);
    for (const char* w : words)
        if (contains(all, w)) return true;
    return false;
}

// "front"/"rear" stated by a node or its materials. 1 = front, -1 = rear,
// 0 = says nothing. Checked before any geometric guess about which end is
// the nose, because an author who named their nodes deserves to be obeyed.
int textSaysEnd(const MeshNode& n) {
    std::string all = lower(n.name);
    for (const std::string& m : n.materials) all += " " + lower(m);
    // Whole-ish words first; "fl"/"fr" only as a suffix-ish token, because
    // "fl" matches "flange" and half the noun in "roof_left".
    if (contains(all, "front") || contains(all, "przod") || contains(all, "przód"))
        return 1;
    if (contains(all, "rear") || contains(all, "back") || contains(all, "tyl") ||
        contains(all, "tył"))
        return -1;
    return 0;
}

struct Cluster {
    std::vector<int> members;
    float ext[3] = {0.0f, 0.0f, 0.0f};  // mean extents
    int axleAxis = 2, upAxis = 1, fwdAxis = 0;
    float score = 0.0f;
};

}  // namespace

Detection detectWheels(const std::vector<MeshNode>& nodes) {
    Detection det;

    // Only geometry participates. Cameras, lights and empties carry no
    // vertices and never reach any of the reasoning below.
    std::vector<int> geo;
    for (int i = 0; i < (int)nodes.size(); ++i)
        if (nodes[i].vertexCount > 0) geo.push_back(i);

    if (geo.size() < 2) {
        det.bodyNodes = geo;
        det.notes.push_back("Fewer than two meshes with geometry - nothing to "
                            "match a wheel cluster against.");
        return det;
    }

    // Whole-model bounds: "wheels sit low" and "wheels are small" are both
    // relative statements and need something to be relative to.
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (int i : geo)
        for (int a = 0; a < 3; ++a) {
            mn[a] = std::min(mn[a], nodes[i].mn[a]);
            mx[a] = std::max(mx[a], nodes[i].mx[a]);
        }

    // --- cluster by shape ---------------------------------------------------
    // Greedy: every unassigned node seeds a cluster and pulls in every other
    // unassigned node of the same size. Grouping on SIZE rather than on vertex
    // count is deliberate - a car whose front and rear rims are different
    // meshes is still a car, and identical vertex counts are only a bonus.
    std::vector<bool> taken(nodes.size(), false);
    std::vector<Cluster> clusters;
    for (int i : geo) {
        if (taken[i]) continue;
        Cluster c;
        c.members.push_back(i);
        taken[i] = true;
        for (int j : geo) {
            if (taken[j]) continue;
            bool same = true;
            for (int a = 0; a < 3; ++a) {
                const float ea = nodes[i].extent(a), eb = nodes[j].extent(a);
                const float big = std::max(std::fabs(ea), std::fabs(eb));
                if (big > 1e-6f && std::fabs(ea - eb) / big > kSizeTol) same = false;
            }
            if (same) {
                c.members.push_back(j);
                taken[j] = true;
            }
        }
        for (int a = 0; a < 3; ++a) {
            float sum = 0.0f;
            for (int m : c.members) sum += nodes[m].extent(a);
            c.ext[a] = sum / (float)c.members.size();
        }
        clusters.push_back(std::move(c));
    }

    // --- score each cluster -------------------------------------------------
    Cluster* best = nullptr;
    for (Cluster& c : clusters) {
        const int n = (int)c.members.size();
        if (n != 2 && n != 4 && n != 6) continue;  // three wheels is not a car

        // The axle is the axis a wheel is THINNEST along: a wheel's box is
        // diameter x diameter x width. This is what makes the frame derivable
        // from the wheels alone, with no exporter axis metadata consulted -
        // Blender, Maya and 3ds Max disagree about that metadata and a wheel
        // cluster does not.
        c.axleAxis = 0;
        for (int a = 1; a < 3; ++a)
            if (c.ext[a] < c.ext[c.axleAxis]) c.axleAxis = a;

        const int o1 = (c.axleAxis + 1) % 3, o2 = (c.axleAxis + 2) % 3;

        // Of the two remaining axes, the wheels' CENTRES spread along forward
        // (the wheelbase) and barely at all along up - they all sit on the
        // ground. That resolves up vs forward without another assumption.
        float spread[3] = {0.0f, 0.0f, 0.0f};
        for (int a : {o1, o2}) {
            float lo = 1e30f, hi = -1e30f;
            for (int m : c.members) {
                lo = std::min(lo, nodes[m].centre(a));
                hi = std::max(hi, nodes[m].centre(a));
            }
            spread[a] = hi - lo;
        }
        c.fwdAxis = spread[o1] >= spread[o2] ? o1 : o2;
        c.upAxis = c.fwdAxis == o1 ? o2 : o1;

        float s = 0.0f;
        s += n == 4 ? 4.0f : (n == 6 ? 3.0f : 1.5f);

        // Round: the two non-axle extents of a wheel are its diameter twice.
        const float ea = c.ext[o1], eb = c.ext[o2];
        const float big = std::max(ea, eb);
        if (big > 1e-6f && std::fabs(ea - eb) / big < 0.15f) s += 2.5f;

        // Thin: narrower across the axle than it is tall.
        if (big > 1e-6f && c.ext[c.axleAxis] / big < 0.85f) s += 1.5f;

        // Low: the cluster sits in the bottom of the model.
        float cUp = 0.0f;
        for (int m : c.members) cUp += nodes[m].centre(c.upAxis);
        cUp /= (float)n;
        const float upSpan = mx[c.upAxis] - mn[c.upAxis];
        if (upSpan > 1e-6f && (cUp - mn[c.upAxis]) / upSpan < 0.45f) s += 2.0f;

        // Small: a wheel is a fraction of the vehicle, not most of it.
        const float fwdSpan = mx[c.fwdAxis] - mn[c.fwdAxis];
        if (fwdSpan > 1e-6f && c.ext[c.fwdAxis] / fwdSpan < 0.4f) s += 1.0f;

        // Same mesh reused - the usual way wheels are authored.
        bool sameVerts = true;
        for (int m : c.members)
            if (nodes[m].vertexCount != nodes[c.members[0]].vertexCount)
                sameVerts = false;
        if (sameVerts) s += 1.0f;

        // Text, last and weakest.
        int textHits = 0;
        for (int m : c.members)
            if (textSaysWheel(nodes[m])) ++textHits;
        if (textHits > 0) s += 2.0f;

        c.score = s;
        if (!best || s > best->score) best = &c;
    }

    // Below this a "cluster" is two unrelated props that happen to be the same
    // size. Reporting no wheels is a recoverable state; inventing four is not.
    constexpr float kAccept = 7.0f;
    if (!best || best->score < kAccept) {
        det.bodyNodes = geo;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "No wheel cluster found (best score %.1f of %.1f needed) - "
                      "the whole model is treated as the body.",
                      best ? best->score : 0.0f, kAccept);
        det.notes.push_back(buf);
        return det;
    }

    det.found = true;
    det.forwardAxis = best->fwdAxis;
    det.upAxis = best->upAxis;
    det.axleAxis = best->axleAxis;

    const char* axisName[3] = {"X", "Y", "Z"};
    {
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "Wheel cluster: %d meshes, score %.1f. Axle axis %s "
                      "(thinnest), up %s, forward %s (widest centre spread).",
                      (int)best->members.size(), best->score,
                      axisName[det.axleAxis], axisName[det.upAxis],
                      axisName[det.forwardAxis]);
        det.notes.push_back(buf);
    }

    // --- which end is the nose ---------------------------------------------
    // A named node wins outright. Otherwise the shorter body overhang past the
    // axle is taken as the front - a weak convention, true rather more often
    // than not, and flagged as an assumption either way so the panel offers
    // the flip. Getting this wrong makes the car drive backwards, which is the
    // most likely wrong answer this whole file can produce.
    float fwdCentres[8];
    int fwdCount = 0;
    for (int m : best->members)
        fwdCentres[fwdCount++] = nodes[m].centre(det.forwardAxis);
    float axleLo = 1e30f, axleHi = -1e30f;
    for (int i = 0; i < fwdCount; ++i) {
        axleLo = std::min(axleLo, fwdCentres[i]);
        axleHi = std::max(axleHi, fwdCentres[i]);
    }

    int named = 0;
    for (int m : best->members) {
        const int e = textSaysEnd(nodes[m]);
        if (e == 0) continue;
        const bool atHi = nodes[m].centre(det.forwardAxis) > 0.5f * (axleLo + axleHi);
        named = (e == 1) == atHi ? 1 : -1;
        break;
    }

    if (named != 0) {
        det.forwardSign = named;
        det.frontAssumed = false;
        det.notes.push_back("Front/rear taken from the node names.");
    } else {
        const float overhangHi = mx[det.forwardAxis] - axleHi;
        const float overhangLo = axleLo - mn[det.forwardAxis];
        det.forwardSign = overhangHi <= overhangLo ? 1 : -1;
        det.frontAssumed = true;
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "Nothing names the front. Assuming +%s (body overhang "
                      "%.2f past that axle vs %.2f past the other) - flip it if "
                      "the car drives backwards.",
                      axisName[det.forwardAxis],
                      det.forwardSign > 0 ? overhangHi : overhangLo,
                      det.forwardSign > 0 ? overhangLo : overhangHi);
        det.notes.push_back(buf);
    }

    // --- sides --------------------------------------------------------------
    // right = up x forward, so "left" needs no second convention to disagree
    // with the one the runtime uses.
    float upV[3] = {0, 0, 0}, fwdV[3] = {0, 0, 0}, rightV[3] = {0, 0, 0};
    upV[det.upAxis] = 1.0f;
    fwdV[det.forwardAxis] = (float)det.forwardSign;
    rightV[0] = upV[1] * fwdV[2] - upV[2] * fwdV[1];
    rightV[1] = upV[2] * fwdV[0] - upV[0] * fwdV[2];
    rightV[2] = upV[0] * fwdV[1] - upV[1] * fwdV[0];

    const float axleMid = 0.5f * (axleLo + axleHi);
    float sideMid = 0.0f;
    for (int m : best->members)
        sideMid += nodes[m].centre(0) * rightV[0] + nodes[m].centre(1) * rightV[1] +
                   nodes[m].centre(2) * rightV[2];
    sideMid /= (float)best->members.size();

    for (int m : best->members) {
        Wheel w;
        w.node = m;
        w.nodeName = nodes[m].name;
        for (int a = 0; a < 3; ++a) w.centre[a] = nodes[m].centre(a);
        const int o1 = (det.axleAxis + 1) % 3, o2 = (det.axleAxis + 2) % 3;
        w.radius = 0.25f * (nodes[m].extent(o1) + nodes[m].extent(o2));
        w.width = nodes[m].extent(det.axleAxis);
        const float alongFwd = nodes[m].centre(det.forwardAxis);
        w.front = (alongFwd > axleMid) == (det.forwardSign > 0);
        const float alongRight = w.centre[0] * rightV[0] + w.centre[1] * rightV[1] +
                                 w.centre[2] * rightV[2];
        w.left = alongRight < sideMid;
        // Seeded, not decided: front steers, rear drives. Both are ordinary
        // per-wheel checkboxes in the editor, because a rear-steer forklift
        // and a 4WD are the same asset with two boxes ticked differently.
        w.steered = w.front;
        w.driven = !w.front;
        det.wheels.push_back(w);
    }

    // Front-left, front-right, rear-left, rear-right - the order every
    // consumer indexes by.
    std::stable_sort(det.wheels.begin(), det.wheels.end(),
                     [](const Wheel& a, const Wheel& b) {
                         if (a.front != b.front) return a.front;
                         return a.left && !b.left;
                     });

    // --- the numbers the author no longer has to type ------------------------
    float frontF = 0.0f, rearF = 0.0f, leftS = 0.0f, rightS = 0.0f, rad = 0.0f;
    int nf = 0, nr = 0, nl = 0, nrr = 0;
    for (const Wheel& w : det.wheels) {
        const float f = w.centre[det.forwardAxis];
        const float s = w.centre[0] * rightV[0] + w.centre[1] * rightV[1] +
                        w.centre[2] * rightV[2];
        if (w.front) frontF += f, ++nf;
        else rearF += f, ++nr;
        if (w.left) leftS += s, ++nl;
        else rightS += s, ++nrr;
        rad += w.radius;
    }
    if (nf) frontF /= (float)nf;
    if (nr) rearF /= (float)nr;
    if (nl) leftS /= (float)nl;
    if (nrr) rightS /= (float)nrr;
    det.wheelBase = (nf && nr) ? std::fabs(frontF - rearF) : 0.0f;
    det.track = (nl && nrr) ? std::fabs(rightS - leftS) : 0.0f;
    det.radius = det.wheels.empty() ? 0.0f : rad / (float)det.wheels.size();

    {
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "Wheelbase %.3f, track %.3f, wheel radius %.3f "
                      "(model units).",
                      det.wheelBase, det.track, det.radius);
        det.notes.push_back(buf);
    }

    for (int i : geo) {
        bool isWheel = false;
        for (const Wheel& w : det.wheels)
            if (w.node == i) isWheel = true;
        if (!isWheel) det.bodyNodes.push_back(i);
    }
    return det;
}

std::vector<SpecField> specFields(DriveSpec& s) {
    // Ranges are what the editor's sliders offer, so they bound what an author
    // can reach with the mouse - not what the sim tolerates. Keys are format.
    return {
        {"wheelBase", &s.wheelBase, 0.5f, 12.0f, "Wheelbase",
         "Front axle to rear axle. The turn radius follows from it, so a longer "
         "vehicle turns wider on its own."},
        {"track", &s.track, 0.4f, 6.0f, "Track", "Left wheel to right wheel."},
        {"wheelRadius", &s.wheelRadius, 0.05f, 2.0f, "Wheel radius",
         "Measured off the baked wheel; drives ride height and how fast the "
         "wheels appear to spin."},
        {"topSpeed", &s.topSpeed, 1.0f, 80.0f, "Top speed", "Units per second, forward."},
        {"reverseTopSpeed", &s.reverseTopSpeed, 0.5f, 30.0f, "Reverse top speed", ""},
        {"accel", &s.accel, 0.5f, 40.0f, "Acceleration", "Units per second squared at full throttle."},
        {"brakeDecel", &s.brakeDecel, 1.0f, 60.0f, "Braking", "Units per second squared."},
        {"engineBraking", &s.engineBraking, 0.0f, 20.0f, "Engine braking",
         "How fast it slows with no throttle and no brake."},
        {"drag", &s.drag, 0.0f, 0.02f, "Drag",
         "Quadratic air resistance - what actually caps the top speed on a downhill."},
        {"maxSteerDeg", &s.maxSteerDeg, 5.0f, 60.0f, "Steering lock", "Degrees at a standstill."},
        {"highSpeedSteerDeg", &s.highSpeedSteerDeg, 2.0f, 60.0f, "Lock at top speed",
         "The lock shrinks to this as the car reaches top speed. Without the "
         "taper a full-lock flick at speed spins the car on the spot."},
        {"steerRateDeg", &s.steerRateDeg, 20.0f, 900.0f, "Steering rate",
         "How fast the wheels turn, degrees per second."},
        {"steerReturnDeg", &s.steerReturnDeg, 20.0f, 900.0f, "Self-centring",
         "How fast the wheels straighten with no steering input."},
        {"grip", &s.grip, 0.5f, 80.0f, "Grip",
         "The cap on how fast the tyres kill sideways slip. Low slides, high is on rails."},
        {"handbrakeGrip", &s.handbrakeGrip, 0.0f, 40.0f, "Handbrake grip",
         "Replaces grip while the handbrake is held - this is the drift knob."},
        {"gravity", &s.gravity, 1.0f, 80.0f, "Gravity", "Units per second squared."},
        {"rideHeight", &s.rideHeight, 0.0f, 3.0f, "Ride height",
         "Chassis origin above the contact plane. Seeded from the wheel radius."},
        {"suspensionTravel", &s.suspensionTravel, 0.0f, 1.0f, "Suspension travel",
         "How far a wheel moves against the body over bumps. Visual only."},
        {"suspensionRate", &s.suspensionRate, 0.5f, 40.0f, "Suspension rate",
         "How quickly the wheels follow the ground. Visual only."},
        {"maxSlopeCos", &s.maxSlopeCos, 0.0f, 1.0f, "Slope grip limit",
         "Past this steepness the tyres start losing grip instead of climbing."},
        {"mass", &s.mass, 0.1f, 200.0f, "Mass",
         "Relative, and only used where the vehicle shoves a physics body."},
        {"leanAmount", &s.leanAmount, 0.0f, 2.0f, "Body lean",
         "Weight transfer: squat, dive and corner roll. 0 is a kart on "
         "rails, 2 an American sofa. Visual only."},
        {"gears", &s.gears, 1.0f, 8.0f, "Gears",
         "Forward gears. The top one reaches the top speed; each one below it "
         "reaches that divided by the spread."},
        {"gearSpread", &s.gearSpread, 1.1f, 2.5f, "Gear spread",
         "The ratio between consecutive gears' top speeds. Wide spread = fewer, "
         "longer gears."},
        {"idleRpm", &s.idleRpm, 300.0f, 2000.0f, "Idle RPM",
         "Where the engine sits at a standstill, and the pitch a drive starts at."},
        {"redlineRpm", &s.redlineRpm, 2000.0f, 12000.0f, "Redline RPM",
         "Reached at the top of every gear, so it is also the highest pitch the "
         "engine sound plays at."},
        {"shiftUpFrac", &s.shiftUpFrac, 0.5f, 1.0f, "Shift up at",
         "Fraction of the redline where it changes up."},
        {"shiftDownFrac", &s.shiftDownFrac, 0.1f, 0.9f, "Shift down at",
         "Fraction of the redline where it changes down. Clamped below the "
         "up-shift landing point, so the box cannot hunt between two gears."},
        {"shiftTime", &s.shiftTime, 0.0f, 0.6f, "Shift time",
         "Seconds of throttle cut per gear change - the audible gap. 0 keeps the "
         "gearbox purely presentational."},
        {"gearTorque", &s.gearTorque, 0.0f, 1.0f, "Gear torque",
         "How much the gear shapes acceleration. 0 pulls the same in every gear; "
         "1 is fully geared, normalised so the car's overall performance does not "
         "change."},
        {"nosCapacity", &s.nosCapacity, 0.0f, 20.0f, "Nitrous seconds",
         "Seconds of full boost the tank holds. 0 means this vehicle has no "
         "nitrous at all."},
        {"nosBoost", &s.nosBoost, 0.0f, 3.0f, "Nitrous boost",
         "Extra acceleration while boosting, as a fraction of the normal figure."},
        {"nosTopSpeed", &s.nosTopSpeed, 1.0f, 2.0f, "Nitrous top speed",
         "Top-speed multiplier while boosting."},
        {"nosRefill", &s.nosRefill, 0.0f, 1.0f, "Nitrous refill",
         "Tank fractions recovered per second while the button is not held."},
    };
}

// ---------------------------------------------------------------------------
// The gearbox
// ---------------------------------------------------------------------------

int gearCount(const DriveSpec& s) {
    int n = (int)(s.gears + 0.5f);
    return n < 1 ? 1 : (n > 8 ? 8 : n);
}

float gearTopSpeed(const DriveSpec& s, int gear) {
    const int n = gearCount(s);
    if (gear < 0) return std::max(s.reverseTopSpeed, 0.001f);
    if (gear > n - 1) gear = n - 1;
    const float spread = clampf(s.gearSpread, 1.01f, 4.0f);
    return std::max(s.topSpeed, 0.001f) / std::pow(spread, (float)(n - 1 - gear));
}

float gearTorqueMul(const DriveSpec& s, int gear) {
    const int n = gearCount(s);
    if (gear < 0) gear = 0;
    if (gear > n - 1) gear = n - 1;
    const float spread = clampf(s.gearSpread, 1.01f, 4.0f);
    // Centred on the middle gear: the exponent runs +/- (n-1)/2, so the
    // multipliers straddle 1.0 instead of all being >= 1. That is what makes
    // `gearTorque` a character knob rather than a power knob.
    const float e = (float)(n - 1 - gear) - 0.5f * (float)(n - 1);
    const float mul = std::pow(spread, e);
    return 1.0f + clampf(s.gearTorque, 0.0f, 1.0f) * (mul - 1.0f);
}

float rpmFor(const DriveSpec& s, float wheelSpeed, int gear) {
    const float top = gearTopSpeed(s, gear);
    const float f = clampf(std::fabs(wheelSpeed) / std::max(top, 0.001f), 0.0f, 1.0f);
    const float idle = std::max(s.idleRpm, 0.0f);
    const float red = std::max(s.redlineRpm, idle + 1.0f);
    return idle + (red - idle) * f;
}

namespace {

// The down-shift point, held below where an up-shift LANDS. An author is free
// to dial the two thresholds into a gearbox that would change up and
// immediately back down for ever; this is what stops it, and it is computed
// rather than validated because a slider that silently misbehaves at one end of
// its range is worse than one that quietly refuses to.
float safeShiftDownFrac(const DriveSpec& s) {
    const float spread = clampf(s.gearSpread, 1.01f, 4.0f);
    const float lands = clampf(s.shiftUpFrac, 0.1f, 1.0f) / spread;
    return std::min(clampf(s.shiftDownFrac, 0.05f, 0.95f), lands - 0.05f);
}

}  // namespace

// ---------------------------------------------------------------------------
// The drive model
// ---------------------------------------------------------------------------
//
// Everything below works in the CANONICAL vehicle frame - forward +Z, up +Y,
// right +X, yaw in degrees about Y. The bake rotates a model out of whatever
// frame it was authored in and into this one exactly once (using Detection's
// axes), so the sim, the viewport preview and the generated runtime never see
// an exporter's opinion about axes again.

void wheelAnchors(const DriveSpec& spec, const DriveState& state, float out[4][3]) {
    const float c = std::cos(state.yaw * kDeg2Rad), s = std::sin(state.yaw * kDeg2Rad);
    const float hx = 0.5f * spec.track, hz = 0.5f * spec.wheelBase;
    // FL, FR, RL, RR - Detection::wheels order.
    const float local[4][2] = {{-hx, hz}, {hx, hz}, {-hx, -hz}, {hx, -hz}};
    for (int i = 0; i < 4; ++i) {
        const float lx = local[i][0], lz = local[i][1];
        out[i][0] = state.pos[0] + lx * c + lz * s;
        out[i][1] = state.pos[1];
        out[i][2] = state.pos[2] - lx * s + lz * c;
    }
}

void step(const DriveSpec& spec, const DriveInput& in, float dt,
          const HeightFn& height, DriveState& state, const SolidFn& solid) {
    // A stalled frame or a paused editor must not tunnel the car through the
    // world; the sim would rather run slow than teleport.
    dt = clampf(dt, 0.0f, 0.05f);
    if (dt <= 0.0f) return;

    // For the weight-transfer lean at the bottom: the speed the frame STARTED
    // with, so the lean reads the acceleration everything below produces -
    // wall hits included, which is what makes the nose dip on impact for free.
    const float speed0 = state.speed;

    // --- steering -----------------------------------------------------------
    // The lock shrinks with speed. Without it a full-lock flick at top speed
    // spins the car on the spot, and a d-pad (which is always full deflection)
    // makes the vehicle undriveable rather than twitchy.
    const float speedFrac = clampf(std::fabs(state.speed) / std::max(spec.topSpeed, 0.001f), 0.0f, 1.0f);
    const float lock = spec.maxSteerDeg +
                       (spec.highSpeedSteerDeg - spec.maxSteerDeg) * speedFrac;
    // NEGATED: DriveInput.steer is "positive = the driver's right", and in
    // the canonical frame (forward +Z, up +Y, right-handed) the body's right
    // is -X - while positive steerAngle/yaw turns toward +X, the LEFT. The
    // original acceptance test verified that yaw CHANGED under stick input,
    // not which way the car went on screen, which is how "stick left, car
    // goes right" shipped and was found by a person, not a harness.
    const float steerIn = clampf(-in.steer, -1.0f, 1.0f);
    if (std::fabs(steerIn) > 0.02f)
        state.steerAngle = approach(state.steerAngle, steerIn * lock,
                                    spec.steerRateDeg * dt);
    else
        state.steerAngle = approach(state.steerAngle, 0.0f, spec.steerReturnDeg * dt);
    state.steerAngle = clampf(state.steerAngle, -lock, lock);

    // --- ground contact -----------------------------------------------------
    // Four samples under the wheels. They give the ride height, the pitch and
    // the roll from one query each - which is the whole reason a heightfield
    // vehicle is affordable at all.
    float anchors[4][3];
    wheelAnchors(spec, state, anchors);
    float gy[4];
    float sum = 0.0f;
    bool anyGround = false;
    for (int i = 0; i < 4; ++i) {
        gy[i] = height ? height(anchors[i][0], anchors[i][2]) : 0.0f;
        // TERRAIN_VOID_Y: a scene with no terrain answers "unreachably low",
        // so "there is no floor here" needs no branch of its own.
        if (gy[i] > -1e5f) anyGround = true;
        sum += gy[i];
    }
    const float planeY = sum * 0.25f;
    const float restY = planeY + spec.rideHeight;

    if (!anyGround) {
        state.grounded = false;
    } else if (state.pos[1] <= restY + 0.02f) {
        state.grounded = true;
    } else {
        state.grounded = false;
    }

    if (state.grounded) {
        // The chassis RIDES the contact plane - no rate limit on this. Smoothing
        // the vertical position here looks like suspension and is not: a car
        // climbing a 25% grade at 17 units/s needs 4.3 units/s of vertical
        // travel, an authored suspension rate supplies about 1.4, and the
        // chassis sinks below the terrain and stays there for the whole climb
        // (measured: y = 4.68 where the ground was 10.16). The ride belongs in
        // wheelCompress, which is presentation and costs the sim nothing.
        state.pos[1] = restY;
        state.velY = 0.0f;

        const float frontY = 0.5f * (gy[0] + gy[1]), rearY = 0.5f * (gy[2] + gy[3]);
        const float leftY = 0.5f * (gy[0] + gy[2]), rightY = 0.5f * (gy[1] + gy[3]);
        const float tPitch = std::atan2(frontY - rearY, std::max(spec.wheelBase, 0.01f)) * kRad2Deg;
        const float tRoll = std::atan2(rightY - leftY, std::max(spec.track, 0.01f)) * kRad2Deg;
        state.pitch = approach(state.pitch, tPitch, 180.0f * dt);
        state.roll = approach(state.roll, tRoll, 180.0f * dt);

        // Compression is each wheel's ground height against the TILTED chassis
        // plane, not against the mean: measured against the mean, a constant
        // slope reads as fully compressed at one axle and fully extended at the
        // other, when the body is in fact riding it level. The residual against
        // the plane the pitch and roll already express is zero on any flat
        // ground at any angle, and non-zero exactly where the ground is bumpy -
        // which is what suspension travel is for.
        const float dPitch = 0.5f * (frontY - rearY), dRoll = 0.5f * (rightY - leftY);
        const float sz[4] = {1.0f, 1.0f, -1.0f, -1.0f};   // FL, FR, RL, RR
        const float sx[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            const float planeAt = planeY + sz[i] * dPitch + sx[i] * dRoll;
            const float resid = clampf((gy[i] - planeAt) / std::max(spec.suspensionTravel, 0.001f),
                                       -1.0f, 1.0f);
            state.wheelCompress[i] =
                approach(state.wheelCompress[i], clampf(0.5f + resid * 0.5f, 0.0f, 1.0f),
                         spec.suspensionRate * dt);
        }
    } else {
        state.velY -= spec.gravity * dt;
        state.pos[1] += state.velY * dt;
        if (anyGround && state.pos[1] < restY) {
            state.pos[1] = restY;
            state.velY = 0.0f;
            state.grounded = true;
        }
        // In the air the body keeps the attitude it left the ground with.
        state.pitch = approach(state.pitch, 0.0f, 40.0f * dt);
        state.roll = approach(state.roll, 0.0f, 40.0f * dt);
    }

    // --- the powertrain, before the longitudinal step -----------------------
    // The gear is resolved from the speed the car ALREADY has, which is what
    // makes the gearbox derived rather than simulated (see DriveSpec). The two
    // things it hands forward are a torque multiplier and, while a shift is in
    // progress, a throttle cut.
    const int nGears = gearCount(spec);
    const float redline = std::max(spec.redlineRpm, spec.idleRpm + 1.0f);
    if (state.gear > nGears - 1) state.gear = nGears - 1;
    // The shift clock runs whatever the direction: it used to tick only in
    // the forward branch, so a car that rolled into reverse mid-shift (the
    // throttle is cut and slope gravity can push speed negative) kept the
    // throttle cut for the entire reverse episode.
    state.shiftTimer = std::max(0.0f, state.shiftTimer - dt);
    if (state.speed < -0.05f) {
        state.gear = -1;  // reverse is its own gear and never shifts
    } else {
        if (state.gear < 0) state.gear = 0;
        if (state.shiftTimer <= 0.0f) {
            const float f = rpmFor(spec, state.wheelSpeed, state.gear) / redline;
            if (f > clampf(spec.shiftUpFrac, 0.1f, 1.0f) && state.gear < nGears - 1) {
                ++state.gear;
                state.shiftTimer = clampf(spec.shiftTime, 0.0f, 0.6f);
            } else if (state.gear > 0 &&
                       (f < safeShiftDownFrac(spec) ||
                        (clampf(in.throttle, -1.0f, 1.0f) > 0.8f && f < 0.72f &&
                         std::fabs(state.speed) <
                             gearTopSpeed(spec, state.gear - 1) *
                                 (clampf(spec.shiftUpFrac, 0.2f, 1.0f) - 0.15f)))) {
                // The second arm is the KICKDOWN: flat out and the engine
                // under 72% of redline means the gear is too tall for the
                // grade - drop one now rather than wallowing to the passive
                // threshold. The landing guard leaves 0.15 of headroom under
                // the up-shift point - not 0.05, because the shift CUT itself
                // decays the speed, and with the tighter margin the box
                // kicked down into its own up-shift for ever and the harness
                // car crawled 170 units in 50 seconds on the FLAT.
                --state.gear;
                state.shiftTimer = clampf(spec.shiftTime, 0.0f, 0.6f);
            }
        }
    }
    const bool shifting = state.shiftTimer > 0.0f;

    // Nitrous. The tank IS the switch (capacity 0 = the vehicle has none), so
    // there is no second flag that could disagree with it.
    state.nosActive = false;
    if (spec.nosCapacity > 0.001f) {
        // Only on the throttle: holding the button against a wall used to empty
        // the tank with the car stationary (measured on the console - nos10 fell
        // 7 to 5 at spd10 0), which is a way to lose a resource without ever
        // seeing it do anything.
        if (in.nos && state.nos > 0.0f && state.grounded && !shifting &&
            in.throttle > 0.01f) {
            state.nosActive = true;
            state.nos = std::max(0.0f, state.nos - dt / spec.nosCapacity);
        } else if (!in.nos) {
            state.nos = std::min(1.0f, state.nos + clampf(spec.nosRefill, 0.0f, 1.0f) * dt);
        }
    }
    const float accelMul = gearTorqueMul(spec, state.gear < 0 ? 0 : state.gear) *
                           (state.nosActive ? 1.0f + std::max(spec.nosBoost, 0.0f) : 1.0f);
    const float topMul = state.nosActive ? std::max(spec.nosTopSpeed, 1.0f) : 1.0f;

    // --- longitudinal -------------------------------------------------------
    if (state.grounded) {
        const float throttle = shifting ? 0.0f : clampf(in.throttle, -1.0f, 1.0f);
        const float brake = clampf(in.brake, 0.0f, 1.0f);
        if (brake > 0.01f) {
            state.speed = approach(state.speed, 0.0f, spec.brakeDecel * brake * dt);
        } else if (throttle > 0.01f) {
            state.speed = std::min(state.speed + spec.accel * accelMul * throttle * dt,
                                   spec.topSpeed * topMul);
        } else if (throttle < -0.01f) {
            state.speed = std::max(state.speed + spec.accel * throttle * dt,
                                   -spec.reverseTopSpeed);
        } else {
            state.speed = approach(state.speed, 0.0f, spec.engineBraking * dt);
        }
        if (in.handbrake)
            state.speed = approach(state.speed, 0.0f, spec.brakeDecel * 0.4f * dt);

        // Gravity along the slope. Steeper ground both pulls the car down it
        // and costs grip - which is what stops a vehicle climbing a cliff.
        const float slope = std::sin(state.pitch * kDeg2Rad);
        state.speed -= spec.gravity * slope * dt;
    }
    state.speed -= spec.drag * state.speed * std::fabs(state.speed) * dt;

    // --- yaw and lateral ----------------------------------------------------
    // The bicycle model: the turn radius follows from the wheelbase and the
    // steering angle, so a long vehicle turns wide without a second knob.
    float dYaw = 0.0f;
    float yawRateRad = 0.0f;  // saved for the cornering lean below
    if (state.grounded && std::fabs(state.speed) > 0.05f) {
        yawRateRad = (state.speed / std::max(spec.wheelBase, 0.01f)) *
                     std::tan(state.steerAngle * kDeg2Rad);
        dYaw = yawRateRad * dt * kRad2Deg;
        state.yaw += dYaw;
    }

    // Yawing the BODY does not yaw the velocity: the difference is exactly the
    // sideways slip the tyres then have to kill. That is the whole lateral
    // model, and it is why grip is one number - low slides, high is on rails.
    if (dYaw != 0.0f) {
        const float r = dYaw * kDeg2Rad;
        const float c = std::cos(r), s = std::sin(r);
        const float f = state.speed * c + state.lateral * s;
        const float l = -state.speed * s + state.lateral * c;
        state.speed = f;
        state.lateral = l;
    }
    {
        float grip = in.handbrake ? spec.handbrakeGrip : spec.grip;
        if (!state.grounded) grip = 0.0f;  // no tyres on anything
        // A steep contact plane costs grip on the way to costing all of it.
        const float tilt = std::cos(std::fabs(state.roll) * kDeg2Rad);
        if (tilt < spec.maxSlopeCos) grip *= clampf(tilt / std::max(spec.maxSlopeCos, 0.01f), 0.0f, 1.0f);
        state.lateral = approach(state.lateral, 0.0f, grip * dt);
    }

    // --- integrate ----------------------------------------------------------
    const float c = std::cos(state.yaw * kDeg2Rad), s = std::sin(state.yaw * kDeg2Rad);
    // forward = (sin, 0, cos), right = (cos, 0, -sin) for a yaw about +Y.
    const float vx = state.speed * s + state.lateral * c;
    const float vz = state.speed * c - state.lateral * s;
    state.pos[0] += vx * dt;
    state.pos[2] += vz * dt;

    // Walls: four corners against the caller's solid test, the PS2 runtime's
    // twin (updateVehicles in templates.cpp - change one, change both).
    //
    // AXIS-SEPARATED: a blocked move first tries keeping only its X, then only
    // its Z. A glancing hit therefore GRINDS along the wall - speed scrubbed
    // per second, not per hit - and only a head-on (both single axes blocked
    // too) refuses the whole move and takes most of the speed. The old
    // any-corner-refuses-everything rule made every wall touch a dead stop,
    // which on a track whose walls are the racing line's edge reads as the car
    // being glued to them. Per-corner resolution is still off the table: it
    // would rotate a body a kinematic chassis has no way to represent.
    if (solid) {
        const float hx = 0.5f * spec.track, hz = 0.5f * spec.wheelBase;
        const float lx[4] = {-hx, hx, -hx, hx};
        const float lz[4] = {hz, hz, -hz, -hz};
        const float feet = state.pos[1] - spec.rideHeight;
        auto blockedAt = [&](float bx, float bz) {
            for (int k = 0; k < 4; ++k)
                if (solid(bx + lx[k] * c + lz[k] * s, bz - lx[k] * s + lz[k] * c,
                          feet))
                    return true;
            return false;
        };
        if (blockedAt(state.pos[0], state.pos[2])) {
            const float prevX = state.pos[0] - vx * dt;
            const float prevZ = state.pos[2] - vz * dt;
            // A slide is only a slide if that axis carries REAL motion. A
            // head-on has ~zero motion along the wall, so "keep only X" is
            // trivially free - and the first version took that branch, ground
            // in place and reported ~5 u/s while standing still (the harness
            // caught it: end z 8.90, end speed 4.80 where a stop was owed).
            const float wl = std::sqrt(vx * vx + vz * vz);
            const float fx = wl > 1e-6f ? std::fabs(vx) / wl : 0.0f;
            const float fz = wl > 1e-6f ? std::fabs(vz) / wl : 0.0f;
            // The grind scrubs by ANGLE: a shallow scrape barely slows, a
            // steep one digs in. f is the fraction of the motion the wall
            // lets through.
            auto grind = [&](float f) {
                return 1.0f - clampf((0.3f + 2.5f * (1.0f - f)) * dt, 0.0f, 0.6f);
            };
            const float latScrub = 1.0f - clampf(12.0f * dt, 0.0f, 0.9f);
            if (fx > 0.3f && !blockedAt(state.pos[0], prevZ)) {
                state.pos[2] = prevZ;  // slide along X
                state.speed *= grind(fx);
                state.lateral *= latScrub;
            } else if (fz > 0.3f && !blockedAt(prevX, state.pos[2])) {
                state.pos[0] = prevX;  // slide along Z
                state.speed *= grind(fz);
                state.lateral *= latScrub;
            } else {
                state.pos[0] = prevX;  // head-on: the impact takes the speed
                state.pos[2] = prevZ;
                state.speed *= 0.25f;
                state.lateral = 0.0f;
            }
        }
    }

    // --- presentation -------------------------------------------------------
    // Derived, never simulated: the wheels and the engine cost the sim nothing.
    //
    // The driven wheels' surface speed is the car's speed PLUS whatever drive
    // the tyres could not lay down. `grip` is already this model's one tyre
    // number, so the comparison is drive against grip and no new knob is
    // needed - which is also why a stock car never spins its wheels (accel 9
    // against grip 26) while one on nitrous does.
    float demand = std::fabs(state.speed);
    if (state.grounded && !shifting && in.throttle > 0.01f && in.brake < 0.01f) {
        const float drive = spec.accel * accelMul * clampf(in.throttle, 0.0f, 1.0f);
        demand += 0.5f * std::max(0.0f, drive - spec.grip);
    }
    // A sliding tyre is turning faster than the road under it.
    demand += 0.5f * std::fabs(state.lateral);
    state.wheelSpeed = approach(state.wheelSpeed, demand, kWheelSpinRate * dt);

    // The engine follows the wheels, smoothed - which is what turns a gear
    // change into an audible dip instead of a step in the pitch. While the
    // clutch is out it falls toward idle instead.
    const float rpmRate = (redline - spec.idleRpm) * dt;
    state.rpm = shifting ? approach(state.rpm, spec.idleRpm, rpmRate * 2.5f)
                         : approach(state.rpm, rpmFor(spec, state.wheelSpeed, state.gear),
                                    rpmRate * 6.0f);

    // ONE slip number for the smoke and the screech, so the two cannot
    // disagree about when a tyre has let go.
    const float slipRef = std::max(1.0f, spec.topSpeed * 0.25f);
    state.slip = clampf((std::fabs(state.lateral) +
                         std::max(0.0f, state.wheelSpeed - std::fabs(state.speed))) /
                            slipRef,
                        0.0f, 1.0f);

    // Weight transfer - the arcade car's body language, on top of the
    // terrain-derived attitude and deliberately never fed back into it (slope
    // gravity reads sin(pitch), and a cosmetic lean in there would make the
    // car accelerate downhill because it is accelerating). The pitch target
    // reads the frame's own longitudinal acceleration - wall hits included,
    // which is what makes the nose dip on impact with no code of its own -
    // and the roll target the centripetal acceleration the bicycle model just
    // produced. Signs: accelerating = nose up (squat), turning right = right
    // side up (the body leans OUT of the corner).
    {
        const float accLong = (state.speed - speed0) / dt;
        const float aLat = state.grounded ? yawRateRad * state.speed : 0.0f;
        const float la = clampf(spec.leanAmount, 0.0f, 2.0f);
        const float tp =
            state.grounded ? clampf(accLong * 0.30f, -4.0f, 4.0f) * la : 0.0f;
        const float tr =
            state.grounded ? clampf(aLat * 0.35f, -6.0f, 6.0f) * la : 0.0f;
        // 35 deg/s, up from 25: the slower follow read as a soft, boaty
        // suspension - reported from the driver's seat, and leanAmount is
        // the knob for anyone who wants the boat back.
        state.leanPitch = approach(state.leanPitch, tp, 35.0f * dt);
        state.leanRoll = approach(state.leanRoll, tr, 35.0f * dt);
    }

    // The wheels turn at the WHEEL speed, not the car's, so a burnout spins
    // them faster than the ground is moving.
    const float rolled = state.speed < 0.0f ? -state.wheelSpeed : state.wheelSpeed;
    const float spin = (rolled / std::max(spec.wheelRadius, 0.001f)) * dt * kRad2Deg;
    for (int i = 0; i < 4; ++i) {
        state.wheelSpin[i] = std::fmod(state.wheelSpin[i] + spin, 360.0f);
        if (state.wheelSpin[i] < 0.0f) state.wheelSpin[i] += 360.0f;
    }
}

}  // namespace vehiclesim
