/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: analytic two-bone foot IK over the skeletal pose.
# The math is mirrored by the editor's src/footik.cpp so the viewport
# preview and the console agree - change one, change both.
*/

#include "renderer/3d/mesh/dynamic/skel_foot_ik.hpp"

#include <math.h>
#include <string.h>

namespace Tyra {

namespace {

// --- small column-major 4x4 / 3-vector helpers (plain EE floats: this runs
// a few dozen times a frame, and VU0 macro mode is busy skinning) ---

void v3sub(const float* a, const float* b, float* r) {
  r[0] = a[0] - b[0];
  r[1] = a[1] - b[1];
  r[2] = a[2] - b[2];
}

float v3dot(const float* a, const float* b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void v3cross(const float* a, const float* b, float* r) {
  r[0] = a[1] * b[2] - a[2] * b[1];
  r[1] = a[2] * b[0] - a[0] * b[2];
  r[2] = a[0] * b[1] - a[1] * b[0];
}

float v3len(const float* a) { return sqrtf(v3dot(a, a)); }

/** Normalizes in place; returns the original length (0 = left untouched). */
float v3norm(float* a) {
  const float l = v3len(a);
  if (l > 1e-8F) {
    a[0] /= l;
    a[1] /= l;
    a[2] /= l;
  }
  return l;
}

/** Rotates the 3-vector part of a column-major 4x4 (w = 0 semantics). */
void xformDir(const float* m, const float* v, float* r) {
  r[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2];
  r[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2];
  r[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2];
}

void xformPoint(const float* m, const float* v, float* r) {
  r[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12];
  r[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13];
  r[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14];
}

/** General affine inverse - the object matrix carries per-axis scale, so the
 * cheap transpose-of-rotation shortcut is not available. Returns false on a
 * singular basis (a zero scale), which the caller treats as "no IK". */
bool invertAffine(const float* m, float* out) {
  const float a = m[0], b = m[4], c = m[8];
  const float d = m[1], e = m[5], f = m[9];
  const float g = m[2], h = m[6], i = m[10];
  const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
  const float det = a * A + b * B + c * C;
  if (det > -1e-12F && det < 1e-12F) return false;
  const float inv = 1.0F / det;
  out[0] = A * inv;
  out[1] = B * inv;
  out[2] = C * inv;
  out[3] = 0.0F;
  out[4] = (c * h - b * i) * inv;
  out[5] = (a * i - c * g) * inv;
  out[6] = (b * g - a * h) * inv;
  out[7] = 0.0F;
  out[8] = (b * f - c * e) * inv;
  out[9] = (c * d - a * f) * inv;
  out[10] = (a * e - b * d) * inv;
  out[11] = 0.0F;
  const float tx = m[12], ty = m[13], tz = m[14];
  out[12] = -(out[0] * tx + out[4] * ty + out[8] * tz);
  out[13] = -(out[1] * tx + out[5] * ty + out[9] * tz);
  out[14] = -(out[2] * tx + out[6] * ty + out[10] * tz);
  out[15] = 1.0F;
  return true;
}

/** Rodrigues: rotate v about the unit axis by `ang` radians. */
void rotateAbout(const float* v, const float* axis, float ang, float* r) {
  const float c = cosf(ang), s = sinf(ang);
  float cr[3];
  v3cross(axis, v, cr);
  const float d = v3dot(axis, v) * (1.0F - c);
  for (int k = 0; k < 3; ++k) r[k] = v[k] * c + cr[k] * s + axis[k] * d;
}

/** 3x3 rotation (column-major, 9 floats) taking unit `from` to unit `to`. */
void rotFromTo(const float* from, const float* to, float* R) {
  float axis[3];
  v3cross(from, to, axis);
  float dot = v3dot(from, to);
  if (dot > 1.0F) dot = 1.0F;
  if (dot < -1.0F) dot = -1.0F;
  const float alen = v3norm(axis);
  if (alen < 1e-7F) {
    if (dot > 0.0F) {  // already aligned
      memset(R, 0, 9 * sizeof(float));
      R[0] = R[4] = R[8] = 1.0F;
      return;
    }
    // antiparallel: any perpendicular axis does, pick the most stable one
    float pick[3] = {1.0F, 0.0F, 0.0F};
    if (fabsf(from[0]) > 0.9F) {
      pick[0] = 0.0F;
      pick[1] = 1.0F;
    }
    v3cross(from, pick, axis);
    v3norm(axis);
    dot = -1.0F;
  }
  const float ang = acosf(dot);
  const float c = cosf(ang), s = sinf(ang), t = 1.0F - c;
  const float x = axis[0], y = axis[1], z = axis[2];
  R[0] = t * x * x + c;
  R[1] = t * x * y + s * z;
  R[2] = t * x * z - s * y;
  R[3] = t * x * y - s * z;
  R[4] = t * y * y + c;
  R[5] = t * y * z + s * x;
  R[6] = t * x * z + s * y;
  R[7] = t * y * z - s * x;
  R[8] = t * z * z + c;
}

void rot3Apply(const float* R, const float* v, float* r) {
  r[0] = R[0] * v[0] + R[3] * v[1] + R[6] * v[2];
  r[1] = R[1] * v[0] + R[4] * v[1] + R[7] * v[2];
  r[2] = R[2] * v[0] + R[5] * v[1] + R[8] * v[2];
}

/**
 * Rotates a joint's 4x4 about a pivot: its basis is pre-multiplied by R (so
 * the bone turns) and its origin orbits the pivot. Passing the joint's own
 * origin as the pivot turns it in place, which is what the chain's last bone
 * and the foot roll want.
 */
void rotateJoint(float* m, const float* R, const float* pivot) {
  for (int col = 0; col < 3; ++col) {
    float v[3] = {m[col * 4], m[col * 4 + 1], m[col * 4 + 2]};
    float r[3];
    rot3Apply(R, v, r);
    m[col * 4] = r[0];
    m[col * 4 + 1] = r[1];
    m[col * 4 + 2] = r[2];
  }
  float rel[3] = {m[12] - pivot[0], m[13] - pivot[1], m[14] - pivot[2]};
  float rr[3];
  rot3Apply(R, rel, rr);
  m[12] = pivot[0] + rr[0];
  m[13] = pivot[1] + rr[1];
  m[14] = pivot[2] + rr[2];
}

/** Critically damped follow - no overshoot at any dt, which matters because
 * a stair edge is a step input and an underdamped foot would visibly bounce
 * off every tread. */
void springTo(float* x, float* v, float target, float rate, float dt) {
  if (dt <= 0.0F || rate <= 0.0F) {
    *x = target;
    *v = 0.0F;
    return;
  }
  if (dt > 0.1F) dt = 0.1F;  // a hitch must not fling the foot
  const float w = rate;
  const float a = -2.0F * w * (*v) - w * w * (*x - target);
  *v += a * dt;
  *x += (*v) * dt;
}

float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

FootIk::FootIk() {
  memset(offset, 0, sizeof(offset));
  memset(offsetVel, 0, sizeof(offsetVel));
  memset(targetW, 0, sizeof(targetW));
  memset(hit, 0, sizeof(hit));
  for (int i = 0; i < FootIkRig::kMaxLegs; ++i) {
    normalW[i][0] = 0.0F;
    normalW[i][1] = 1.0F;
    normalW[i][2] = 0.0F;
  }
  memset(world, 0, sizeof(world));
  world[0] = world[5] = world[10] = world[15] = 1.0F;
  memcpy(worldInv, world, sizeof(world));
}

void FootIk::setWorld(const M4x4& objectToWorld) {
  memcpy(world, objectToWorld.data, 16 * sizeof(float));
  haveWorld = invertAffine(world, worldInv);
}

void FootIk::modifyPose(SkelInstance& inst) {
  if (weight <= 0.0F || groundFn == nullptr || !haveWorld) return;
  if (!rig.valid()) return;

  // World up expressed in model space. Two forms, and mixing them up is the
  // classic bug here: the RAW vector is what a world-space HEIGHT converts
  // into (it carries the object's scale), the unit one is a DIRECTION.
  const float worldUp[3] = {0.0F, 1.0F, 0.0F};
  float modelUpRaw[3];
  xformDir(worldInv, worldUp, modelUpRaw);
  float modelUpUnit[3] = {modelUpRaw[0], modelUpRaw[1], modelUpRaw[2]};
  if (v3norm(modelUpUnit) < 1e-8F) return;

  const u8 legs = rig.legCount;

  // --- stage 1: trace, from the pose the CLIP produced ---
  float deepest = 0.0F;
  for (u8 i = 0; i < legs; ++i) {
    const FootIkRig::Leg& leg = rig.legs[i];
    const M4x4& ankle = inst.poseGlobal((u32)leg.ankle);
    float ankleM[3] = {ankle.data[12], ankle.data[13], ankle.data[14]};
    float soleM[3];
    for (int k = 0; k < 3; ++k)
      soleM[k] = ankleM[k] - modelUpUnit[k] * rig.soleOffset;

    float soleW[3], ankleW[3];
    xformPoint(world, soleM, soleW);
    xformPoint(world, ankleM, ankleW);

    float gy = 0.0F;
    float gn[3] = {0.0F, 1.0F, 0.0F};
    hit[i] = groundFn(groundUser, soleW, rig.traceUp, rig.traceDown, &gy, gn);

    float raw = 0.0F;
    if (hit[i]) {
      raw = clampf(gy - soleW[1], -rig.traceDown, rig.maxLift);
      if (v3norm(gn) < 1e-6F) {
        gn[0] = 0.0F;
        gn[1] = 1.0F;
        gn[2] = 0.0F;
      }
      if (gn[1] < 0.0F)
        for (int k = 0; k < 3; ++k) gn[k] = -gn[k];  // a soup has no winding
      memcpy(normalW[i], gn, sizeof(gn));
    } else {
      normalW[i][0] = 0.0F;
      normalW[i][1] = 1.0F;
      normalW[i][2] = 0.0F;
    }

    springTo(&offset[i], &offsetVel[i], raw, rig.smoothing, frameDt);
    if (offset[i] < deepest) deepest = offset[i];

    // absolute world target for the ankle - taken BEFORE the pelvis moves,
    // because where the foot has to land does not depend on the hips
    targetW[i][0] = ankleW[0];
    targetW[i][1] = ankleW[1] + offset[i] * weight;
    targetW[i][2] = ankleW[2];
  }

  // --- stage 2: lower the pelvis so the deepest leg can reach, and TIP it
  // toward the lower foot. The drop alone is not enough: with one leg
  // reaching down a step and the other bent under the body, level hips read
  // as a character on stilts. The tilt is what puts the weight on a leg. ---
  pelvisOff = clampf(deepest, -rig.maxDrop, 0.0F) * weight;
  if (rig.pelvis >= 0) {
    M4x4 m = inst.poseGlobal((u32)rig.pelvis);
    bool edited = false;
    if (pelvisOff < -1e-5F) {
      m.data[12] += modelUpRaw[0] * pelvisOff;
      m.data[13] += modelUpRaw[1] * pelvisOff;
      m.data[14] += modelUpRaw[2] * pelvisOff;
      edited = true;
    }

    if (legs >= 2 && rig.pelvisTilt > 0.0F) {
      const M4x4& h0 = inst.poseGlobal((u32)rig.legs[0].hip);
      const M4x4& h1 = inst.poseGlobal((u32)rig.legs[1].hip);
      float hipVec[3] = {h1.data[12] - h0.data[12], h1.data[13] - h0.data[13],
                         h1.data[14] - h0.data[14]};
      const float span = v3norm(hipVec);
      // The two feet's height difference, converted from world into model
      // units through the SAME raw up-vector a height offset uses - mixing
      // that up with the unit one silently scales the tilt by the object's
      // scale.
      const float upLen = sqrtf(modelUpRaw[0] * modelUpRaw[0] +
                                modelUpRaw[1] * modelUpRaw[1] +
                                modelUpRaw[2] * modelUpRaw[2]);
      const float dModel = (targetW[0][1] - targetW[1][1]) * upLen;
      if (span > 1e-4F && fabsf(dModel) > 1e-5F) {
        float ang = atanf(dModel / span) * rig.pelvisTilt * weight;
        const float maxAng = rig.maxTiltDeg * (3.14159265F / 180.0F);
        ang = clampf(ang, -maxAng, maxAng);
        float axis[3];
        v3cross(hipVec, modelUpUnit, axis);
        if (v3norm(axis) > 1e-6F) {
          // Sign by experiment, the same trick the knee bend uses: rotate the
          // hip offset both ways and keep the one that moves leg 0's hip the
          // way its own foot went. Cheaper and safer than reasoning about an
          // arbitrary rig's handedness.
          float rel[3] = {h0.data[12] - m.data[12], h0.data[13] - m.data[13],
                          h0.data[14] - m.data[14]};
          float probe[3];
          rotateAbout(rel, axis, ang, probe);
          const float moved = (probe[0] - rel[0]) * modelUpUnit[0] +
                              (probe[1] - rel[1]) * modelUpUnit[1] +
                              (probe[2] - rel[2]) * modelUpUnit[2];
          if (moved * dModel < 0.0F) ang = -ang;
          float R[9];
          float turned[3];
          rotateAbout(modelUpUnit, axis, ang, turned);
          rotFromTo(modelUpUnit, turned, R);
          float pivot[3] = {m.data[12], m.data[13], m.data[14]};
          rotateJoint(m.data, R, pivot);
          edited = true;
        }
      }
    }

    if (edited) {
      inst.setPoseGlobal((u32)rig.pelvis, m);
      inst.refreshPose();  // the hips must have MOVED before the legs read them
    }
  }

  // --- stage 3: solve each leg onto its target ---
  for (u8 i = 0; i < legs; ++i)
    solveLeg(inst, rig.legs[i], i, modelUpUnit, modelUpRaw);
  inst.refreshPose();
}

void FootIk::solveLeg(SkelInstance& inst, const FootIkRig::Leg& leg, u8 index,
                      const float* modelUpUnit, const float* modelUpRaw) {
  (void)modelUpRaw;
  const u32 hipId = (u32)leg.hip, kneeId = (u32)leg.knee,
            ankleId = (u32)leg.ankle;

  M4x4 hipM = inst.poseGlobal(hipId);
  M4x4 kneeM = inst.poseGlobal(kneeId);
  M4x4 ankleM = inst.poseGlobal(ankleId);

  float A[3] = {hipM.data[12], hipM.data[13], hipM.data[14]};
  float B[3] = {kneeM.data[12], kneeM.data[13], kneeM.data[14]};
  float C[3] = {ankleM.data[12], ankleM.data[13], ankleM.data[14]};

  float T[3];
  xformPoint(worldInv, targetW[index], T);

  float ab[3], bc[3];
  v3sub(B, A, ab);
  v3sub(C, B, bc);
  const float L1 = v3len(ab), L2 = v3len(bc);
  if (L1 < 1e-5F || L2 < 1e-5F) return;

  // The bend plane comes from the POSE, so the knee keeps hinging the way the
  // animator hinged it - no pole target to author, and a rig with any bone
  // orientation convention works. A straight leg has no plane of its own, so
  // fall back to a hip axis (still a stable, rig-relative choice).
  float ac[3];
  v3sub(C, A, ac);
  float plane[3];
  v3cross(ab, ac, plane);
  if (v3norm(plane) < 1e-6F) {
    float hipX[3] = {hipM.data[0], hipM.data[1], hipM.data[2]};
    v3cross(ac, hipX, plane);
    if (v3norm(plane) < 1e-6F) {
      float hipZ[3] = {hipM.data[8], hipM.data[9], hipM.data[10]};
      v3cross(ac, hipZ, plane);
      if (v3norm(plane) < 1e-6F) return;
    }
  }

  float at[3];
  v3sub(T, A, at);
  float d = v3len(at);
  if (d < 1e-5F) return;
  float dir[3] = {at[0] / d, at[1] / d, at[2] / d};
  // A target out of reach is pulled onto the reachable sphere rather than
  // refused: the leg then points straight at it, fully extended, which is
  // what the pelvis drop above is there to make rare.
  const float dMin = fabsf(L1 - L2) + 1e-3F;
  const float dMax = L1 + L2 - 1e-3F;
  d = clampf(d, dMin, dMax);
  float Tc[3];
  for (int k = 0; k < 3; ++k) Tc[k] = A[k] + dir[k] * d;

  // law of cosines at the hip
  float cosA = (L1 * L1 + d * d - L2 * L2) / (2.0F * L1 * d);
  cosA = clampf(cosA, -1.0F, 1.0F);
  const float angA = acosf(cosA);

  // Both bend directions are computed and the one closer to the CURRENT
  // upper bone wins. That is cheaper than reasoning about the cross-product
  // winding of an arbitrary rig, and it cannot pick the wrong knee.
  float cand1[3], cand2[3];
  rotateAbout(dir, plane, angA, cand1);
  rotateAbout(dir, plane, -angA, cand2);
  float abUnit[3] = {ab[0] / L1, ab[1] / L1, ab[2] / L1};
  const float* pick = v3dot(cand1, abUnit) >= v3dot(cand2, abUnit) ? cand1
                                                                  : cand2;
  float Bn[3];
  for (int k = 0; k < 3; ++k) Bn[k] = A[k] + pick[k] * L1;

  // R1 swings the thigh onto its new direction, carrying knee and ankle
  float newAb[3];
  v3sub(Bn, A, newAb);
  v3norm(newAb);
  float R1[9];
  rotFromTo(abUnit, newAb, R1);
  rotateJoint(hipM.data, R1, A);
  rotateJoint(kneeM.data, R1, A);
  rotateJoint(ankleM.data, R1, A);

  // R2 swings the shin so the ankle lands exactly on the target
  float C1[3] = {ankleM.data[12], ankleM.data[13], ankleM.data[14]};
  float bc1[3], bt[3];
  v3sub(C1, Bn, bc1);
  v3sub(Tc, Bn, bt);
  if (v3norm(bc1) > 1e-6F && v3norm(bt) > 1e-6F) {
    float R2[9];
    rotFromTo(bc1, bt, R2);
    rotateJoint(kneeM.data, R2, Bn);
    rotateJoint(ankleM.data, R2, Bn);
  }

  // Foot roll. The tilt is measured against the object's own up, so a flat
  // floor is exactly identity and the clip's authored foot angle survives
  // untouched - the property that keeps IK invisible on level ground.
  if (rig.normalBlend > 0.0F && hit[index]) {
    float nModel[3];
    xformDir(worldInv, normalW[index], nModel);
    if (v3norm(nModel) > 1e-6F) {
      float dot = clampf(v3dot(modelUpUnit, nModel), -1.0F, 1.0F);
      float ang = acosf(dot) * rig.normalBlend * weight;
      const float maxAng = rig.maxRollDeg * (3.14159265F / 180.0F);
      if (ang > maxAng) ang = maxAng;
      if (ang > 1e-4F) {
        float axis[3];
        v3cross(modelUpUnit, nModel, axis);
        if (v3norm(axis) > 1e-6F) {
          float partial[3];
          rotateAbout(modelUpUnit, axis, ang, partial);
          float R3[9];
          rotFromTo(modelUpUnit, partial, R3);
          float pivot[3] = {ankleM.data[12], ankleM.data[13], ankleM.data[14]};
          rotateJoint(ankleM.data, R3, pivot);
        }
      }
    }
  }

  inst.setPoseGlobal(hipId, hipM);
  inst.setPoseGlobal(kneeId, kneeM);
  inst.setPoseGlobal(ankleId, ankleM);
}

}  // namespace Tyra
