#include "visionpose.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace visionpose {

namespace {

constexpr float kDeg = 57.29577951308232f;
constexpr float kRad = 0.017453292519943295f;

struct Q {
    float x = 0, y = 0, z = 0, w = 1;
};
struct V {
    float x = 0, y = 0, z = 0;
};

Q mul(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Q conj(const Q& q) { return {-q.x, -q.y, -q.z, q.w}; }
Q normalized(Q q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f) return Q();
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}
V rot(const Q& q, const V& v) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    return {(1 - 2 * (y * y + z * z)) * v.x + 2 * (x * y - z * w) * v.y + 2 * (x * z + y * w) * v.z,
            2 * (x * y + z * w) * v.x + (1 - 2 * (x * x + z * z)) * v.y + 2 * (y * z - x * w) * v.z,
            2 * (x * z - y * w) * v.x + 2 * (y * z + x * w) * v.y + (1 - 2 * (x * x + y * y)) * v.z};
}
Q axisAngle(const V& axis, float radians) {
    const float l = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (l < 1e-8f) return Q();
    const float s = std::sin(radians * 0.5f) / l;
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
}
V norm(V v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l < 1e-8f) return {0, 0, 0};
    return {v.x / l, v.y / l, v.z / l};
}
Q fromArray(const float* q) { return {q[0], q[1], q[2], q[3]}; }
void toArray(const Q& q, float* out) {
    out[0] = q.x;
    out[1] = q.y;
    out[2] = q.z;
    out[3] = q.w;
}

// Yaw-pitch-roll as intrinsic rotations about Y, then X, then Z - the order
// Vision's three angles describe, and the order they have to be composed in for
// "turned, then nodded, then tilted" to mean what it says.
Q fromYawPitchRoll(float yaw, float pitch, float roll) {
    return normalized(mul(mul(axisAngle({0, 1, 0}, yaw), axisAngle({1, 0, 0}, pitch)),
                          axisAngle({0, 0, 1}, roll)));
}

// Splits `q` into rotations about three axes and clamps each, then rebuilds.
// Approximate by construction - three sequential clamps are not a proper
// constrained solve - but the job here is to stop a bad frame throwing a head
// backwards, and for that it is exactly right.
Q clampYawPitchRoll(const Q& q, float maxYaw, float maxPitch, float maxRoll) {
    // Recover angles from the rotated basis rather than from the quaternion's
    // components, which is stable through the poles that matter here.
    const V f = rot(q, {0, 0, 1});
    float yaw = std::atan2(f.x, f.z);
    float pitch = std::asin(std::max(-1.0f, std::min(1.0f, -f.y)));
    const V up = rot(q, {0, 1, 0});
    const V side = norm({std::cos(yaw), 0.0f, -std::sin(yaw)});
    float roll = std::atan2(up.x * side.x + up.y * side.y + up.z * side.z,
                            up.y * std::cos(pitch) + 1e-6f);
    const float ly = maxYaw * kRad, lp = maxPitch * kRad, lr = maxRoll * kRad;
    yaw = std::max(-ly, std::min(ly, yaw));
    pitch = std::max(-lp, std::min(lp, pitch));
    roll = std::max(-lr, std::min(lr, roll));
    return fromYawPitchRoll(yaw, pitch, roll);
}

// A 3D vector as it appears in the image, given the camera's orientation - and
// crucially KEEPING ITS LENGTH, which is where the depth information hides.
//
// No intrinsics and no principal point: a hand is small and far enough that
// perspective across it is negligible next to the noise in a 90-pixel hand, so
// this is scaled orthographic projection. The scale (distance and focal length
// together) is unknown and is solved for, which is why none of it needs sending.
// Image +y points DOWN, hence the negated up axis.
V imageVec(const Q& cameraRot, const V& worldVec) {
    const V right = rot(cameraRot, {1, 0, 0});
    const V up = rot(cameraRot, {0, 1, 0});
    return {worldVec.x * right.x + worldVec.y * right.y + worldVec.z * right.z,
            -(worldVec.x * up.x + worldVec.y * up.y + worldVec.z * up.z), 0.0f};
}

float dot2(const V& a, const V& b) { return a.x * b.x + a.y * b.y; }

