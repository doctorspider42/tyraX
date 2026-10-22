#pragma once
#include <cmath>

// Private A16 OUTSIDE-only gate. The VU output uses scale 2048 and the env
// raster is centered on size/2; retain a full pixel beyond each scissor edge.
namespace TargetReject {
inline unsigned outside(const float* m, const float* lo, const float* hi,
                        float size) {
  if (!std::isfinite(size) || size < 1 || size > 2048) return 0;
  for (unsigned i=0;i<16;++i) if (!std::isfinite(m[i])) return 0;
  for (unsigned i=0;i<3;++i)
    if (!std::isfinite(lo[i]) || !std::isfinite(hi[i]) || lo[i]>hi[i]) return 0;
  const float band=(size*.5F+1.0F)/2048.0F;
  for (unsigned p=0;p<4;++p) {
    const unsigned row=p/2;
    const float sign=(p&1)?1.0F:-1.0F;
    float value=m[15]*band+sign*m[12+row];
    float magnitude=std::fabs(m[15]*band)+std::fabs(m[12+row])+1.0F;
    for (unsigned i=0;i<3;++i) {
      const float a=m[4*i+3]*band, b=sign*m[4*i+row], c=a+b;
      value += c*(c>=0?hi[i]:lo[i]);
      magnitude += (std::fabs(a)+std::fabs(b))*
                   (std::fabs(lo[i])+std::fabs(hi[i]));
    }
    if (std::isfinite(value) && std::isfinite(magnitude) &&
        value < -8.0e-6F*magnitude) return p+1;
  }
  return 0;
}
}
