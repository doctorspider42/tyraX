/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

// Added by TyraX: EE MMI (128-bit integer SIMD) helpers. GCC never emits MMI
// on its own (docs/vu0-macro-mode.md), so these are hand-written; the operand
// rules are the same as the VU0 blocks': the memory is named as an "m"
// operand, every GPR the asm uses is a clobber.

#pragma once

#include <tamtypes.h>

namespace Tyra {
namespace mmi {

typedef float Floats16[16];

/**
 * Bit-for-bit equality of two 64-byte blocks - memcmp(a, b, 64) == 0 - as
 * eight `lq`, four `pxor` and an OR-reduction: ~41 EE cycles against ~247
 * for newlib's memcmp (PCSX2, tools/vu0-bench). Both pointers must be 16-byte
 * aligned, the same contract as M4x4::copy's lqc2: `lq` ignores the low four
 * address bits, so a misaligned pointer reads the wrong quadword.
 */
inline bool equal64(const float* a, const float* b) {
  u64 acc;
  asm volatile(
      "lq     $8,  0x00(%1)   \n\t"
      "lq     $9,  0x00(%2)   \n\t"
      "lq     $10, 0x10(%1)   \n\t"
      "lq     $11, 0x10(%2)   \n\t"
      "pxor   $8,  $8,  $9    \n\t"
      "lq     $12, 0x20(%1)   \n\t"
      "lq     $13, 0x20(%2)   \n\t"
      "pxor   $10, $10, $11   \n\t"
      "lq     $14, 0x30(%1)   \n\t"
      "lq     $15, 0x30(%2)   \n\t"
      "pxor   $12, $12, $13   \n\t"
      "pxor   $14, $14, $15   \n\t"
      "por    $8,  $8,  $10   \n\t"
      "por    $12, $12, $14   \n\t"
      "por    $8,  $8,  $12   \n\t"
      "pcpyud $9,  $8,  $0    \n\t"  // high doubleword down
      "or     %0,  $8,  $9    \n\t"
      : "=r"(acc)
      : "r"(a), "r"(b), "m"(*reinterpret_cast<const Floats16*>(a)),
        "m"(*reinterpret_cast<const Floats16*>(b))
      : "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15");
  return acc == 0;
}

}  // namespace mmi
}  // namespace Tyra
