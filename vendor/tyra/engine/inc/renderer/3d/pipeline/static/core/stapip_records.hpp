/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Modified by TyraX - new file: the retained draw records
# (docs/record-renderer.md, phase 1).
*/

#pragma once

#include <tamtypes.h>
#include <vector>
#include "math/vec4.hpp"
#include "renderer/core/3d/bbox/core_bbox.hpp"

// Modified by TyraX: the generated game's world producers (terrain, static
// batches, road and procedural chunks) draw from record lists. 0 is the
// kill switch: the old per-item loops.
#ifndef TYRA_STAPIP_RECORD_LISTS
#define TYRA_STAPIP_RECORD_LISTS 1
#endif
// 1: every frame the old loops also run dry and the game compares their
// draw sequence with the records' (RECLISTVERIFY), and rebuilds each list
// from the live producer state to prove no writer skipped its revision bump.
#ifndef TYRA_STAPIP_RECORD_LIST_VERIFY
#define TYRA_STAPIP_RECORD_LIST_VERIFY 0
#endif

namespace Tyra {

class StaPipBag;

/**
 * Modified by TyraX: the per-view inputs of a record list's visibility pass
 * (StaPipRecordList::cull). Everything a producer's old per-frame loop read
 * from the game's camera and view state, as data. Rules a record does not
 * carry ignore their view fields.
 */
struct StaPipRecordView {
  const Plane* frustum = nullptr;  // the view's 6 world-space planes
  float eye[3] = {0.0F, 0.0F, 0.0F};
  // A record with group bits is drawn only when one of them is set here
  // (a record with group 0 is always eligible).
  u8 groupMask = 0xFF;
  // kRuleSplit: the split-screen half's two band planes through bandP.
  bool splitBand = false;
  float bandP[3] = {0.0F, 0.0F, 0.0F};
  float bandN[2][3] = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}};
  // kRuleGround: drop a record whose box is farther than this from the eye
  // (0 = off).
  float groundRadius = 0.0F;
  // kRuleExit: drop a record whose aux box lies wholly on the negative side
  // of this plane (n.p - d < -0.05), when exitPlane is set.
  bool exitPlane = false;
  float exitPlaneV[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  // kRuleRing: a record is drawn only when its cell lies in one of these
  // rects {x0, z0, x1, z1} (nRects 0 = rule off).
  const int (*rects)[4] = nullptr;
  int nRects = 0;
  // kRuleOcclude: asked last, only for a record every other rule passed.
  bool (*occluded)(void* ctx, const float* mn, const float* mx) = nullptr;
  void* occCtx = nullptr;
};

/**
 * Modified by TyraX: a retained list of draw records (docs/record-renderer.md).
 *
 * One record per drawable a producer owns (a terrain chunk's base pass, one
 * of its layer passes, a static batch, a road chunk...), at a POSITION the
 * producer chooses: the list order is the draw order. Records are written
 * when the producer's geometry or its draw rules change, not every frame.
 * A frame runs one visibility pass over the list (cull) and gets back the
 * visible positions in order.
 *
 * The columns are separate arrays (SoA): the pass reads the boxes and the
 * rule words contiguously, and touches a record's other columns only when a
 * rule needs them.
 *
 * Every rule reproduces a test the producer used to make in its own loop,
 * with the same arithmetic, so the visible set is the old loop's set
 * (the producers' verify builds compare the two every frame).
 */
class StaPipRecordList {
 public:
  enum : u16 {
    kRuleFrustum = 1,      // world box vs the view frustum (OUTSIDE drops)
    kRuleSplit = 2,        // world box vs the split band (view.splitBand)
    kRuleGround = 4,       // world box farther than view.groundRadius drops
    kRuleDistCentre = 8,   // |point0 - eye| > dist drops
    kRuleDistBox = 16,     // distance(eye, aux box) > dist drops
    kRuleLod = 32,         // (|point1 - eye| > lodDist) != (tier == 1) drops
    kRuleRing = 64,        // cell outside every view rect drops
    kRuleExit = 128,       // aux box behind the view's exit plane drops
    kRuleOcclude = 256,    // view.occluded(box) drops (asked last)
  };

  /** The record at `pos` (grows the list). A null bag empties it. */
  struct Record {
    StaPipBag* bag = nullptr;
    float mn[3] = {0.0F, 0.0F, 0.0F}, mx[3] = {0.0F, 0.0F, 0.0F};
    u16 rules = kRuleFrustum;
    u8 group = 0;
    s8 lodTier = -1;
    int cx = 0, cz = 0;
    // The aux box (kRuleDistBox, kRuleExit) and the two points
    // (kRuleDistCentre: point0, kRuleLod: point1).
    float auxMn[3] = {0.0F, 0.0F, 0.0F}, auxMx[3] = {0.0F, 0.0F, 0.0F};
    float point0[3] = {0.0F, 0.0F, 0.0F}, point1[3] = {0.0F, 0.0F, 0.0F};
    float dist = 0.0F, lodDist = 0.0F;
  };

  void clear();
  void resize(u32 n);
  void set(u32 pos, const Record& r);
  void erase(u32 pos);
  u32 size() const { return static_cast<u32>(bags.size()); }
  StaPipBag* bag(u32 pos) const { return bags[pos]; }
  /** Records currently holding a bag. */
  u32 live() const { return liveCount; }

  /**
   * The visibility pass: every record's rules against `view`, in list
   * order. Writes the visible positions to `out` (size() entries at most)
   * and returns their number. vis(pos) is the frustum verdict of the last
   * pass for a visible record (IN_FRUSTUM or PARTIALLY_IN_FRUSTUM).
   */
  u32 cull(const StaPipRecordView& view, u32* out);
  u8 vis(u32 pos) const { return verdict[pos]; }

  /** True when `pos` holds exactly `r` (the verify builds' record check). */
  bool equals(u32 pos, const Record& r) const;
  /** Positions whose records differ from `o`'s (the verify builds compare
   * the retained list with one rebuilt from the live producer state). The
   * first differing position goes to *first when it is non-null. */
  u32 diff(const StaPipRecordList& o, u32* first = nullptr) const;

  /**
   * The producer's sync token: the value of whatever revision the list was
   * last rebuilt from (a DrawEpochs class epoch, a scene generation...).
   * The producer compares and rebuilds; the list never reads it.
   */
  u32 syncToken = 0;
  bool synced = false;

 private:
  struct alignas(16) Box {
    Vec4 lo, hi;
  };
  struct Meta {
    u16 rules;
    u8 group;
    s8 lodTier;
    s16 cx, cz;
    u32 sameBox;  // 1: the box and rules equal the previous record's
  };
  struct alignas(16) Aux {
    float auxMn[3], dist;
    float auxMx[3], lodDist;
    float point0[3], pad0;
    float point1[3], pad1;
  };
  std::vector<Box> boxes;
  std::vector<Meta> metas;
  std::vector<Aux> auxes;
  std::vector<StaPipBag*> bags;
  std::vector<u8> verdict;
  u32 liveCount = 0;
  void fixSame(u32 pos);
};

}  // namespace Tyra
