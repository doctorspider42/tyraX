/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Modified by TyraX - new file: the retained draw records
# (docs/record-renderer.md, phase 1).
*/

#include "renderer/3d/pipeline/static/core/stapip_records.hpp"
#include <math.h>
#include <string.h>

namespace Tyra {

void StaPipRecordList::clear() {
  boxes.clear();
  metas.clear();
  auxes.clear();
  bags.clear();
  verdict.clear();
  liveCount = 0;
  synced = false;
}

void StaPipRecordList::resize(u32 n) {
  if (n < bags.size())
    for (u32 i = n; i < bags.size(); ++i)
      if (bags[i] != nullptr) --liveCount;
  boxes.resize(n);
  metas.resize(n, Meta{0, 0, -1, 0, 0, 0});
  auxes.resize(n);
  bags.resize(n, nullptr);
  verdict.resize(n, 0);
  if (n > 0) fixSame(n - 1);
}

void StaPipRecordList::erase(u32 pos) {
  if (pos >= bags.size()) return;
  if (bags[pos] != nullptr) --liveCount;
  bags[pos] = nullptr;
  fixSame(pos);
  if (pos + 1 < bags.size()) fixSame(pos + 1);
}

void StaPipRecordList::set(u32 pos, const Record& r) {
  if (pos >= bags.size()) resize(pos + 1);
  if (r.bag == nullptr) {
    erase(pos);
    return;
  }
  if (bags[pos] == nullptr) ++liveCount;
  bags[pos] = r.bag;
  Box& b = boxes[pos];
  b.lo = Vec4(r.mn[0], r.mn[1], r.mn[2], 1.0F);
  b.hi = Vec4(r.mx[0], r.mx[1], r.mx[2], 1.0F);
  Meta& m = metas[pos];
  m.rules = r.rules;
  m.group = r.group;
  m.lodTier = r.lodTier;
  m.cx = static_cast<s16>(r.cx);
  m.cz = static_cast<s16>(r.cz);
  Aux& a = auxes[pos];
  memset(&a, 0, sizeof(a));
  for (int k = 0; k < 3; ++k) {
    a.auxMn[k] = r.auxMn[k];
    a.auxMx[k] = r.auxMx[k];
    a.point0[k] = r.point0[k];
    a.point1[k] = r.point1[k];
  }
  a.dist = r.dist;
  a.lodDist = r.lodDist;
  fixSame(pos);
  if (pos + 1 < bags.size()) fixSame(pos + 1);
}

// sameBox: the record's whole box-level decision is the previous record's
// (every field but the bag and the group equal). Never for an occluded
// record: the occlusion test has side effects (its counters) per draw.
void StaPipRecordList::fixSame(u32 pos) {
  Meta& m = metas[pos];
  m.sameBox = 0;
  if (pos == 0 || bags[pos] == nullptr || bags[pos - 1] == nullptr) return;
  const Meta& p = metas[pos - 1];
  if (m.rules != p.rules || (m.rules & kRuleOcclude) || m.lodTier != p.lodTier ||
      m.cx != p.cx || m.cz != p.cz)
    return;
  if (memcmp(&boxes[pos], &boxes[pos - 1], sizeof(Box)) != 0) return;
  if (memcmp(&auxes[pos], &auxes[pos - 1], sizeof(Aux)) != 0) return;
  m.sameBox = 1;
}

bool StaPipRecordList::equals(u32 pos, const Record& r) const {
  if (pos >= bags.size()) return r.bag == nullptr;
  if (bags[pos] != r.bag) return false;
  if (r.bag == nullptr) return true;
  const Box& b = boxes[pos];
  const Meta& m = metas[pos];
  const Aux& a = auxes[pos];
  if (b.lo.x != r.mn[0] || b.lo.y != r.mn[1] || b.lo.z != r.mn[2] ||
      b.hi.x != r.mx[0] || b.hi.y != r.mx[1] || b.hi.z != r.mx[2])
    return false;
  if (m.rules != r.rules || m.group != r.group || m.lodTier != r.lodTier ||
      m.cx != static_cast<s16>(r.cx) || m.cz != static_cast<s16>(r.cz))
    return false;
  for (int k = 0; k < 3; ++k)
    if (a.auxMn[k] != r.auxMn[k] || a.auxMx[k] != r.auxMx[k] ||
        a.point0[k] != r.point0[k] || a.point1[k] != r.point1[k])
      return false;
  return a.dist == r.dist && a.lodDist == r.lodDist;
}

u32 StaPipRecordList::diff(const StaPipRecordList& o, u32* first) const {
  const u32 n = size() > o.size() ? size() : o.size();
  u32 bad = 0;
  for (u32 i = 0; i < n; ++i) {
    const StaPipBag* a = i < size() ? bags[i] : nullptr;
    const StaPipBag* b = i < o.size() ? o.bags[i] : nullptr;
    bool same = a == b;
    if (same && a != nullptr) {
      const Meta& m = metas[i];
      const Meta& p = o.metas[i];
      same = memcmp(&boxes[i], &o.boxes[i], sizeof(Box)) == 0 &&
             memcmp(&auxes[i], &o.auxes[i], sizeof(Aux)) == 0 &&
             m.rules == p.rules && m.group == p.group &&
             m.lodTier == p.lodTier && m.cx == p.cx && m.cz == p.cz;
    }
    if (!same) {
      if (bad == 0 && first != nullptr) *first = i;
      ++bad;
    }
  }
  return bad;
}

u32 StaPipRecordList::cull(const StaPipRecordView& v, u32* out) {
  const u32 n = static_cast<u32>(bags.size());
  u32 count = 0;
  // The box-level decision of the current run of sameBox records.
  bool runValid = false, runPass = false;
  u8 runVerdict = 0;
  for (u32 i = 0; i < n; ++i) {
    StaPipBag* bag = bags[i];
    if (bag == nullptr) {
      runValid = false;
      continue;
    }
    const Meta& m = metas[i];
    if (!m.sameBox) runValid = false;
    if (m.group != 0 && (m.group & v.groupMask) == 0) continue;
    if (runValid) {
      if (runPass) {
        verdict[i] = runVerdict;
        out[count++] = i;
      }
      continue;
    }
    runValid = true;
    runPass = false;
    const u16 rules = m.rules;
    const Box& b = boxes[i];
    const float mn[3] = {b.lo.x, b.lo.y, b.lo.z};
    const float mx[3] = {b.hi.x, b.hi.y, b.hi.z};
    if ((rules & kRuleRing) && v.nRects > 0) {
      bool inRing = false;
      for (int r = 0; r < v.nRects && !inRing; ++r)
        inRing = m.cx >= v.rects[r][0] && m.cx <= v.rects[r][2] &&
                 m.cz >= v.rects[r][1] && m.cz <= v.rects[r][3];
      if (!inRing) continue;
    }
    if (rules & (kRuleExit | kRuleDistBox | kRuleDistCentre | kRuleLod)) {
      const Aux& a = auxes[i];
      if ((rules & kRuleExit) && v.exitPlane) {
        const float* p = v.exitPlaneV;
        const float pvx = p[0] > 0.0F ? a.auxMx[0] : a.auxMn[0];
        const float pvy = p[1] > 0.0F ? a.auxMx[1] : a.auxMn[1];
        const float pvz = p[2] > 0.0F ? a.auxMx[2] : a.auxMn[2];
        if (p[0] * pvx + p[1] * pvy + p[2] * pvz - p[3] < -0.05F) continue;
      }
      if (rules & kRuleLod) {
        const float dx = a.point1[0] - v.eye[0];
        const float dy = a.point1[1] - v.eye[1];
        const float dz = a.point1[2] - v.eye[2];
        const bool isFar = dx * dx + dy * dy + dz * dz > a.lodDist * a.lodDist;
        if (isFar != (m.lodTier == 1)) continue;
      }
      if (rules & kRuleDistBox) {
        float d2 = 0.0F;
        for (int k = 0; k < 3; ++k) {
          const float e = v.eye[k] < a.auxMn[k]   ? a.auxMn[k] - v.eye[k]
                          : v.eye[k] > a.auxMx[k] ? v.eye[k] - a.auxMx[k]
                                                  : 0.0F;
          d2 += e * e;
        }
        if (d2 > a.dist * a.dist) continue;
      }
      if (rules & kRuleDistCentre) {
        const float dx = a.point0[0] - v.eye[0];
        const float dy = a.point0[1] - v.eye[1];
        const float dz = a.point0[2] - v.eye[2];
        if (dx * dx + dy * dy + dz * dz > a.dist * a.dist) continue;
      }
    }
    if ((rules & kRuleSplit) && v.splitBand) {
      const float cx = 0.5F * (mn[0] + mx[0]) - v.bandP[0];
      const float cy = 0.5F * (mn[1] + mx[1]) - v.bandP[1];
      const float cz = 0.5F * (mn[2] + mx[2]) - v.bandP[2];
      const float ex = 0.5F * (mx[0] - mn[0]);
      const float ey = 0.5F * (mx[1] - mn[1]);
      const float ez = 0.5F * (mx[2] - mn[2]);
      bool outside = false;
      for (int p = 0; p < 2 && !outside; ++p) {
        const float* nn = v.bandN[p];
        const float r = ex * fabsf(nn[0]) + ey * fabsf(nn[1]) + ez * fabsf(nn[2]);
        if (cx * nn[0] + cy * nn[1] + cz * nn[2] + r < 0.0F) outside = true;
      }
      if (outside) continue;
    }
    if ((rules & kRuleGround) && v.groundRadius > 0.0F) {
      const float px = v.eye[0], py = v.eye[1], pz = v.eye[2];
      const float dx = px < mn[0] ? mn[0] - px : (px > mx[0] ? px - mx[0] : 0.0F);
      const float dy = py < mn[1] ? mn[1] - py : (py > mx[1] ? py - mx[1] : 0.0F);
      const float dz = pz < mn[2] ? mn[2] - pz : (pz > mx[2] ? pz - mx[2] : 0.0F);
      if (dx * dx + dy * dy + dz * dz > v.groundRadius * v.groundRadius) continue;
    }
    u8 vis = PARTIALLY_IN_FRUSTUM;
    if (rules & kRuleFrustum) {
      const CoreBBoxFrustum f = CoreBBox::frustumCheckAABB(v.frustum, b.lo, b.hi);
      if (f == OUTSIDE_FRUSTUM) continue;
      vis = static_cast<u8>(f);
    }
    if ((rules & kRuleOcclude) && v.occluded != nullptr &&
        v.occluded(v.occCtx, mn, mx))
      continue;
    runPass = true;
    runVerdict = vis;
    verdict[i] = vis;
    out[count++] = i;
  }
  return count;
}

}  // namespace Tyra
