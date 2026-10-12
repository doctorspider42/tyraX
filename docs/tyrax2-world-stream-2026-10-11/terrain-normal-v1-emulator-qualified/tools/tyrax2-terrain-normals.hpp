#pragma once
#ifdef TERRAIN_NORMALS_HOST
#include <cassert>
#define TYRA_ASSERT(condition, ...) assert(condition)
#else
#include "debug/debug.hpp"
#endif
#include <vector>
#include <cstdio>
#include <cstring>
namespace TerrainNormals {
struct Normal { float x,y,z; };
inline bool enabled=false;
inline unsigned generation=~0U,builds=0,uses=0,audits=0;
inline int width=0,depth=0;
inline const float* heights=nullptr;
inline float dx=0,dz=0;
inline std::vector<Normal> source;
template<class Compute> inline void ensure(unsigned gen,const float* h,int w,int d,
                                         float sx,float sz,Compute compute){
  if(generation==gen && heights==h && width==w && depth==d && dx==sx && dz==sz)return;
  TYRA_ASSERT(w>1 && d>1,"Terrain normal lattice dimensions");
  source.resize(static_cast<size_t>(w)*d);
  for(int z=0;z<d;++z)for(int x=0;x<w;++x){
    const auto n=compute(x,z);source[static_cast<size_t>(z)*w+x]={n.x,n.y,n.z};
  }
  // Native original arithmetic also checks indexing/storage before price.
  unsigned checked=0;
  for(int z=0;z<d;++z)for(int x=0;x<w;++x){
    const auto n=compute(x,z);const Normal expected={n.x,n.y,n.z};
    TYRA_ASSERT(!std::memcmp(&expected,&source[static_cast<size_t>(z)*w+x],sizeof(Normal)),
                "Terrain original normal equality");++checked;
  }
  generation=gen;heights=h;width=w;depth=d;dx=sx;dz=sz;audits+=checked;
  std::printf("NORMALSOURCE generation=%u samples=%u bytes=%u exact=1\n",gen,checked,
              unsigned(source.size()*sizeof(Normal)));std::fflush(stdout);
}
inline Normal at(int x,int z){return source[static_cast<size_t>(z)*width+x];}
inline void reset(){builds=uses=audits=0;}
inline void witness(unsigned phase,int arm=-1){
  std::printf("NORMALWITNESS phase=%u arm=%d enabled=%u builds=%u uses=%u audits=%u bytes=%u\n",
    phase,arm,unsigned(enabled),builds,uses,audits,unsigned(source.size()*sizeof(Normal)));
  std::fflush(stdout);
}
}
