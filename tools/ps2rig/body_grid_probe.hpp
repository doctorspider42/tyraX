// Private fixed-snapshot grid experiment; run only after all VU packets drain.
#pragma once
#include "physics/broad_phase.hpp"
#include "file/file_utils.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace BodyGridProbe {
inline std::vector<Tyra::BroadBox> boxes,queries;
inline bool capture=false;
inline unsigned ticks(){unsigned t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");return t;}
inline bool overlap(const Tyra::BroadBox& a,const Tyra::BroadBox& b){
  for(int k=0;k<3;++k)if(a.mn[k]>b.mx[k] || a.mx[k]<b.mn[k])return false;
  return true;
}
inline unsigned hashBytes(unsigned h,const void* data,size_t bytes){
  const auto* p=static_cast<const unsigned char*>(data);
  for(size_t i=0;i<bytes;++i)h=(h^p[i])*16777619u;
  return h;
}
inline void begin(int phase,int frame,size_t count){
  capture=phase==13 && (frame==0 || frame==150 || frame==300 || frame==600);
  if(capture){boxes.resize(count);queries.clear();}
}
inline bool run(int phase,int frame){
  if(!capture)return true;
  capture=false;
  unsigned input=hashBytes(2166136261u,boxes.data(),boxes.size()*sizeof(boxes[0]));
  input=hashBytes(input,queries.data(),queries.size()*sizeof(queries[0]));
  char name[96];snprintf(name,sizeof(name),"body-grid-%d-%d.bin",phase,frame);
  FILE* f=fopen(Tyra::FileUtils::fromCwd(name).c_str(),"wb");
  const unsigned header[]={0x47524944u,1u,unsigned(boxes.size()),unsigned(queries.size()),unsigned(phase),unsigned(frame)};
  bool ok=f && fwrite(header,sizeof(header),1,f)==1 &&
    fwrite(boxes.data(),sizeof(boxes[0]),boxes.size(),f)==boxes.size() &&
    fwrite(queries.data(),sizeof(queries[0]),queries.size(),f)==queries.size();
  if(f)fclose(f);
  struct Sample{unsigned build,query,digest;};Sample samples[4][8]{};
  constexpr float cells[]={8,4,2,1};constexpr int Loops=16;
  for(int repeat=0;repeat<8;++repeat)for(int order=0;order<4;++order){
    const int mode=repeat%2?3-order:order;
    Tyra::BroadPhase grid(Tyra::BroadPhase::kGrid,cells[mode]);
    const unsigned start=ticks();grid.begin(int(boxes.size()));
    for(size_t i=0;i<boxes.size();++i)grid.set(int(i),boxes[i],false);
    grid.commit();const unsigned built=ticks();
    std::vector<int> out;unsigned digest=2166136261u;
    // Independent full walk outside timing, including exact sorted order.
    for(const auto& q:queries){grid.queryBox(q,out);std::vector<int> expected;
      for(size_t i=0;i<boxes.size();++i)if(overlap(q,boxes[i]))expected.push_back(int(i));
      // Wide always-candidates can be extra; apply the unchanged exact box test.
      out.erase(std::remove_if(out.begin(),out.end(),[&](int i){return !overlap(q,boxes[size_t(i)]);}),out.end());
      ok &= out==expected;
    }
    const unsigned qstart=ticks();
    for(int loop=0;loop<Loops;++loop)for(const auto& q:queries){grid.queryBox(q,out);
      for(int i:out)digest=(digest^unsigned(i))*16777619u;}
    samples[mode][repeat]={built-start,ticks()-qstart,digest};
  }
  snprintf(name,sizeof(name),"body-grid-%d-%d.csv",phase,frame);
  f=fopen(Tyra::FileUtils::fromCwd(name).c_str(),"wb");ok &= f!=nullptr;
  if(f){fprintf(f,"phase,frame,repeat,cell,objects,queries,loops,input_hash,build_ticks,query_ticks,digest\n");
    for(int m=0;m<4;++m)for(int r=0;r<8;++r){const auto& s=samples[m][r];
      fprintf(f,"%d,%d,%d,%.0f,%u,%u,%d,%u,%u,%u,%u\n",phase,frame,r,cells[m],unsigned(boxes.size()),unsigned(queries.size()),Loops,input,s.build,s.query,s.digest);}
    fclose(f);}
  printf("BODYGRID phase=%d frame=%d objects=%u queries=%u failures=%u\n",phase,frame,unsigned(boxes.size()),unsigned(queries.size()),ok?0u:1u);fflush(stdout);return ok;
}
}
