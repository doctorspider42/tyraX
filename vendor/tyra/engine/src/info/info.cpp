/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022-2022, tyra - https://github.com/h4570/tyrav2
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "info/info.hpp"
#include "strings.h"
#include <stdlib.h>

namespace Tyra {

bool Info::writeLogsToFile = false;

// Modified by TyraX: default OFF - assertions stay on the console / log
// instead of taking over the screen (see info.hpp and debug/debug.hpp).
bool Info::drawAssertScreen = false;

u32 Info::presentCounter = 0;

// Modified by TyraX: COP0 Count, the clock the frame-timing rig uses. Wraps
// every ~14.6 s; u32 subtraction of two reads is wrap-safe, which is why the
// window accumulates PER-FRAME deltas rather than spanning one long interval.
static inline u32 cop0Count() {
  u32 v;
  asm volatile("mfc0 %0, $9" : "=r"(v));
  return v;
}

Info::Info() {
  fps = 0.0F;
  presentedFps = 0.0F;
  lastCount = 0;
  primed = false;
  windowTicks = 0;
  windowFrames = 0;
  windowPresents = 0;
  lastPresents = 0;
}

Info::~Info() {}

/**
 * Modified by TyraX: one rendered frame, and the running average over the
 * declared window. See getFps() in the header for why the clock changed.
 *
 * The old body sampled EE Timer 3 ONCE per frame and divided a hardcoded
 * 15625 by that single delta - so the number was an instantaneous
 * frame-to-frame reading that merely REFRESHED every fifth frame, on a clock
 * whose rate follows the video mode.
 */
void Info::update() {
  const u32 now = cop0Count();
  const u32 presents = presentCounter;
  if (!primed) {  // the first call only seeds; there is no previous frame
    primed = true;
    lastCount = now;
    lastPresents = presents;
    return;
  }
  const u32 delta = now - lastCount;
  windowTicks += (u64)delta;
  ++windowFrames;
  windowPresents += presents - lastPresents;
  lastCount = now;
  lastPresents = presents;

  if (windowTicks >= (u64)kFpsWindowTicks) {
    const float seconds = (float)windowTicks / (kTicksPerMs * 1000.0F);
    fps = (float)windowFrames / seconds;
    presentedFps = (float)windowPresents / seconds;
    windowTicks = 0;
    windowFrames = 0;
    windowPresents = 0;
  }
}

float Info::getAvailableRAM() {
  size_t bits = getFreeRAMSize();
  return bits / 1024.0F / 1024.0F;
}

// Modified by TyraX. The original searched for the largest allocatable block
// by starting at the TOP BIT of size_t - malloc(2 GB) on a 32-bit EE - halving
// until one succeeded, then freeing it, refining the size upward, and finally
// allocating the refined size AGAIN. Two things in that shape are why the
// showcase's HUD read "MEM 32.0/32 MB" on a console with 13.6 MB free, and why
// the Debugger's "Measure now" printed nothing:
//
//   - the final re-allocation can fail where the first one succeeded (the
//     refinement's own allocate/free churn moves the heap around), and its
//     recovery step clears the LOWEST SET BIT of the size - which for a size
//     that is a single bit, the everyday case, is not "a bit smaller", it is
//     ZERO. The function then returns "no block, 0 bytes", getFreeRAMSize
//     stops on the first block, and the whole measurement reads 0 free;
//   - measured on PS2DEV v2.0.0, the run of absurd requests before the first
//     success (2 GB, 1 GB, ... 16 MB, eight failures) is not free either.
//
// So: start at the console's actual RAM, and never free the block that worked
// - refine by KEEPING each better allocation and dropping the previous one,
// which removes the re-allocation and its bit-clearing recovery entirely. The
// caller still owns the returned block and still frees the chain at the end.
void* Info::allocateLargestFreeRAMBlock(size_t* Size) {
  // The EE has 32 MB and nothing this engine runs on has more.
  const size_t kEeRam = 32u * 1024u * 1024u;
  size_t s0 = kEeRam;
  void* p = nullptr;

  while (s0 && (p = malloc(s0)) == nullptr) s0 >>= 1;
  if (p == nullptr) {
    *Size = 0;
    return nullptr;
  }

  for (size_t step = s0 >> 1; step; step >>= 1) {
    void* q = malloc(s0 + step);
    if (q == nullptr) continue;
    free(p);
    p = q;
    s0 += step;
  }

  *Size = s0;
  return p;
}

size_t Info::getFreeRAMSize() {
  size_t total = 0;
  void* pFirst = nullptr;
  void* pLast = nullptr;

  for (;;) {
    size_t largest;
    void* p = allocateLargestFreeRAMBlock(&largest);

    if (largest < sizeof(void*)) {
      if (p != nullptr) free(p);
      break;
    }

    *(void**)p = nullptr;

    total += largest;

    if (pFirst == nullptr) pFirst = p;

    if (pLast != nullptr) *(void**)pLast = p;

    pLast = p;
  }

  while (pFirst != nullptr) {
    void* p = *(void**)pFirst;
    free(pFirst);
    pFirst = p;
  }

  // A sum larger than the machine is a bug report, not a reading: clamp it so
  // a broken probe can never present itself as plausible.
  const size_t kEeRam = 32u * 1024u * 1024u;
  if (total > kEeRam) total = kEeRam;
  return total;
}

}  // Namespace Tyra