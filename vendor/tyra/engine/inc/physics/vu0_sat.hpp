// Added by TyraX: bounded asynchronous OBB SAT/linear-CCD jobs.
#pragma once
#include "convex_collision.hpp"
#ifndef TYRA_PHYSICS_MICRO
#define TYRA_PHYSICS_MICRO 0
#endif
#ifndef TYRA_PHYSICS_MICRO_VERIFY
#define TYRA_PHYSICS_MICRO_VERIFY 0
#endif
namespace Tyra {
struct Vu0SatMetrics {
  unsigned jobs=0,batches=0,busyAtFinish=0,busyWhileQuerying=0,waitTicks=0;
  unsigned confirmedCcd=0,confirmedTie=0,confirmedParallel=0;
};
inline Vu0SatMetrics vu0SatMetrics;
inline unsigned vu0SatValidationErrors=0;
inline bool vu0SatVerify=false; // diagnostic-only shadow oracle, never a shipped poll
// Practical validation tolerances, relative to the smaller body's radius.
// A reference hit may never disappear. Only shallow EXTRA contacts are allowed.
inline constexpr float Vu0SatDepthRatio=1e-4f,Vu0SatPointRatio=.01f,Vu0SatNormalError=.01f;
struct Vu0SatComparison {
  bool accepted=true,different=false,extraTouch=false,missed=false;
  float depth=0,normal=0,point=0;
};
inline Vu0SatComparison compareVu0Sat(bool hit,const ConvexContact& actual,
                                     bool referenceHit,const ConvexContact& reference,
                                     float scale) {
  Vu0SatComparison r;scale=std::max(scale,1e-6f);
  if(referenceHit && !hit){r.accepted=false;r.missed=true;return r;}
  if(hit && !referenceHit){r.extraTouch=true;r.different=true;r.depth=actual.depth;
    r.accepted=std::isfinite(actual.depth) && actual.depth>=0 && actual.depth<=Vu0SatDepthRatio*scale;return r;}
  if(!hit)return r;
  r.depth=std::fabs(actual.depth-reference.depth);
  const auto nd=cvSub(actual.normal,reference.normal),pd=cvSub(actual.point,reference.point);
  r.normal=std::sqrt(cvDot(nd,nd));r.point=std::sqrt(cvDot(pd,pd));
  r.different=r.depth!=0 || r.normal!=0 || r.point!=0;
  r.accepted=std::isfinite(r.depth+r.normal+r.point) && r.depth<=Vu0SatDepthRatio*scale &&
      r.normal<=Vu0SatNormalError && r.point<=Vu0SatPointRatio*scale;
  return r;
}
struct Vu0SatValidationDetails {
  unsigned numerical=0,extraTouches=0,misses=0;
  float depthRatio=0,pointRatio=0,normalError=0,extraDepthRatio=0;
};
inline Vu0SatValidationDetails vu0SatValidation;
inline bool scalarMotionContactReference(const ConvexView& a,const ConvexView& b,
    CollisionV3 ma,CollisionV3 mb,ConvexContact& out,const CollisionObb* ba,const CollisionObb* bb) {
  // One compiled EE routine: an independently inlined copy can contract FPU
  // operations differently and exchange strict CCD support-vertex ties.
  static decltype(&convexMotionContact) volatile routine=&convexMotionContact;
  return routine(a,b,ma,mb,out,ba,bb,nullptr);
}
// Only plain scalar data crosses the asynchronous boundary. No Vec4/COP2
// helpers may execute between start() and finish(), including indirect calls.
struct alignas(16) Vu0SatJob {
  float q[32][4]{}; // 2 boxes (centre + 3 half axes), 2 motions, 21 SAT axes, count
};
struct alignas(16) Vu0SatResult {
  float normalDepth[4], sweepEnter[4], flags[4]; // leave, overlap, possible, found
  float indices[4]; // winning discrete/sweep axis, for exact EE feature selection
};
bool packVu0Sat(Vu0SatJob& job, const ConvexView& a, const ConvexView& b,
               const CollisionObb& boxA, const CollisionObb& boxB,
               CollisionV3 motionA, CollisionV3 motionB);
inline bool vu0SatScalarPreferred(const Vu0SatJob& job) {
  // The flag is stored as integer bits for ILW, not a float value.
  unsigned value;__builtin_memcpy(&value,&job.q[31][1],sizeof(value));return value!=0;
}
bool finishVu0Sat(const Vu0SatResult& result, const Vu0SatJob& job, const ConvexView& a,
                 const ConvexView& b, CollisionV3 motionA, CollisionV3 motionB,
                 ConvexContact& contact);
class Vu0SatBatch {
 public:
  static constexpr int Capacity = 7;
  bool begin();
  void start(const Vu0SatJob* jobs, int count);
  void finish(Vu0SatResult* results);
  bool running() const { return active; }
  void observeOverlap() const;
  ~Vu0SatBatch();
 private:
  bool active = false;
  int count = 0;
};
} // namespace Tyra
