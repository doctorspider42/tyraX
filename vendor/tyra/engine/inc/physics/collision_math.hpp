// Added by TyraX: scalar batched collision transforms.
#pragma once
#include "convex_collision.hpp"
namespace Tyra {
inline void collisionTransformPoints(const float* matrix, CollisionV3 position,
                                     const float* local, int count,
                                     CollisionV3* out) {
  for (int i = 0; i < count; ++i) {
    const float* v = local + i * 3;
    out[i] = {
        matrix[0] * v[0] + matrix[1] * v[1] + matrix[2] * v[2] + position.x,
        matrix[3] * v[0] + matrix[4] * v[1] + matrix[5] * v[2] + position.y,
        matrix[6] * v[0] + matrix[7] * v[1] + matrix[8] * v[2] + position.z};
  }
}
}  // namespace Tyra
