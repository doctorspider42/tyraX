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

// ---------------------------------------------------------------------------
// The drive model
// ---------------------------------------------------------------------------
//
// Everything below works in the CANONICAL vehicle frame - forward +Z, up +Y,
// right +X, yaw in degrees about Y. The bake rotates a model out of whatever
// frame it was authored in and into this one exactly once (using Detection's
// axes), so the sim, the viewport preview and the generated runtime never see
// an exporter's opinion about axes again.

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float approach(float v, float target, float rate) {
    if (v < target) return std::min(target, v + rate);
    return std::max(target, v - rate);
}

}  // namespace

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
          const HeightFn& height, DriveState& state) {
    // A stalled frame or a paused editor must not tunnel the car through the
    // world; the sim would rather run slow than teleport.
    dt = clampf(dt, 0.0f, 0.05f);
    if (dt <= 0.0f) return;

    // --- steering -----------------------------------------------------------
    // The lock shrinks with speed. Without it a full-lock flick at top speed
    // spins the car on the spot, and a d-pad (which is always full deflection)
    // makes the vehicle undriveable rather than twitchy.
    const float speedFrac = clampf(std::fabs(state.speed) / std::max(spec.topSpeed, 0.001f), 0.0f, 1.0f);
    const float lock = spec.maxSteerDeg +
                       (spec.highSpeedSteerDeg - spec.maxSteerDeg) * speedFrac;
    const float steerIn = clampf(in.steer, -1.0f, 1.0f);
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

    // --- longitudinal -------------------------------------------------------
    if (state.grounded) {
        const float throttle = clampf(in.throttle, -1.0f, 1.0f);
        const float brake = clampf(in.brake, 0.0f, 1.0f);
        if (brake > 0.01f) {
            state.speed = approach(state.speed, 0.0f, spec.brakeDecel * brake * dt);
        } else if (throttle > 0.01f) {
            state.speed = std::min(state.speed + spec.accel * throttle * dt, spec.topSpeed);
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
    if (state.grounded && std::fabs(state.speed) > 0.05f) {
        const float yawRate = (state.speed / std::max(spec.wheelBase, 0.01f)) *
                              std::tan(state.steerAngle * kDeg2Rad);
        dYaw = yawRate * dt * kRad2Deg;
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

    // --- presentation -------------------------------------------------------
    // Derived, never simulated: the wheels cost the sim nothing.
    const float spin = (state.speed / std::max(spec.wheelRadius, 0.001f)) * dt * kRad2Deg;
    for (int i = 0; i < 4; ++i) {
        state.wheelSpin[i] = std::fmod(state.wheelSpin[i] + spin, 360.0f);
        if (state.wheelSpin[i] < 0.0f) state.wheelSpin[i] += 360.0f;
    }
}

}  // namespace vehiclesim
