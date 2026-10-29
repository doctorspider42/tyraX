// Independent reference copied from 2ed7cb6f7, before prepared contact math.
#include "physics/contact_solver.hpp"
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
using namespace Tyra;
using RbV = CollisionV3;
inline RbV rbV(float x, float y, float z) { return {x, y, z}; }
inline RbV rbAdd(RbV a, RbV b) { return cvAdd(a, b); }
inline RbV rbSub(RbV a, RbV b) { return cvSub(a, b); }
inline RbV rbMul(RbV a, float s) { return cvMul(a, s); }
inline RbV rbCross(RbV a, RbV b) { return cvCross(a, b); }
inline float rbDot(RbV a, RbV b) { return cvDot(a, b); }
inline RbV rbMat(const float *m, RbV v) {
  return {m[0] * v.x + m[1] * v.y + m[2] * v.z,
          m[3] * v.x + m[4] * v.y + m[5] * v.z,
          m[6] * v.x + m[7] * v.y + m[8] * v.z};
}
struct RbStep {
  RbV x1, v, w, vp, wp;
  float invIw[9], invMass, bounce, friction;
  bool grounded = false;
};
struct RbContact {
  int a, b;
  RbV n, p;
  float depth, vnPred, bounceTarget, mu, kn, jn, jp;
  RbV ft, ra, rb;
};
constexpr int PHYS_ITERATIONS = 6, PHYS_POS_ITERATIONS = 3;
constexpr float PHYS_SLOP = .005f, PHYS_BAUMGARTE = .5f;
void reference(std::vector<RbStep> &steps, std::vector<RbContact> &contacts,
               float bounceThreshold) {
  // --- 4. sequential impulses
  // -------------------------------------------------
  const int nc = (int)contacts.size();
  for (int k = 0; k < nc; ++k) {
    RbContact &c = contacts[k];
    RbStep &A = steps[c.a];
    c.ra = rbSub(c.p, A.x1);
    RbV vrel = rbAdd(A.v, rbCross(A.w, c.ra));
    const RbV ca = rbCross(c.ra, c.n);
    float kn = A.invMass + rbDot(rbCross(rbMat(A.invIw, ca), c.ra), c.n);
    float e = A.bounce, f = A.friction;
    if (c.b >= 0) {
      RbStep &Bs = steps[c.b];
      c.rb = rbSub(c.p, Bs.x1);
      vrel = rbSub(vrel, rbAdd(Bs.v, rbCross(Bs.w, c.rb)));
      const RbV cb = rbCross(c.rb, c.n);
      kn += Bs.invMass + rbDot(rbCross(rbMat(Bs.invIw, cb), c.rb), c.n);
      if (Bs.bounce > e)
        e = Bs.bounce;
      f = 0.5F * (f + Bs.friction);
    } else {
      c.rb = rbV(0.0F, 0.0F, 0.0F);
    }
    c.kn = kn > 1e-9F ? kn : 1e-9F;
    c.vnPred = rbDot(vrel, c.n);
    c.bounceTarget = -c.vnPred > bounceThreshold ? -e * c.vnPred : 0.0F;
    // Coulomb friction coefficient from the authored 0..1 "Friction".
    c.mu = 0.05F + 0.95F * f;
    c.jn = c.jp = 0.0F;
    c.ft = rbV(0.0F, 0.0F, 0.0F);
    if (c.n.y > 0.5F)
      A.grounded = true;
    if (c.b >= 0 && c.n.y < -0.5F)
      steps[c.b].grounded = true;
  }
  auto applyImpulse = [&](RbContact &c, RbV P) {
    RbStep &A = steps[c.a];
    A.v = rbAdd(A.v, rbMul(P, A.invMass));
    A.w = rbAdd(A.w, rbMat(A.invIw, rbCross(c.ra, P)));
    if (c.b >= 0) {
      RbStep &Bs = steps[c.b];
      Bs.v = rbSub(Bs.v, rbMul(P, Bs.invMass));
      Bs.w = rbSub(Bs.w, rbMat(Bs.invIw, rbCross(c.rb, P)));
    }
  };
  auto relVel = [&](const RbContact &c) {
    const RbStep &A = steps[c.a];
    RbV v = rbAdd(A.v, rbCross(A.w, c.ra));
    if (c.b >= 0) {
      const RbStep &Bs = steps[c.b];
      v = rbSub(v, rbAdd(Bs.v, rbCross(Bs.w, c.rb)));
    }
    return v;
  };
  for (int it = 0; it < PHYS_ITERATIONS; ++it)
    for (int k = 0; k < nc; ++k) {
      RbContact &c = contacts[k];
      RbV vrel = relVel(c);
      const float vn = rbDot(vrel, c.n);
      float nj = c.jn + (c.bounceTarget - vn) / c.kn;
      if (nj < 0.0F)
        nj = 0.0F;
      const float dj = nj - c.jn;
      c.jn = nj;
      if (dj != 0.0F)
        applyImpulse(c, rbMul(c.n, dj));
      // friction, against whatever tangential slip is left
      vrel = relVel(c);
      const RbV vt = rbSub(vrel, rbMul(c.n, rbDot(vrel, c.n)));
      const float vtl2 = rbDot(vt, vt);
      if (vtl2 < 1e-14F)
        continue;
      const float vtl = sqrtf(vtl2);
      const RbV t = rbMul(vt, 1.0F / vtl);
      const RbStep &A = steps[c.a];
      float kt =
          A.invMass + rbDot(rbCross(rbMat(A.invIw, rbCross(c.ra, t)), c.ra), t);
      if (c.b >= 0) {
        const RbStep &Bs = steps[c.b];
        kt += Bs.invMass +
              rbDot(rbCross(rbMat(Bs.invIw, rbCross(c.rb, t)), c.rb), t);
      }
      if (kt < 1e-9F)
        continue;
      RbV nf = rbAdd(c.ft, rbMul(t, -vtl / kt));
      const float lim = c.mu * c.jn;
      const float nfl2 = rbDot(nf, nf);
      if (nfl2 > lim * lim)
        nf = rbMul(nf, lim / sqrtf(nfl2));
      applyImpulse(c, rbSub(nf, c.ft));
      c.ft = nf;
    }
  // Split impulse: the overlap still left after the velocity solve is
  // removed through pseudo velocities that move the body and are then
  // forgotten, so pushing a body out of the ground never launches it.
  for (int it = 0; it < PHYS_POS_ITERATIONS; ++it)
    for (int k = 0; k < nc; ++k) {
      RbContact &c = contacts[k];
      RbStep &A = steps[c.a];
      const float vnNow = rbDot(relVel(c), c.n);
      const float remaining = c.depth - (vnNow - c.vnPred);
      const float target = remaining > PHYS_SLOP
                               ? PHYS_BAUMGARTE * (remaining - PHYS_SLOP)
                               : 0.0F;
      RbV vp = rbAdd(A.vp, rbCross(A.wp, c.ra));
      if (c.b >= 0) {
        const RbStep &Bs = steps[c.b];
        vp = rbSub(vp, rbAdd(Bs.vp, rbCross(Bs.wp, c.rb)));
      }
      float nj = c.jp + (target - rbDot(vp, c.n)) / c.kn;
      if (nj < 0.0F)
        nj = 0.0F;
      const float dj = nj - c.jp;
      c.jp = nj;
      if (dj == 0.0F)
        continue;
      const RbV P = rbMul(c.n, dj);
      A.vp = rbAdd(A.vp, rbMul(P, A.invMass));
      A.wp = rbAdd(A.wp, rbMat(A.invIw, rbCross(c.ra, P)));
      if (c.b >= 0) {
        RbStep &Bs = steps[c.b];
        Bs.vp = rbSub(Bs.vp, rbMul(P, Bs.invMass));
        Bs.wp = rbSub(Bs.wp, rbMat(Bs.invIw, rbCross(c.rb, P)));
      }
    }
}
int main() {
  std::mt19937 rng(0x505332);
  std::uniform_real_distribution<float> d(-1, 1), positive(.2f, 2.f);
  float worst = 0;
  for (int trial = 0; trial < 2000; ++trial) {
    const int nb = 16, nc = 48;
    std::vector<RbStep> ref(nb + 1);
    SolverBuffer<SolverBody> body(nb + 1);
    for (int i = 0; i < nb; ++i) {
      auto &s = ref[i];
      s.x1 = {d(rng), d(rng), d(rng)};
      s.v = {d(rng) * .1f, d(rng) * .1f, d(rng) * .1f};
      s.w = {d(rng) * .05f, d(rng) * .05f, d(rng) * .05f};
      s.invMass = i % 7 ? positive(rng) : 0;
      s.bounce = .1f;
      s.friction = .6f;
      // Symmetric positive definite inertia, including non-diagonal terms.
      float q[3] = {d(rng), d(rng), d(rng)};
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
          s.invIw[r * 3 + c] =
              s.invMass ? (q[r] * q[c] + float(r == c) * positive(rng)) : 0;
      // The diagonal additions above are independent but remain positive.
      auto &b = body[i];
      b.v = sv(s.v);
      b.w = sv(s.w);
      b.mass = {s.invMass, 0, 0, 0};
      for (int k = 0; k < 3; ++k)
        b.invI[k] = {s.invIw[k], s.invIw[k + 3], s.invIw[k + 6], 0};
    }
    std::vector<RbContact> rc(nc);
    SolverBuffer<SolverContact> sc(nc);
    for (int k = 0; k < nc; ++k) {
      auto &r = rc[k];
      r.a = rng() % nb;
      r.b = k % 4 ? int(rng() % nb) : -1;
      if (r.b == r.a)
        r.b = -1;
      r.p = {d(rng), d(rng), d(rng)};
      r.n = {d(rng), d(rng), d(rng)};
      r.n = cvMul(r.n, 1.f / std::sqrt(cvDot(r.n, r.n)));
      r.depth = .02f * positive(rng);
      auto &c = sc[k];
      c.a = r.a;
      c.b = r.b < 0 ? nb : r.b;
      c.normal = sv(r.n);
      c.ra = sv(cvSub(r.p, ref[r.a].x1));
      c.rb = sv(r.b < 0 ? RbV{} : cvSub(r.p, ref[r.b].x1), r.depth);
      prepareSolverContact(c, body[c.a], body[c.b], .1f, .6f, .01f);
    }
    reference(ref, rc, .01f);
    solvePrepared<false>(body.data(), sc.data(), nc, 6, 3, .005f, .5f);
    for (int i = 0; i <= nb; ++i)
      for (int j = 0; j < 4; ++j) {
        RbV a = j == 0   ? ref[i].v
                : j == 1 ? ref[i].w
                : j == 2 ? ref[i].vp
                         : ref[i].wp;
        RbV b = xyz(j == 0   ? body[i].v
                    : j == 1 ? body[i].w
                    : j == 2 ? body[i].vp
                             : body[i].wp);
        float e = std::max(
            {std::fabs(a.x - b.x), std::fabs(a.y - b.y), std::fabs(a.z - b.z)});
        assert(std::isfinite(e));
        worst = std::max(worst, e);
        assert(e < .0002f);
      }
    for (auto &c : sc) {
      assert(c.k[0].w >= 0);
      assert(cvDot(xyz(c.friction), xyz(c.friction)) <=
             c.angularB.w * c.angularB.w * c.k[0].w * c.k[0].w + 1e-6f);
    }
  }
  printf("2000 coupled contact graphs: scalar prepared/reference worst "
         "component error %.9f, all impulses finite and inside friction cone\n",
         worst);
}
