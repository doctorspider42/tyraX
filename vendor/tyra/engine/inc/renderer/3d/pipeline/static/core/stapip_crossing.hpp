/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Modified by TyraX - new file: the crossing pass (TYRA_STAPIP_CROSSING_PASS,
# docs/dod-submission.md, "Phase 3, second slice: the crossing pass"). The
# crossing (partial) route as a stage-major, record-based pass inside the
# 2.12.0 stamp pass. Compiled into StaPip only (stapip_core.cpp and
# stapip_qbuffer_renderer.cpp).
*/

#pragma once

#include <stdio.h>
#include <string.h>
#include <tamtypes.h>
#include "math/vec4.hpp"
#include "math/plane.hpp"
#include "renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp"

#if TYRA_STAPIP_CROSSING_ON

namespace Tyra {

/**
 * Modified by TyraX: the crossing pass.
 *
 * A crossing bag (its box straddles the view, the lean partial replay sends
 * it: classifyRoutes + emitPartialRuns) used to re-derive its planes, re-read
 * its package boxes cold and re-build its uniform head every frame. Now:
 *  - after a lean partial replay the bag keeps a crossing RECORD in its stamp
 *    slot (XRec): its merged package boxes and coarse boxes in one SoA block,
 *    its main box, the cull entry (= the stamp's) and the clip entry with
 *    their serials, and where the clip REF goes in the stamp head;
 *  - a bag that has never been wholly visible gets a CROSSING-ONLY stamp slot
 *    (xonly) built from its crossing head: tryStamp rejects it, the stamp
 *    pass takes it only as a crossing draw, a real captureStamp clears it;
 *  - at every recorded-list flush a pre-pass (StaPipCore::crossClassify)
 *    derives the record-valid draws' planes (shared per model value) and
 *    classifies ALL their packages in one loop into a per-flush arena;
 *  - the stamp pass reaches such a draw straddling, runs every stamp check,
 *    and emits it from the record in list order
 *    (StaPipQBufferRenderer::crossEmit): the stamp head, the clip REF,
 *    emitPartialRuns over the record's entries. Anything else: general path.
 */
namespace CrossPass {

constexpr u32 kMaxList = 1024;  // list positions the pre-pass covers
constexpr u32 kArena = 16384;   // packages classified per flush
constexpr u32 kMaxCross = 256;  // crossing bags per flush
constexpr u32 kSets = 64;       // plane sets (model value + band) per flush
constexpr u32 kSlots = 512;     // = StaPipQBufferRenderer::kStampSlots

// One crossing record per stamp slot. Valid while: the bag line facts below
// hold (pre-pass), glob/prog are the epochs it was built under, the cull entry
// is still the stamp's (pointer + serial) and the clip entry's serial / arena
// are unchanged (the pass); a destroyed entry clears it (sweepStamps). The
// stamp's own record (facts or class revision, model, T/L epochs) is checked
// by the stamp pass before a crossing draw ever reaches the emit.
struct alignas(64) XRec {
  const void* bag;
  const void* vertices;
  u32 count, bboxVersion;
  const void* cull;
  u32 cullSerial;
  const void* clip;
  u32 clipSerial;
  const void* clipArena;
  u32 glob, prog, repTex;
  u16 maxVert, n, lastSize, ins, cap, packageSize;
  u8 valid, stripped, tight, fullClip, cb, rep;
  alignas(16) Vec4 lo, hi;  // the main box (object space)
  // SoA, one block: pmin[n], pmax[n], cmin[g], cmax[g] (g = ceil(n / 8)).
  Vec4* box;
};
inline XRec* xrecs = nullptr;

// One plane set per (model value, band) in a flush.
struct alignas(16) PlaneSet {
  Plane op[6];  // object-space view frustum
  Plane cp[8];  // object-space VU1 clip planes (+ exact near/far)
  u8 cpOk;
};
// One crossing bag of the flush.
struct alignas(16) Cross {
  float model[16];  // the value the planes were derived from
  const void* bag;
  const void* modelPtr;
  u32 off;
  u16 n;
  s16 slot;
  u16 set;
  u16 pad;
};
// The pre-pass's verdict per list position.
enum : u8 { VK_NONE = 0, VK_CROSS = 1, VK_IN = 2, VK_OUT = 3, VK_GB = 4,
            VK_STALE = 5, VK_FULL = 6 };
struct Flush {
  u32 n;  // list positions covered
  u32 nCross;
  u32 used;  // arena packages
  u8 partialBatch;
  u8 vk[kMaxList];
  u16 xi[kMaxList];
  Cross cl[kMaxCross];
  PlaneSet sets[kSets];
  alignas(16) u8 routes[kArena];
  alignas(16) u8 masks[kArena];
};
inline Flush F;

// A stamp slot synthesized from a crossing bag's head (crossSynth): it
// carries the crossing record only and is NEVER a stamp.
inline u8 xonly[kSlots];

// The general path's crossing head, taken just before emitPartialRuns (the
// packet may be sent by it), with sendObjectData's stamp offsets.
struct Snap {
  u32 qw, ok;
  u16 mvpAt, optAt, tex0At, spotAt, singleAt, envAt;
  u8 fogEligible, hasTex0, spotActive, fogOff, wrapBlock;
  qword_t q[26] __attribute__((aligned(16)));
};
inline Snap snap;

#if TYRA_STAPIP_CROSSING_VERIFY
/**
 * TYRA_STAPIP_CROSSING_VERIFY: nothing is sent by the crossing pass. Its
 * pre-pass runs as usual; crossEmit builds the draw's words DRY into vWords
 * and the draw goes down the general path, whose classification
 * (crossVerifyClassify) and emitted head + runs (crossVerifyCompare, a DMAC
 * walk: tags exact but for the PAD bits, CNT payloads exact, REFs exact) are
 * compared. Logged as CROSSVERIFY lines every 20 000 predictions; clean =
 * every bad counter, noCmp, once and order 0 (cmp == preds).
 */
struct V {
  u32 armed, li, qw, headQw, n, cmpWords, cmpCls;
  const void* bag;
  const void* curBag;
  const u8* routes;
  const u8* masks;
  u32 preds, cmp, wordBad, headBad, runsBad, lenBad, clsBad, verdictBad, noCmp,
      once, order, logged, padOnly, nextReport;
};
inline V v = {};
constexpr u32 kVWords = 4096;  // the dry words (qwords)
inline qword_t vWords[kVWords] __attribute__((aligned(16)));
inline int curIdx = -1;  // the list index renderNow is running for

__attribute__((cold, noinline)) inline void report() {
  printf("CROSSVERIFY preds=%u cmp=%u wordBad=%u headBad=%u runsBad=%u lenBad=%u "
         "clsBad=%u verdictBad=%u noCmp=%u once=%u order=%u padOnly=%u\n",
         v.preds, v.cmp, v.wordBad, v.headBad, v.runsBad, v.lenBad, v.clsBad,
         v.verdictBad, v.noCmp, v.once, v.order, v.padOnly);
  fflush(stdout);
}
/** Called once per flush: a CROSSVERIFY line every 20 000 predictions. */
inline void tick() {
  if (v.preds < v.nextReport) return;
  v.nextReport = v.preds + 20000;
  report();
}

// The general path finished list index li (stamp pass `general:`).
inline void afterGeneral(u32 li) {
  curIdx = -1;
  if (!v.armed) return;
  if (v.li != li) ++v.order;
  if (v.cmpWords == 0) ++v.noCmp;
  else if (v.cmpWords > 1) ++v.once;
  v.armed = 0;
  v.curBag = nullptr;
}

__attribute__((cold, noinline)) inline void logBug(const char* what, u32 at, u32 want,
                                                   u32 got, const qword_t* a,
                                                   const qword_t* p) {
  if (v.logged >= 16) return;
  ++v.logged;
  printf("CROSSVERIFY BUG %s at=%u want=%u got=%u headQw=%u n=%u\n", what, at, want,
         got, v.headQw, v.n);
  if (a != nullptr && p != nullptr)
    printf("  sent %08x %08x %08x %08x\n  pred %08x %08x %08x %08x\n", a->sw[0],
           a->sw[1], a->sw[2], a->sw[3], p->sw[0], p->sw[1], p->sw[2], p->sw[3]);
  fflush(stdout);
}
#endif

}  // namespace CrossPass
}  // namespace Tyra

#endif
