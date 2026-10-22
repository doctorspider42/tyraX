#pragma once
#include "debug/debug.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

// Private fixture ABI. All bytes are owned after cold loading. Restored BagArrays
// retain their ordinary stamp and queue-snapshot rules; these tables are never
// borrowed as DMA payloads.
namespace OfflineTerrain {
inline bool enabled=false,forcing=false;
inline int own=1,edges[4]={1,1,1,1};
inline unsigned nx=0,nz=0,restores=0,fallbacks=0,residentBytes=0;
struct Change{unsigned offset,value;};
using Patch=std::array<std::vector<Change>,6>;
struct Bounds{unsigned edges[4];float values[8];};
struct Entry{
  unsigned cx,cz,lod;
  float bounds[8];
  std::array<std::vector<unsigned>,6> streams;
  std::array<Patch,8> patches;
  std::vector<Bounds> variants;
};
inline std::vector<Entry> entries;
inline unsigned readWord(FILE* f){unsigned v;TYRA_ASSERT(fread(&v,4,1,f)==1,"Offline terrain truncated word");return v;}
inline void readBytes(FILE* f,void* p,unsigned n){TYRA_ASSERT(!n || fread(p,n,1,f)==1,"Offline terrain truncated bytes");residentBytes+=n;}
inline void load(){
  if(!entries.empty())return;
  FILE* f=fopen("host:offline-terrain.bin","rb");TYRA_ASSERT(f,"Offline terrain compiled input missing");
  TYRA_ASSERT(readWord(f)==0x3242544f && readWord(f)==2 && readWord(f)==0,"Offline terrain format");
  nx=readWord(f);nz=readWord(f);const unsigned count=readWord(f);
  TYRA_ASSERT(nx && nz && nx<=64 && nz<=64 && count==nx*nz*3,"Offline terrain dimensions");
  entries.resize(count);
  for(unsigned i=0;i<count;++i){
    auto& e=entries[i];e.cx=readWord(f);e.cz=readWord(f);e.lod=readWord(f);
    TYRA_ASSERT(e.cx==(i/3)%nx && e.cz==(i/3)/nx && e.lod==(1U<<(i%3)),"Offline terrain entry order");
    readBytes(f,e.bounds,sizeof(e.bounds));
    for(auto& stream:e.streams){const unsigned bytes=readWord(f);TYRA_ASSERT(bytes%16==0 && bytes<=1024*1024,"Offline terrain stream extent");stream.resize(bytes/4);readBytes(f,stream.data(),bytes);}
    for(auto& patch:e.patches)for(unsigned s=0;s<6;++s){
      const unsigned n=readWord(f);TYRA_ASSERT(n<=e.streams[s].size(),"Offline terrain patch length");patch[s].resize(n);
      unsigned previous=0;
      for(unsigned j=0;j<n;++j){auto& c=patch[s][j];c.offset=readWord(f);c.value=readWord(f);
        TYRA_ASSERT(c.offset%4==0 && c.offset/4<e.streams[s].size() && (!j || c.offset>previous),"Offline terrain patch offset");previous=c.offset;}
      residentBytes+=n*sizeof(Change);
    }
    const unsigned variants=readWord(f);TYRA_ASSERT(variants && variants<=81,"Offline terrain variant count");
    e.variants.resize(variants);
    for(auto& b:e.variants){for(auto& edge:b.edges){edge=readWord(f);TYRA_ASSERT(edge==1 || edge==2 || edge==4,"Offline terrain neighbour");}readBytes(f,b.values,sizeof(b.values));}
  }
  TYRA_ASSERT(readWord(f)==0x454e4432 && fgetc(f)==EOF,"Offline terrain footer");fclose(f);
  unsigned storage=entries.capacity()*sizeof(Entry);
  for(const auto& e:entries){
    for(const auto& stream:e.streams)storage+=stream.capacity()*sizeof(unsigned);
    for(const auto& patch:e.patches)for(const auto& changes:patch)storage+=changes.capacity()*sizeof(Change);
    storage+=e.variants.capacity()*sizeof(Bounds);
  }
  printf("OFFLINELOAD entries=%u streamPatchBytes=%u capacityBytes=%u allocatorOverheadExcluded=1 cold=1\n",unsigned(entries.size()),residentBytes,storage);fflush(stdout);
}
inline const Entry* find(int scene,int cx,int cz,int lod){
  if(scene!=0 || cx<0 || cz<0 || unsigned(cx)>=nx || unsigned(cz)>=nz || (lod!=1 && lod!=2 && lod!=4))return nullptr;
  const unsigned index=(unsigned(cz)*nx+unsigned(cx))*3+(lod==1?0:lod==2?1:2);
  return index<entries.size()?&entries[index]:nullptr;
}
template<class Array>
void restore(Array& out,const Entry& e,unsigned stream,const unsigned edge[4]){
  const auto& source=e.streams[stream];
  out.resize(source.size()*4/sizeof(*out.data()));
  if(source.empty())return;
  unsigned char* dst=reinterpret_cast<unsigned char*>(&out[0]);
  memcpy(dst,source.data(),source.size()*4);
  for(unsigned i=0;i<4;++i)if(edge[i]>e.lod){
    const auto& patch=e.patches[i*2+(edge[i]==4)][stream];
    for(const auto& c:patch)memcpy(dst+c.offset,&c.value,4);
  }
}
inline const float* bounds(const Entry& e,const unsigned edge[4]){
  for(const auto& b:e.variants)if(memcmp(edge,b.edges,16)==0)return b.values;
  TYRA_ASSERT(false,"Offline terrain absent edge combination");return nullptr;
}
}
