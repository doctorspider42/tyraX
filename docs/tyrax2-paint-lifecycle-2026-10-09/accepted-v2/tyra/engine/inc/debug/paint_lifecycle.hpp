#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>

namespace PaintLife {
struct State {
  const void* source = nullptr;
  unsigned count = 0, stamp = 0;
  float forward[3]{}, half[3]{};
  bool valid = false, eeReady = false;
  bool changed(const void* p, unsigned n, unsigned version) const {
    return !valid || source != p || count != n || stamp != version;
  }
  void accept(const void* p, unsigned n, unsigned version,
              const float* f, const float* h) {
    source=p; count=n; stamp=version;
    std::memcpy(forward,f,sizeof(forward)); std::memcpy(half,h,sizeof(half));
    valid=true; eeReady=false;
  }
  bool needsEe(bool vu) const { return valid && !vu && !eeReady; }
};

inline unsigned mode=0, visits=0, errors=0, modeMask=0, lodMask=0;
inline unsigned accepts=0, refreshes=0, heldRefreshes=0, legacy=0, custom=0;
inline unsigned reloads=0;
inline void expected(const float* n, const State& state, float* out) {
  float d=n[0]*state.forward[0]+n[1]*state.forward[1]+n[2]*state.forward[2];
  out[0]=out[1]=out[2]=128.0F*(.30F+.70F*(1.0F-std::fabs(d)));
  float s=n[0]*state.half[0]+n[1]*state.half[1]+n[2]*state.half[2];
  if(s<0) s=0;
  s*=s; s*=s; s*=s;
  out[3]=1.0F+220.0F*s;
  if(out[3]>255) out[3]=255;
}
inline void check(const State& state, bool vu, bool accepted, bool refreshed,
                  unsigned lod, const void* normals, const void* colors, unsigned n) {
  ++visits; modeMask|=1U<<mode; if(lod<32) lodMask|=1U<<lod;
  accepts+=accepted; refreshes+=refreshed; heldRefreshes+=refreshed&&!accepted;
  const bool want=mode!=0 && mode!=2 && mode!=4;
  if(!state.valid || vu!=want || (!vu&&!state.eeReady) || (vu&&refreshed)) ++errors;
  if(mode==2&&!vu) ++legacy;
  if(mode==4&&!vu) ++custom;
  if(vu) return;
  for(unsigned i=0;i<n;++i) {
    float normal[4],actual[4],reference[4];
    std::memcpy(normal,static_cast<const char*>(normals)+i*16,16);
    std::memcpy(actual,static_cast<const char*>(colors)+i*16,16);
    expected(normal,state,reference);
    for(unsigned k=0;k<4;++k)
      if(!std::isfinite(actual[k]) || std::fabs(actual[k]-reference[k])>.001F) ++errors;
  }
}
inline void report(unsigned phase) {
  std::printf("PAINTLIFE phase=%u visits=%u accepts=%u refreshes=%u held=%u modes=%u lods=%u legacy=%u custom=%u errors=%u\n",
              phase,visits,accepts,refreshes,heldRefreshes,modeMask,lodMask,legacy,custom,errors);
  visits=errors=modeMask=lodMask=accepts=refreshes=heldRefreshes=legacy=custom=0;
}
}
