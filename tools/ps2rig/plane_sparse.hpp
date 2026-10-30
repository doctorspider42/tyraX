// Private exact sparse-plane experiment. No epsilon or bounding-box substitution.
#pragma once
#include "physics/convex_collision.hpp"
#include <cstdint>
#include <cstring>
namespace PlaneSparse {
inline bool classify(const float* planes,int count){
  if(count!=6)return false;
  static const float normals[18]={0,-1,0, 1,0,0, 0,0,-1, 0,0,1, -1,0,0, 0,1,0};
  for(int k=0;k<6;++k)for(int a=0;a<3;++a)
    if(planes[k*4+a]!=normals[k*3+a])return false;
  return true;
}
inline bool normal(float value){
  std::uint32_t bits;std::memcpy(&bits,&value,4);
  return ((bits&0x7fffffffu)-0x00800000u)<0x7f000000u;
}
inline bool normalPoint(Tyra::CollisionV3 p){return normal(p.x)&normal(p.y)&normal(p.z);}
// Caller must check exact plane classification and normalPoint first. Excluding
// zeros/subnormals/nonfinite coordinates preserves the original exceptional
// arithmetic and signed-zero behavior through its full scalar fallback.
inline int inside(const float* p,Tyra::CollisionV3 l,float* depth){
  int best=-1;float bd=1e30F;
  const float d0=p[3]+l.y;if(d0<=0)return -1;if(d0<bd){bd=d0;best=0;}
  const float d1=p[7]-l.x;if(d1<=0)return -1;if(d1<bd){bd=d1;best=1;}
  const float d2=p[11]+l.z;if(d2<=0)return -1;if(d2<bd){bd=d2;best=2;}
  const float d3=p[15]-l.z;if(d3<=0)return -1;if(d3<bd){bd=d3;best=3;}
  const float d4=p[19]+l.x;if(d4<=0)return -1;if(d4<bd){bd=d4;best=4;}
  const float d5=p[23]-l.y;if(d5<=0)return -1;if(d5<bd){bd=d5;best=5;}
  *depth=bd;return best;
}
} // namespace PlaneSparse