// Shortest-arc interpolation, sign-corrected: between q and -q - the same
// orientation - the long way round is the joint swinging through the body.
Q slerp(const Q& a, Q b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f)
        return normalized({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                           a.w + (b.w - a.w) * t});
    const float theta = std::acos(d);
    const float s = std::sin(theta);
    return normalized({a.x * std::sin((1 - t) * theta) / s + b.x * std::sin(t * theta) / s,
                       a.y * std::sin((1 - t) * theta) / s + b.y * std::sin(t * theta) / s,
                       a.z * std::sin((1 - t) * theta) / s + b.z * std::sin(t * theta) / s,
                       a.w * std::sin((1 - t) * theta) / s + b.w * std::sin(t * theta) / s});
}

// Angle between two orientations, radians.
float angleTo(const Q& a, const Q& b) {
    float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    if (d > 1.0f) d = 1.0f;
    return 2.0f * std::acos(d);
}

V sub2(const float* from, const float* to) {
    return {to[0] - from[0], to[1] - from[1], 0.0f};
}

int findJoint(const std::vector<std::string>& names, const char* want) {
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == want) return (int)i;
    return -1;
}

}  // namespace

bool solveHead(const Observation& o, const float* parentWorldRot, const Limits& limits,
               float* outLocal) {
    if (!o.haveFace || !o.haveCamera || !parentWorldRot || !outLocal) return false;

    // Vision measures the face against the CAMERA. Turning that into a head
    // orientation is one composition - and then the joint wants it relative to
    // the neck, because a skeleton stores locals.
    const Q camera = normalized(fromArray(o.cameraRot));
    const Q faceInCamera = fromYawPitchRoll(o.faceYaw, o.facePitch, o.faceRoll);
    const Q headWorld = normalized(mul(camera, faceInCamera));
    const Q parent = normalized(fromArray(parentWorldRot));
    const Q local = normalized(mul(conj(parent), headWorld));

    toArray(clampYawPitchRoll(local, limits.headYaw, limits.headPitch, limits.headRoll), outLocal);
    return true;
}

