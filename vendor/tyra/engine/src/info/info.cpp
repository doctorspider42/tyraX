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

void* Info::allocateLargestFreeRAMBlock(size_t* Size) {
  size_t s0, s1;
  void* p;

  s0 = ~(size_t)0 ^ (~(size_t)0 >> 1);

  while (s0 && (p = malloc(s0)) == nullptr) s0 >>= 1;

  if (p) free(p);

  s1 = s0 >> 1;

  while (s1) {
    if ((p = malloc(s0 + s1)) != nullptr) {
      s0 += s1;
      free(p);
    }
    s1 >>= 1;
  }

  while (s0 && (p = malloc(s0)) == nullptr) s0 ^= s0 & -s0;

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

  return total;
}

}  // Namespace Tyra