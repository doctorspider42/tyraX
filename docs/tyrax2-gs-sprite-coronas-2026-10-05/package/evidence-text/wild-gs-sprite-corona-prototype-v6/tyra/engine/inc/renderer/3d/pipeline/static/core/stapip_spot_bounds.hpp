/*
# Modified by TyraX: conservative object-space rejection of a dynamic light
# whose vertex contribution is zero over the complete bag bounds.
*/
#pragma once
#include <math.h>
#include "./stapip_clipper.hpp"
#include "renderer/core/3d/bbox/core_bbox.hpp"

namespace Tyra {

// Uses the SAME object-space constants as the VU1 and EE clipper formula.
// No world-scale assumption: nonuniform models use buildSpotForBag's existing
// transform too. An interval expansion leaves borderline bags on the lit path.
inline bool stapipSpotHasNoInfluence(const StaPipClipperSpot& light,
                                     const CoreBBox& bounds) {
  if (!light.enabled) return true;
  const Vec4& lo = bounds[0];
  const Vec4& hi = bounds[7];
  float low[3] = {lo.x - light.position.x, lo.y - light.position.y,
                  lo.z - light.position.z};
  float high[3] = {hi.x - light.position.x, hi.y - light.position.y,
                   hi.z - light.position.z};
  const float dir[3] = {light.direction.x, light.direction.y, light.direction.z};
  float nearest2 = 0.0F, maxAxial = 0.0F;
  for (unsigned i = 0; i < 3; ++i) {
    const float margin = 0.001F * (1.0F + fabsf(low[i]) + fabsf(high[i]));
    low[i] -= margin;
    high[i] += margin;
    const float d = low[i] > 0.0F ? low[i] : (high[i] < 0.0F ? -high[i] : 0.0F);
    nearest2 += d * d;
    maxAxial += dir[i] * (dir[i] >= 0.0F ? high[i] : low[i]);
  }
  // All vertices are beyond the radial falloff. Extra slack covers differing
  // EE/VU rounding at the zero boundary; this is not a brightness threshold.
  if (nearest2 * light.invRange2 > 1.001F) return true;
  // Point lights encode the cone with a negative cutoff; only range rejects.
  if (light.cosCut2 <= 0.0F) return false;
  if (maxAxial <= 0.0F) return true;
  // t <= maxAxial and dist2 >= nearest2 everywhere in the expanded AABB.
  // Thus the cone numerator is nonpositive throughout a rejected box.
  return maxAxial * maxAxial < light.cosCut2 * nearest2 * 0.999F;
}

}  // namespace Tyra