bool solveWrist(const Observation& o, bool leftHand, const float* forearmWorldRot,
                const float* cameraRot, const PalmShape& palm, const Limits& limits,
                float* outLocal, const float* preferLocal) {
    const Observation::Hand& hand = leftHand ? o.left : o.right;
    if (!hand.have || !forearmWorldRot || !cameraRot || !outLocal) return false;
    if (hand.confidence < limits.minConfidence) return false;

    // What Vision saw, as two VECTORS in the image - lengths included. Where the
    // hand is comes from the body skeleton, which knows it in three dimensions;
    // all that is missing is which way it is turned. The first attempt at this
    // compared normalized directions and failed on eight synthetic cases out of
    // nine, by 24 to 166 degrees, because two image directions are two
    // constraints on three unknowns. The lengths are the third.
    const V obsForward = sub2(hand.wrist, hand.middleMcp);
    const V obsAcross = sub2(hand.indexMcp, hand.littleMcp);
    const V obsThumb = sub2(hand.wrist, hand.thumbMcp);
    const bool useThumb = hand.haveThumb;
    float obsNorm = dot2(obsForward, obsForward) + dot2(obsAcross, obsAcross);
    if (useThumb) obsNorm += dot2(obsThumb, obsThumb);
    if (obsNorm < 1e-9f) return false;

    const Q forearm = normalized(fromArray(forearmWorldRot));
    const Q camera = normalized(fromArray(cameraRot));
    const V palmForward = norm({palm.forward[0], palm.forward[1], palm.forward[2]});
    V palmAcross = norm({palm.across[0], palm.across[1], palm.across[2]});
    // A left hand is a right hand mirrored: the same knuckles run the other way
    // across the palm. Without this the solver finds the correct pose for the
    // wrong hand and looks confidently broken.
    if (leftHand) palmAcross = {-palmAcross.x, -palmAcross.y, -palmAcross.z};

    // The search. Two projected directions constrain a 3D orientation only up
    // to depth and a mirror, so the range searched IS the extra information:
    // a wrist bends about 70 degrees and twists about 85, and no pose outside
    // that is a candidate no matter how well it fits the pixels.
    //
    // Coarse grid then a local refine, rather than a gradient - the objective
    // has two near-equal minima (the palm and its mirror) and a descent from a
    // bad start walks confidently into the wrong one.
    // The palm at its real size, so the two predicted vectors foreshorten
    // against each other the way the real ones did.
    const Q prefer = preferLocal ? normalized(fromArray(preferLocal)) : Q();
    const V palmF{palmForward.x * palm.forwardLen, palmForward.y * palm.forwardLen,
                  palmForward.z * palm.forwardLen};
    const V palmA{palmAcross.x * palm.acrossLen, palmAcross.y * palm.acrossLen,
                  palmAcross.z * palm.acrossLen};
    // The palm's normal, from the axes as they actually are - so a left hand,
    // whose `across` has already been flipped, gets its thumb lifted off the
    // correct side without a second special case.
    const V palmN = norm({palmForward.y * palmAcross.z - palmForward.z * palmAcross.y,
                          palmForward.z * palmAcross.x - palmForward.x * palmAcross.z,
                          palmForward.x * palmAcross.y - palmForward.y * palmAcross.x});
    const V palmT{palmForward.x * palm.forwardLen * palm.thumbForward +
                      palmAcross.x * palm.acrossLen * palm.thumbAcross +
                      palmN.x * palm.acrossLen * palm.thumbOutOfPlane,
                  palmForward.y * palm.forwardLen * palm.thumbForward +
                      palmAcross.y * palm.acrossLen * palm.thumbAcross +
                      palmN.y * palm.acrossLen * palm.thumbOutOfPlane,
                  palmForward.z * palm.forwardLen * palm.thumbForward +
                      palmAcross.z * palm.acrossLen * palm.thumbAcross +
                      palmN.z * palm.acrossLen * palm.thumbOutOfPlane};

    auto score = [&](float bend, float deviate, float twist) {
        const Q local = normalized(mul(mul(axisAngle({1, 0, 0}, bend * kRad),
                                           axisAngle({0, 0, 1}, deviate * kRad)),
                                       axisAngle({0, 1, 0}, twist * kRad)));
        const Q world = normalized(mul(forearm, local));
        const V f = imageVec(camera, rot(world, palmF));
        const V a = imageVec(camera, rot(world, palmA));
        const V th = useThumb ? imageVec(camera, rot(world, palmT)) : V{0, 0, 0};
        // One unknown scale covers distance and focal length together, and it
        // has a closed form - the least-squares scale between predicted and
        // observed. Solving for it rather than sending intrinsics is what keeps
        // the phone from having to know anything.
        float pp = dot2(f, f) + dot2(a, a);
        float po = dot2(f, obsForward) + dot2(a, obsAcross);
        if (useThumb) {
            pp += dot2(th, th);
            po += dot2(th, obsThumb);
        }
        if (pp < 1e-12f) return -1e30f;
        const float s = po / pp;
        // Residual after the best scale, as a fraction of what was observed: 0
        // is a perfect fit, 1 is no better than predicting nothing.
        const float residual = obsNorm - s * po;
        float fit = -residual / obsNorm;
        // The tie-break, and it is deliberately feeble. With the thumb in the
        // residual the mirror no longer scores the same, so this only has to
        // separate poses that are genuinely indistinguishable; at any more
        // weight it starts dragging correct answers toward the rest pose, which
        // it measurably did - 8 to 19 degrees of error that vanished when the
        // weight came down by a factor of ten.
        const float d = angleTo(local, prefer);
        fit -= 0.002f * d * d;
        return fit;
    };

    float bestBend = 0, bestDeviate = 0, bestTwist = 0, best = -1e30f;
    const float bendMax = limits.wristBend, devMax = limits.wristDeviate,
                twistMax = limits.wristTwist;
    const int steps = 9;
    for (int i = 0; i <= steps; ++i)
        for (int j = 0; j <= steps; ++j)
            for (int k = 0; k <= steps; ++k) {
                const float b = -bendMax + 2.0f * bendMax * (float)i / (float)steps;
                const float d = -devMax + 2.0f * devMax * (float)j / (float)steps;
                const float t = -twistMax + 2.0f * twistMax * (float)k / (float)steps;
                const float s = score(b, d, t);
                if (s > best) {
                    best = s;
                    bestBend = b;
                    bestDeviate = d;
                    bestTwist = t;
                }
            }
    // Refine around the winner, halving the window each pass.
    float window = 2.0f * bendMax / (float)steps;
    for (int pass = 0; pass < 6; ++pass) {
        const float b0 = bestBend, d0 = bestDeviate, t0 = bestTwist;
        for (int i = -2; i <= 2; ++i)
            for (int j = -2; j <= 2; ++j)
                for (int k = -2; k <= 2; ++k) {
                    const float b = std::max(-bendMax, std::min(bendMax, b0 + window * i * 0.5f));
                    const float d = std::max(-devMax, std::min(devMax, d0 + window * j * 0.5f));
                    const float t = std::max(-twistMax, std::min(twistMax, t0 + window * k * 0.5f));
                    const float s = score(b, d, t);
                    if (s > best) {
                        best = s;
                        bestBend = b;
                        bestDeviate = d;
                        bestTwist = t;
                    }
                }
        window *= 0.5f;
    }

    // A perfect fit scores 0 and anything worse is negative. A palm that cannot
    // be placed to within a quarter of what was observed is not this hand: an
    // arm behind the body, a bystander's hand, or Vision holding on to a shape
    // that has left the frame.
    if (best < -0.25f) return false;

    const Q local = normalized(mul(mul(axisAngle({1, 0, 0}, bestBend * kRad),
                                       axisAngle({0, 0, 1}, bestDeviate * kRad)),
                                   axisAngle({0, 1, 0}, bestTwist * kRad)));
    toArray(local, outLocal);
    return true;
}

