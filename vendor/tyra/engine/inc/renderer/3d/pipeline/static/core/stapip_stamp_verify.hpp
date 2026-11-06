/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Modified by TyraX - new file: TYRA_STAPIP_STAMP_VERIFY, the checks of the
# stamp record and the stamp pass (docs/dod-submission.md, "Phase 3, first
# slice"). Compiled into StaPip only; the state below is used only when the
# switch is on.
*/

#pragma once

#include <stdio.h>
#include <string.h>
#include <tamtypes.h>
#include "renderer/core/draw_epochs.hpp"

namespace Tyra {

/**
 * Modified by TyraX: TYRA_STAPIP_STAMP_VERIFY. With the switch on nothing is
 * sent by a record or by the pass: every draw the pass (or the record path
 * in tryStamp) would take is predicted DRY - same code, words into a scratch
 * head - and then sent down the full path, which compares its words and
 * answers with the prediction. Logged as STAMPVERIFY lines:
 *   rec   record replays compared / clean / contract bugs (a cached answer
 *         differs while its epochs did not move: a writer that does not
 *         bump) / recomputes (a re-derived word differs);
 *   pass  pass predictions / compared / bad / sent-by-pass-but-rejected /
 *         unconsumed, order and exactly-once violations of the list;
 *   cull  in-pass culls predicted / confirmed by renderNow / wrong;
 *   rev   revision-class claims checked / wrong (a game writer of a tagged
 *         bag's companions that does not bump its class).
 * Clean = every bug counter 0. The first 12 bugs print a detail line.
 */
namespace StampVerify {

enum : int { kPredFull = 0, kPredHit = 1, kPredReject = 2 };
// Group bits of a cached answer.
enum : unsigned { G_CAM = 1, G_TEX = 2, G_LIGHT = 4, G_FOGTAB = 8, G_MVPTAB = 16 };
// Bug classes: w.* = an emitted qword differs; the rest = an answer.
enum : int {
  B_MVP, B_FOG, B_TEX0, B_SPOT, B_SINGLE, B_ENV, B_OTHER, B_REND, B_FRUSTUM,
  B_TEXRES, B_SPOTACT, B_WANTED, B_CAND, B_TEXUSE, B_LIGHT, B_HEAD, B_FRAME,
  B_PASS, kB
};
inline const char* const kBNames[kB] = {
    "w.mvp",  "w.fog",  "w.tex0",  "w.spot",    "w.single", "w.env",
    "w.other", "rend",  "frustum", "texres",    "spotAct",  "wanted",
    "cand",   "texuse", "light",   "head",      "frame",    "pass"};
// Revision-claim bug classes.
enum : int { R_FACTS, R_SINGLE, R_FUNC, R_RECEIVE, R_BAND, R_PRIM, R_KEY, R_TEXBAG, kR };
inline const char* const kRNames[kR] = {"facts", "single", "func", "receive",
                                        "band",  "prim",   "key",  "texbag"};

// The dry prediction for the current draw, consumed by the full path's end
// (StaPipQBufferRenderer::afterFullStamp) or its first reject (rejected()).
struct alignas(16) Prediction {
  qword_t words[24];
  u32 armed, pred, predStage, cached, slot, qw;
  const void* wanted;
  const void* tb;
  const void* light;
  u32 clutAddr;
  u8 clutPsm, clutLoad, func, spotOn, cand, needC, needT, needL;
  u8 moved, byPass, cull, pad;
};
inline Prediction v;

struct Counters {
  u32 cmp, ok, full, agreeReject, liveReject, verdict, bug, recompute, logged;
  u32 bugAt[kB], recomputeAt[kB];
  // the pass
  u32 predicted, passCmp, passOk, passBad, passVerdict, unconsumed, order, once;
  // in-pass culls and revision claims
  u32 cullPredicted, cullOk, cullBad, revChecked, revBug;
  u32 revBugCls[DrawEpochs::kClasses], revBugAt[kR];
  u32 nextReport;
};
inline Counters vd;

__attribute__((cold, noinline)) inline void report() {
  printf("STAMPVERIFY rec cmp=%u ok=%u full=%u agreeRej=%u liveRej=%u verdict=%u "
         "bug=%u recomp=%u | pass pred=%u cmp=%u ok=%u bad=%u verdict=%u uncons=%u "
         "order=%u once=%u | cull pred=%u ok=%u bad=%u | rev chk=%u bug=%u",
         vd.cmp, vd.ok, vd.full, vd.agreeReject, vd.liveReject, vd.verdict, vd.bug,
         vd.recompute, vd.predicted, vd.passCmp, vd.passOk, vd.passBad,
         vd.passVerdict, vd.unconsumed, vd.order, vd.once, vd.cullPredicted,
         vd.cullOk, vd.cullBad, vd.revChecked, vd.revBug);
  for (int b = 0; b < kB; ++b)
    if (vd.bugAt[b] || vd.recomputeAt[b])
      printf(" %s:%u/%u", kBNames[b], vd.bugAt[b], vd.recomputeAt[b]);
  for (unsigned c = 1; c < DrawEpochs::kClasses; ++c)
    if (vd.revBugCls[c]) printf(" cls%u:%u", c, vd.revBugCls[c]);
  for (int k = 0; k < kR; ++k)
    if (vd.revBugAt[k]) printf(" %s:%u", kRNames[k], vd.revBugAt[k]);
  printf("\n");
  fflush(stdout);
}
/** Called once per flush: a STAMPVERIFY line every 20 000 checks. */
inline void tick() {
  const u32 n = vd.cmp + vd.full + vd.agreeReject + vd.predicted;
  if (n < vd.nextReport) return;
  vd.nextReport = n + 20000;
  report();
}

__attribute__((cold, noinline)) inline void logBug(int b, bool contract, u32 slot,
                                                   u32 at, const qword_t* a,
                                                   const qword_t* p) {
  if (contract) {
    ++vd.bug;
    ++vd.bugAt[b];
  } else {
    ++vd.recompute;
    ++vd.recomputeAt[b];
  }
  if (vd.logged >= 12) return;
  ++vd.logged;
  const DrawEpochs::State& g = DrawEpochs::g;
  printf("STAMPVERIFY BUG %s stage=%s slot=%u at=%u cached=0x%x mode=%u%u%u%u "
         "pass=%u ep cam=%u light=%u tex=%u glob=%u prog=%u\n",
         contract ? "contract" : "recompute", kBNames[b], slot, at, v.cached, v.needC,
         v.needT, v.needL, v.moved, v.byPass, g.cam, g.light, g.tex, g.glob, g.prog);
  if (a != nullptr && p != nullptr)
    printf("  sent %08x %08x %08x %08x\n  pred %08x %08x %08x %08x\n", a->sw[0],
           a->sw[1], a->sw[2], a->sw[3], p->sw[0], p->sw[1], p->sw[2], p->sw[3]);
  fflush(stdout);
}

inline void predictFull() {
  v.pred = kPredFull;
  v.armed = 1;
}
inline void predictReject(int n, u32 slot) {
  v.pred = kPredReject;
  v.predStage = static_cast<u32>(n);
  v.slot = slot;
  v.armed = 1;
}
inline void predictHit(u32 slot, u32 qw, bool needC, bool texCached, bool needL,
                       bool ident, bool moved, bool byPass, const void* wanted,
                       const void* tb, const void* light, u32 clutAddr, u32 clutPsm,
                       u32 clutLoad, u8 func, bool spotOn, bool cand, bool needT) {
  v.pred = kPredHit;
  v.armed = 1;
  v.slot = slot;
  v.qw = qw;
  v.cached = (needC ? 0u : G_CAM) | (texCached ? G_TEX : 0u) |
             (needL ? 0u : G_LIGHT) | (needC ? G_FOGTAB : 0u) | (ident ? G_MVPTAB : 0u);
  v.wanted = wanted;
  v.tb = tb;
  v.light = light;
  v.clutAddr = clutAddr;
  v.clutPsm = static_cast<u8>(clutPsm);
  v.clutLoad = static_cast<u8>(clutLoad);
  v.func = func;
  v.spotOn = spotOn ? 1 : 0;
  v.cand = cand ? 1 : 0;
  v.needC = needC ? 1 : 0;
  v.needT = needT ? 1 : 0;
  v.needL = needL ? 1 : 0;
  v.moved = moved ? 1 : 0;
  v.byPass = byPass ? 1 : 0;
  if (byPass) ++vd.predicted;
}

/** tryStamp rejected (stage n) after a dry prediction. */
inline void rejected(int n) {
  v.armed = 0;
  if (v.byPass) {
    // The pass would have sent it; the full path did not. Reject 8 (the wrap
    // in force) is read by the dry pass BEFORE setSubmissionBatchCandidate
    // may flush: a dry-only modelling gap, counted, not a bug.
    v.byPass = 0;
    ++vd.passVerdict;
    if (n != 8) logBug(B_PASS, true, v.slot, static_cast<u32>(n), nullptr, nullptr);
    return;
  }
  if (v.pred == kPredFull) {
    ++vd.full;
    return;
  }
  if (v.pred == kPredReject) {
    ++vd.agreeReject;
    return;
  }
  // Predicted a hit. Prim (4) and the wrap state (8) are live in both paths
  // and the dry run does not model them: not a record question.
  if (n == 4 || n == 8) {
    ++vd.liveReject;
    return;
  }
  ++vd.verdict;
  const int b = n == 3 ? B_REND : n == 5 ? B_FRUSTUM : n == 6 ? B_TEXRES : B_SPOTACT;
  logBug(b, true, v.slot, static_cast<u32>(n), nullptr, nullptr);
}

// Which head field a differing qword belongs to, and the group bits whose
// cached answer / table produced it (0 = live in both paths).
inline int field(u32 i, u32 mvpAt, u32 optAt, bool hasTex0, u32 tex0At,
                 bool spotActive, u32 spotAt, u32 singleAt, u32 envAt, u32* group) {
  *group = 0;
  if (i >= mvpAt + 1u && i <= mvpAt + 4u) {
    *group = G_CAM | G_MVPTAB;
    return B_MVP;
  }
  if (i == optAt + 1u) {
    *group = G_CAM | G_FOGTAB;
    return B_FOG;
  }
  if (hasTex0 && i == tex0At) {
    *group = G_TEX;
    return B_TEX0;
  }
  if (spotActive && i >= spotAt + 1u && i <= spotAt + 3u) {
    *group = G_LIGHT;
    return B_SPOT;
  }
  if (singleAt != 0 && i == singleAt + 1u) return B_SINGLE;
  if (envAt != 0 && i >= envAt + 1u && i <= envAt + 3u) return B_ENV;
  return B_OTHER;
}

/** The full path sent the stamp (words in `dst`, answers in the rest):
 * compare with the armed prediction. true = it was a hit prediction (the
 * caller then commits the record as the replay would). */
__attribute__((noinline)) inline bool compareSent(
    u32 qw, const qword_t* dst, u32 mvpAt, u32 optAt, bool hasTex0, u32 tex0At,
    bool spotActive, u32 spotAt, u32 singleAt, u32 envAt, const void* wanted,
    bool cand, bool textured, const void* tb, u32 clutAddr, u32 clutPsm,
    u32 clutLoad, u8 func, const void* light, bool spotOn) {
  v.armed = 0;
  if (v.pred == kPredFull) {
    ++vd.full;
    return false;
  }
  if (v.pred == kPredReject) {
    // Predicted a reject, the full path sent it.
    ++vd.verdict;
    logBug(v.predStage == 3   ? B_REND
           : v.predStage == 5 ? B_FRUSTUM
           : v.predStage == 6 ? B_TEXRES
                              : B_SPOTACT,
           false, v.slot, v.predStage, nullptr, nullptr);
    return false;
  }
  ++vd.cmp;
  const u32 bugs0 = vd.bug + vd.recompute;
  for (u32 i = 0; i < qw; ++i) {
    if (memcmp(&dst[i], &v.words[i], 16) == 0) continue;
    u32 group = 0;
    const int b = field(i, mvpAt, optAt, hasTex0, tex0At, spotActive, spotAt,
                        singleAt, envAt, &group);
    logBug(b, (v.cached & group) != 0, v.slot, i, &dst[i], &v.words[i]);
  }
  const bool tCached = (v.cached & G_TEX) != 0;
  const bool lCached = (v.cached & G_LIGHT) != 0;
  if (v.wanted != wanted) logBug(B_WANTED, tCached, v.slot, 0, nullptr, nullptr);
  if ((v.cand != 0) != cand) logBug(B_CAND, tCached, v.slot, 0, nullptr, nullptr);
  if (textured && (v.tb != tb || v.clutAddr != clutAddr || v.clutPsm != clutPsm ||
                   v.clutLoad != clutLoad || v.func != func))
    logBug(B_TEXUSE, tCached, v.slot, 0, nullptr, nullptr);
  if (v.light != light) logBug(B_LIGHT, lCached, v.slot, 0, nullptr, nullptr);
  if ((v.spotOn != 0) != spotOn) logBug(B_SPOTACT, lCached, v.slot, 0, nullptr, nullptr);
  const bool clean = vd.bug + vd.recompute == bugs0;
  if (clean) ++vd.ok;
  if (v.byPass) {
    v.byPass = 0;
    ++vd.passCmp;
    if (clean) ++vd.passOk; else ++vd.passBad;
  }
  return true;
}

/** renderNow's clip-plane cache: the epoch said "same camera" but the
 * matrix moved = a camera writer that does not bump. */
inline void headCheck(bool valid, bool vpSame, u32 clipEpoch) {
  if (!valid || vpSame || clipEpoch != DrawEpochs::g.cam) return;
  ++vd.bugAt[B_HEAD];
  ++vd.bug;
  if (vd.logged < 12) {
    ++vd.logged;
    printf("STAMPVERIFY BUG contract stage=head cam=%u\n", DrawEpochs::g.cam);
  }
}
/** The per-epoch plane / fog table was reused for an epoch whose live planes,
 * viewProj (1) or fog (2) differ. */
__attribute__((cold, noinline)) inline void frameBug(u32 what) {
  ++vd.bugAt[B_FRAME];
  ++vd.bug;
  if (vd.logged < 12) {
    ++vd.logged;
    printf("STAMPVERIFY BUG contract stage=frame what=%u cam=%u glob=%u\n", what,
           DrawEpochs::g.cam, DrawEpochs::g.glob);
  }
}
/** Order (0) / exactly-once (1) violation of the flushed list. */
__attribute__((cold, noinline)) inline void orderBug(u32 what, u32 at, u32 want) {
  if (what == 0) ++vd.order; else ++vd.once;
  if (vd.logged < 12) {
    ++vd.logged;
    printf("STAMPVERIFY BUG pass %s at=%u want=%u\n", what == 0 ? "order" : "once", at,
           want);
  }
}
/** The pass would have culled draw `at`; the general path drew it. */
__attribute__((cold, noinline)) inline void cullBug(u32 at) {
  v.cull = 0;
  ++vd.cullBad;
  if (vd.logged < 12) {
    ++vd.logged;
    printf("STAMPVERIFY BUG cull at=%u: the pass would cull, the general path did "
           "not\n",
           at);
  }
}
/** renderNow culled the draw whole (OUTSIDE): a pending cull prediction holds. */
inline void cullConfirmed() {
  if (!v.cull) return;
  v.cull = 0;
  ++vd.cullOk;
}
/** A revision claim the live state contradicts: a game writer that did not
 * bump the class. */
__attribute__((cold, noinline)) inline void revBug(unsigned cls, int what,
                                                   const void* bag, u32 slot) {
  ++vd.revBug;
  if (cls < DrawEpochs::kClasses) ++vd.revBugCls[cls];
  if (what >= 0 && what < kR) ++vd.revBugAt[what];
  if (vd.logged < 12) {
    ++vd.logged;
    printf("STAMPVERIFY BUG contract stage=rev cls=%u what=%s bag=%p slot=%u "
           "clsEpoch=%u\n",
           cls, what >= 0 && what < kR ? kRNames[what] : "?", bag, slot,
           cls < DrawEpochs::kClasses ? DrawEpochs::g.cls[cls] : 0u);
    fflush(stdout);
  }
}
/** After the general path sent draw `i` of the list. */
inline void afterGeneral(u32 i) {
  if (v.armed && v.byPass) {
    v.armed = 0;
    v.byPass = 0;
    ++vd.unconsumed;
  }
  if (v.cull) cullBug(i);
}

}  // namespace StampVerify
}  // namespace Tyra
