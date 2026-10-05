#pragma once
// Modified by TyraX: PRIVATE package-local exact pool-lattice representation.
#include <cstdint>
#include <cstring>
#include "stapip_pool_color_table.hpp"
namespace Tyra { namespace ExperimentalPoolLattice {
using Bits = ExperimentalPoolTable::ColorBits;
struct alignas(16) Package {
 Bits descriptor{}, color{};
 Bits positions[25]{}, sts[25]{}, triangles[25]{};
};
inline uint32_t gridIndex(uint32_t occurrence) {
 const uint32_t cell=occurrence/6u, corner=occurrence%6u;
 const uint32_t a=(cell/4u)*5u+cell%4u;
 const uint32_t delta[6]={0,1,6,0,6,5};
 return a+delta[corner];
}
inline bool finite(const Bits& value) {
 for(unsigned i=0;i<4;++i)
  if((value.word[i]&0x7f800000u)==0x7f800000u) return false;
 return true;
}
inline uint32_t inputExtent(uint32_t unique,uint32_t triangles) {
 return 4u+2u*unique+triangles;
}
inline uint32_t totalExtent(uint32_t unique,uint32_t triangles,uint32_t count) {
 return inputExtent(unique,triangles)+3u*unique+9u+3u*count;
}
// Source topology is the original96-corner4x4 patch, not coordinate dedup.
// All four lanes must agree for every reused source point. On rejection the
// scratch contents are unspecified and MUST NOT be serialized.
inline bool prepare(const void* positions,const void* sts,uint32_t sourceOffset,
                    uint32_t count,const ExperimentalPoolTable::Slice& colors,
                    Package& out) {
 if(!positions || !sts || count==0 || count>75 || count%3 || sourceOffset%3 ||
    colors.descriptor[0]!=count || colors.descriptor[1]!=count ||
    colors.descriptor[2]!=1 || colors.descriptor[3]!=0 ||
    sourceOffset%96u+count>96u) return false; // crossing stays Pool2
 if(!finite(colors.colors[0]) || colors.colors[0].word[3]!=0x43000000u)
  return false;
 uint32_t map[25];for(auto& x:map)x=~uint32_t(0);
 uint32_t unique=0;
 const auto* p=static_cast<const unsigned char*>(positions);
 const auto* s=static_cast<const unsigned char*>(sts);
 for(uint32_t i=0;i<count;++i) {
  const uint32_t grid=gridIndex(sourceOffset%96u+i);
  if(grid>=25) return false;
  Bits position,st;std::memcpy(&position,p+16u*i,16);
  std::memcpy(&st,s+16u*i,16);
  if(!finite(position) || !finite(st) || position.word[3]!=0x3f800000u)
   return false;
  uint32_t index=map[grid];
  if(index==~uint32_t(0)) {
   if(unique>=25)return false;
   index=unique++;map[grid]=index;
   out.positions[index]=position;out.sts[index]=st;
  } else if(std::memcmp(&position,&out.positions[index],16) ||
            std::memcmp(&st,&out.sts[index],16)) return false;
  out.triangles[i/3u].word[i%3u]=index;
  out.triangles[i/3u].word[3]=0;
 }
 const uint32_t tris=count/3u;
 if(unique>=count || totalExtent(unique,tris,count)>461u) return false;
 out.color=colors.colors[0];
 out.descriptor.word[0]=unique;out.descriptor.word[1]=tris;
 out.descriptor.word[2]=inputExtent(unique,tris);
 out.descriptor.word[3]=inputExtent(unique,tris)+3u*unique;
 return true;
}
static_assert(alignof(Package)==16 && sizeof(Package)==1232,"Lattice REF owner");
} }
