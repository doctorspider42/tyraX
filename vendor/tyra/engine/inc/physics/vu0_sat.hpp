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
};
inline Vu0SatMetrics vu0SatMetrics;
inline unsigned vu0SatValidationErrors=0;
inline bool vu0SatVerify=false; // diagnostic-only shadow oracle, never a shipped poll
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
