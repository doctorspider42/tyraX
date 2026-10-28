// Added by TyraX: batched VU0 macro-mode collision transforms with a scalar
// oracle.
#pragma once
#include "convex_collision.hpp"
// Physical PS2: staging made this slower at both 8 and 24 points. Keep
// the macro kernel available for experiments, with the scalar path default.
#ifndef TYRA_COLLISION_VU0
#define TYRA_COLLISION_VU0 0
#endif
namespace Tyra {
inline void collisionTransformPoints(const float* matrix, CollisionV3 position,
                                     const float* local, int count,
                                     CollisionV3* out) {
#if defined(_EE) && TYRA_COLLISION_VU0
  // The hull budget is 24. Stage aligned quadwords once and retain matrix
  // columns in VU registers for the entire batch; never invoke Vec4 per op.
  if (count >= 4 && count <= 24) {
    alignas(16) float columns[4][4] = {{matrix[0], matrix[3], matrix[6], 0},
                                       {matrix[1], matrix[4], matrix[7], 0},
                                       {matrix[2], matrix[5], matrix[8], 0},
                                       {position.x, position.y, position.z, 1}};
    alignas(16) float input[24][4] = {}, output[24][4];
    for (int i = 0; i < count; ++i) {
      for (int k = 0; k < 3; ++k) input[i][k] = local[i * 3 + k];
      input[i][3] = 1;
    }
    const float* src = &input[0][0];
    float* dst = &output[0][0];
    int remaining = count;
    asm volatile(
        "lqc2 $vf4, 0x00(%6)\n lqc2 $vf5, 0x10(%6)\n"
        "lqc2 $vf6, 0x20(%6)\n lqc2 $vf7, 0x30(%6)\n"
        "1:\n lqc2 $vf8, 0x00(%0)\n"
        "vmulax.xyzw $ACC, $vf4, $vf8\n vmadday.xyzw $ACC, $vf5, $vf8\n"
        "vmaddaz.xyzw $ACC, $vf6, $vf8\n vmaddw.xyzw $vf9, $vf7, $vf8\n"
        "sqc2 $vf9, 0x00(%1)\n addiu %0, %0, 16\n addiu %1, %1, 16\n"
        "addiu %2, %2, -1\n bnez %2, 1b\n nop\n"
        : "+r"(src), "+r"(dst), "+r"(remaining), "=m"(output)
        : "m"(columns), "m"(input), "r"(columns));
    for (int i = 0; i < count; ++i)
      out[i] = {output[i][0], output[i][1], output[i][2]};
    return;
  }
#endif
  for (int i = 0; i < count; ++i) {
    const float* v = local + i * 3;
    out[i] = {
        matrix[0] * v[0] + matrix[1] * v[1] + matrix[2] * v[2] + position.x,
        matrix[3] * v[0] + matrix[4] * v[1] + matrix[5] * v[2] + position.y,
        matrix[6] * v[0] + matrix[7] * v[1] + matrix[8] * v[2] + position.z};
  }
}
}  // namespace Tyra
