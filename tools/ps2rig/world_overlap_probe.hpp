// Private schedule diagnostics; all file writes occur after the phase fence.
#pragma once
#include <cstdio>
#include <vector>
namespace Vehicle_playground { namespace WorldOverlapProbe {
struct Row {
  unsigned world=0,packet=0,busyBefore=0,busyAfter=0,packetWorldTicks=0;
  unsigned contacts=0,hashA=2166136261u,hashB=0x9e3779b9u;
};
inline std::vector<Row> rows;
inline Row& row(){if(rows.size()<=size_t(CB::frame))rows.resize(size_t(CB::frame)+1);return rows[size_t(CB::frame)];}
inline unsigned busy(){unsigned v;asm volatile("cfc2 %0,$29":"=r"(v)::"memory");return v&1;}
inline unsigned beginWorld(bool packet){if(!Tyra::vu0SatVerify)return 0;auto& r=row();++r.world;if(packet){++r.packet;r.busyBefore+=busy();}return CB::ticks();}
inline void endWorld(bool packet,unsigned begin){if(Tyra::vu0SatVerify && packet){auto& r=row();r.packetWorldTicks+=CB::ticks()-begin;r.busyAfter+=busy();}}
inline void word(Row& r,unsigned v){r.hashA=(r.hashA^v)*16777619u;r.hashB^=v+0x9e3779b9u+(r.hashB<<6)+(r.hashB>>2);}
inline void real(Row& r,float v){unsigned u;__builtin_memcpy(&u,&v,4);word(r,u);}
template<class Contacts>void capture(const Contacts& contacts){
  if(!Tyra::vu0SatVerify)return;
  auto& r=row();r.contacts=unsigned(contacts.size());word(r,r.contacts);
  for(const auto& c:contacts){word(r,unsigned(c.a));word(r,unsigned(c.b));word(r,unsigned(c.other));
    real(r,c.n.x);real(r,c.n.y);real(r,c.n.z);real(r,c.p.x);real(r,c.p.y);real(r,c.p.z);real(r,c.depth);}
}
inline void flush(int phase){
  char name[80];snprintf(name,sizeof(name),"world-overlap-%d.csv",phase);
  FILE* f=fopen(name,"w");unsigned world=0,packet=0,before=0,after=0;
  if(f)fprintf(f,"frame,world,packet,busy_before,busy_after,packet_world_ticks,contacts,hash_a,hash_b\n");
  for(size_t i=0;i<rows.size();++i){const auto& r=rows[i];world+=r.world;packet+=r.packet;before+=r.busyBefore;after+=r.busyAfter;
    if(f)fprintf(f,"%u,%u,%u,%u,%u,%u,%u,%u,%u\n",unsigned(i),r.world,r.packet,r.busyBefore,r.busyAfter,r.packetWorldTicks,r.contacts,r.hashA,r.hashB);}
  if(f)fclose(f);
  printf("WORLDOVERLAP phase=%d world=%u packet=%u busy_before=%u busy_after=%u file=%d\n",phase,world,packet,before,after,f!=nullptr);fflush(stdout);rows.clear();
}
} }