int applyToFrame(const Observation& o, const std::vector<std::string>& jointNames,
                 const std::vector<int>& parents, const float* restRot, const Limits& limits,
                 float* localRot, Tracker* tracker, std::vector<std::string>* notes) {
    if (!localRot || jointNames.empty() || parents.size() != jointNames.size()) return 0;
    const size_t n = jointNames.size();

    // World rotations of the source frame, so a joint's parent is known.
    std::vector<Q> global(n);
    for (size_t i = 0; i < n; ++i) {
        const Q local = normalized(fromArray(localRot + i * 4));
        const int par = parents[i];
        global[i] = (par >= 0 && par < (int)i) ? normalized(mul(global[par], local)) : local;
    }
    (void)restRot;

    int driven = 0;
    auto note = [&](const char* text) {
        if (notes) notes->push_back(text);
    };

    // --- the head -----------------------------------------------------------
    const int head = findJoint(jointNames, "head_joint");
    // The upper bound is not belt-and-braces: `parents` is handed here straight
    // off the wire (App::mocapLiveParents_ <- phonecam's `bodyrest`, which only
    // size-checks the array), so a phone claiming parent 9999 for the head used
    // to index `global` past its end. mocap::buildSource sanitizes the file
    // path's parents, which is why this went unnoticed on the live one.
    const int headPar = head >= 0 && head < (int)n ? parents[head] : -1;
    if (head >= 0 && headPar >= 0 && headPar < (int)n) {
        float local[4];
        if (solveHead(o, &global[headPar].x, limits, local)) {
            Q q = normalized(fromArray(local));
            if (tracker) {
                if (tracker->haveHead)
                    q = slerp(normalized(fromArray(tracker->head)), q, 1.0f - tracker->smoothing);
                toArray(q, tracker->head);
                tracker->haveHead = true;
            }
            toArray(q, localRot + (size_t)head * 4);
            ++driven;
        } else if (o.haveFace) {
            note("head: no camera orientation on this frame");
        } else if (tracker && tracker->haveHead) {
            // Vision lost the face for a frame. Holding the last answer beats
            // snapping back to rest, which reads as a twitch.
            std::memcpy(localRot + (size_t)head * 4, tracker->head, sizeof(tracker->head));
        }
    }

    // --- the wrists ---------------------------------------------------------
    const char* wristJoint[2] = {"left_hand_joint", "right_hand_joint"};
    const char* forearmJoint[2] = {"left_forearm_joint", "right_forearm_joint"};
    for (int side = 0; side < 2; ++side) {
        const Observation::Hand& hand = side == 0 ? o.left : o.right;
        if (!hand.have) continue;
        const int wrist = findJoint(jointNames, wristJoint[side]);
        const int forearm = findJoint(jointNames, forearmJoint[side]);
        if (wrist < 0 || forearm < 0) continue;
        PalmShape palm;
        float local[4];
        const float* prefer =
            tracker && tracker->haveWrist[side] ? tracker->wrist[side] : nullptr;
        if (solveWrist(o, side == 0, &global[forearm].x, o.cameraRot, palm, limits, local,
                       prefer)) {
            Q q = normalized(fromArray(local));
            if (tracker) {
                if (tracker->haveWrist[side])
                    q = slerp(normalized(fromArray(tracker->wrist[side])), q,
                              1.0f - tracker->smoothing);
                toArray(q, tracker->wrist[side]);
                tracker->haveWrist[side] = true;
            }
            toArray(q, localRot + (size_t)wrist * 4);
            ++driven;
        } else {
            note(side == 0 ? "left wrist: the palm does not fit where the arm is"
                           : "right wrist: the palm does not fit where the arm is");
        }
    }
    return driven;
}

}  // namespace visionpose
