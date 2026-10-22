#pragma once
#include "debug/offline_vif.hpp"
#include "renderer/core/gs/renderer_core_depth.hpp"
#include "offline_terrain.hpp"
#include "offline_vif_tables.hpp"
#include "offline_vif_bindings.hpp"

namespace OfflineVif {
inline const OfflineVifTables::Entry* current=nullptr;
inline unsigned currentEdges[4];
inline void load(){OfflineVifTables::load();}
inline bool append(unsigned pass,unsigned address,unsigned capacity,unsigned count,
                   unsigned run,bool textured,unsigned primitive,OfflineVifEngine::Body& body,unsigned budget){
  TYRA_ASSERT(current && pass<2,"Offline VIF context");
  const unsigned index=(current->cz*OfflineVifTables::nx+current->cx)*3+
    (current->lod==1?0:current->lod==2?1:2);
  const auto* signature=bindings[index];
  TYRA_ASSERT(pass<signature[0],"Offline VIF pass count");
  const unsigned actual[]={address,capacity,count,run,unsigned(textured)};
  TYRA_ASSERT(!memcmp(actual,signature+1+pass*5,20),"Offline VIF native program/layout binding");
  const auto& source=current->streams[pass];
  unsigned depth;memcpy(&depth,&Tyra::RendererCoreDepth::scale,4);
  TYRA_ASSERT(source.size()>=12 && source[4]==0x45000000 && source[5]==0x45000000 &&
    source[6]==depth && ((source[9]>>15)&2047)==primitive,"Offline VIF depth/primitive admission");
  TYRA_ASSERT(!source.empty() && source.size()%4==0 && body.size()<=budget &&
    source.size()/4<=budget-body.size(),"Offline VIF body budget");
  const unsigned begin=body.size();body.resize(begin+source.size()/4);
  auto* dst=reinterpret_cast<unsigned char*>(body.data()+begin);
  memcpy(dst,source.data(),source.size()*4);
  for(unsigned e=0;e<4;++e)if(currentEdges[e]>current->lod){
    const auto& changes=current->patches[e*2+(currentEdges[e]==4)][pass];
    for(const auto& c:changes)memcpy(dst+c.offset,&c.value,4);
  }
  ++OfflineVifEngine::restores;return true;
}
struct Context {
  Context(int cx,int cz,int lod,int w,int e,int n,int s){
    TYRA_ASSERT(!OfflineVifEngine::append && !current,"Offline VIF nested compiler");
    if(!OfflineTerrain::enabled)return;
    current=OfflineVifTables::find(0,cx,cz,lod);
    TYRA_ASSERT(current,"Offline VIF absent cell");
    const unsigned neighbours[]={unsigned(w),unsigned(e),unsigned(n),unsigned(s)};
    const bool boundary[]={cx==0,unsigned(cx+1)==OfflineVifTables::nx,cz==0,unsigned(cz+1)==OfflineVifTables::nz};
    for(unsigned i=0;i<4;++i)currentEdges[i]=boundary[i]?unsigned(lod):std::max(unsigned(lod),neighbours[i]);
    OfflineVifTables::bounds(*current,currentEdges); // Validate complete variant admission.
    OfflineVifEngine::append=append;
  }
  ~Context(){OfflineVifEngine::append=nullptr;current=nullptr;}
};
}
