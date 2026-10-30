// Private first-frame bit snapshots; only the phase flush performs I/O.
#pragma once
#include <cstdio>
#include <cstring>
#include <vector>
namespace Vehicle_playground { namespace StepPoolProbe {
struct Snapshot {int frame;std::vector<unsigned> words;};
inline std::vector<Snapshot> snapshots;
inline void real(std::vector<unsigned>& out,float value){unsigned v;std::memcpy(&v,&value,4);out.push_back(v);}
template<class V>void vec(std::vector<unsigned>& out,const V& v){real(out,v.x);real(out,v.y);real(out,v.z);}
template<class Steps,class Contacts,class Bodies>void capture(const Steps& steps,const Contacts& contacts,const Bodies& bodies){
  if(!Tyra::vu0SatVerify || CB::frame>1)return;
  snapshots.push_back({CB::frame,{}});auto& out=snapshots.back().words;
  out.push_back(unsigned(steps.size()));out.push_back(unsigned(contacts.size()));
  for(const auto& s:steps){
    for(int v:{s.obj,s.slot,int(s.asleep),int(s.grounded),int(s.sphere),s.nv,int(s.box)})out.push_back(unsigned(v));
    for(float v:{s.sphereR,s.invMass,s.bounce,s.friction})real(out,v);
    vec(out,s.x0);vec(out,s.x1);vec(out,s.v);vec(out,s.w);vec(out,s.vp);vec(out,s.wp);
    for(float v:s.q0)real(out,v);
    for(float v:s.R0)real(out,v);
    for(float v:s.R1)real(out,v);
    for(float v:s.invIw)real(out,v);
    vec(out,s.bounds.mn);vec(out,s.bounds.mx);
    if(s.box){vec(out,s.obb.centre);for(const auto& v:s.obb.halfAxes)vec(out,v);}
    else for(int k=0;k<12;++k)out.push_back(0);
    const auto& b=bodies[size_t(s.slot)];
    for(int k=0;k<s.nv*3;++k)real(out,b.lv[k]);
    for(int k=0;k<s.nv;++k)vec(out,s.wv0[k]);
    for(int k=0;k<s.nv;++k)vec(out,s.wv[k]);
  }
  for(const auto& c:contacts){out.push_back(unsigned(c.a));out.push_back(unsigned(c.b));out.push_back(unsigned(c.other));vec(out,c.n);vec(out,c.p);real(out,c.depth);}
}
inline void flush(int phase){
  for(const auto& s:snapshots){char name[80];snprintf(name,sizeof(name),"step-pool-%d-%d.bin",phase,s.frame);FILE* f=fopen(name,"wb");
    const unsigned header[4]={0x504f4f4c,2,unsigned(phase),unsigned(s.frame)};
    if(f){fwrite(header,4,4,f);fwrite(s.words.data(),4,s.words.size(),f);fclose(f);}
    printf("STEPPOOLSNAP phase=%d frame=%d words=%u file=%d\n",phase,s.frame,unsigned(s.words.size()),f!=nullptr);
  }
  fflush(stdout);snapshots.clear();
}
} }
