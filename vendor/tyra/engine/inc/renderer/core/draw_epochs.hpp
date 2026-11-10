/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Modified by TyraX - new file: input epochs of the StaPip stamp record
# (docs/dod-submission.md, "Phase 3, first slice: the stamp record").
*/

#pragma once

namespace Tyra {

/**
 * Modified by TyraX: the input epochs a stamp's record of resolved draw
 * state was derived under (StaPipQBufferRenderer's stamp pass,
 * docs/dod-submission.md "Phase 3, first slice"). A record answer is reused
 * only while every epoch it read is unchanged, so EVERY writer of one of
 * these inputs must bump its epoch:
 *
 *   cam   view-projection, frustum planes (RendererCore3D::update,
 *         setProjection, pushEnvView / pushPortalView / popEnvView,
 *         narrowFrustumToRect). A camera writer calls
 *         Vif1Queue::beforeForeignWork() FIRST - the recorded draws are
 *         submitted under the view still in force - and bumps AFTER it.
 *   light the dynamic-light set and the torch. The game re-registers them
 *         every frame, so writers only mark them dirty; StaPip compares a
 *         snapshot and moves the epoch when the set really differs.
 *   tex   texture residency, eviction, wrap settings, VRAM-resident binds.
 *   glob  fog values, depth scale, resident program classes, program
 *         overrides, VU1 clipping mode, custom VU params, batching rules.
 *   prog  the StaPip program cache / upload / VU1 re-init.
 *
 * Revision classes. A bag the game tags with StaPipBag::revisionClass != 0
 * skips the companion-bag facts (info, colour and texture bag contents)
 * while that class's epoch is the one they were last verified under: every
 * game writer of a companion of a tagged bag bumps its class (bumpClasses,
 * or a ClassWriter for a whole writer function). Untagged bags (class 0)
 * keep the full field-by-field facts. TYRA_STAPIP_STAMP_VERIFY polices all
 * of it: a record or revision claim that the live state contradicts is
 * logged as a contract bug.
 *
 * Every epoch only grows. Dependency-free: included by the renderer core,
 * the camera, the texture manager, StaPip and the generated game.
 */
namespace DrawEpochs {

constexpr unsigned kClasses = 4;  // class 0 = untagged (full facts)
constexpr unsigned kClassStaticBatch = 1, kClassTerrainChunk = 2,
                   kClassRoadChunk = 3;
constexpr unsigned kMaskStaticBatch = 1u << kClassStaticBatch,
                   kMaskTerrainChunk = 1u << kClassTerrainChunk,
                   kMaskRoadChunk = 1u << kClassRoadChunk, kMaskAll = 0xEu;

struct alignas(64) State {
  unsigned cam = 1;
  // A camera epoch value is never reused for another view; popEnvView
  // restores the pushed view exactly, and with it the view's epoch, so an
  // env or portal pass does not void every main-view record.
  unsigned camNext = 1, camSaved = 1;
  unsigned light = 1;
  unsigned tex = 1;
  unsigned glob = 1;
  unsigned prog = 1;
  unsigned lightDirty = 1;
  unsigned frame = 0;  // RendererCore::beginFrameStamp
  // Per revision class (never 0: a record's 0 means "never verified").
  unsigned cls[kClasses] = {1, 1, 1, 1};
};
inline State g;

inline void cameraChanged() { g.cam = ++g.camNext; }
inline void cameraPushed() {
  g.camSaved = g.cam;
  g.cam = ++g.camNext;
}
inline void cameraPopped() { g.cam = g.camSaved; }
inline void texturesChanged() { ++g.tex; }
inline void globalsChanged() { ++g.glob; }
inline void programsChanged() { ++g.prog; }
inline void lightsDirty() { g.lightDirty = 1; }
inline void newFrame() { ++g.frame; }

/** A companion writer of the classes in `mask` (bit c = class c). */
inline void bumpClasses(unsigned mask) {
  for (unsigned c = 1; c < kClasses; ++c)
    if (mask & (1u << c))
      if (++g.cls[c] == 0) g.cls[c] = 1;
}

/** A writer FUNCTION: bumps on entry and again on exit, so a flush that
 * runs inside it (releaseImmutableStreams, an upload) cannot verify a
 * record against half-written companions and keep it past the writes. */
struct ClassWriter {
  unsigned mask;
  explicit ClassWriter(unsigned m) : mask(m) { bumpClasses(mask); }
  ~ClassWriter() { bumpClasses(mask); }
  ClassWriter(const ClassWriter&) = delete;
  ClassWriter& operator=(const ClassWriter&) = delete;
};

}  // namespace DrawEpochs
}  // namespace Tyra
