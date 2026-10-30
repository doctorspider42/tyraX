// Private EE physics profiler; never included by production game sources.
// Event selectors: SCE EE Core User's Manual v6, sections 7.1 and 7.2.
#pragma once
#include <cstdio>
namespace PhysicsPmu {
using U = unsigned;
inline U control() { U v; asm volatile("mfps %0,0":"=r"(v)::"memory"); return v; }
inline U pc0() { U v; asm volatile("mfpc %0,0":"=r"(v)::"memory"); return v; }
inline U pc1() { U v; asm volatile("mfpc %0,1":"=r"(v)::"memory"); return v; }
inline U ticks() { U v; asm volatile("mfc0 %0,$9":"=r"(v)::"memory"); return v; }
inline void writeControl(U v) { asm volatile("mtps %0,0\n\tsync.p"::"r"(v):"memory"); }
inline void writeCounters(U a,U b) { asm volatile("mtpc %0,0\n\tmtpc %1,1\n\tsync.p"::"r"(a),"r"(b):"memory"); }
inline U errors=0,mode=0,savedControl=0,saved0=0,saved1=0;
inline bool active=false;
struct Mark { U t,a,b; };
inline Mark mark() { return {ticks(),pc0(),pc1()}; }
struct Sample { U calls=0,t=0,a=0,b=0; };
inline Sample samples[8];
inline void start(U event0,U event1) {
  writeControl(0);writeCounters(0,0);
  writeControl((1u<<31)|(event0<<5)|(15u<<1)|(event1<<15)|(15u<<11));
}
inline bool enter(U m) {
  // Refuse to steal a live profiler. Preserve disabled register state exactly.
  savedControl=control();
  if(savedControl&(1u<<31)){++errors;return false;}
  saved0=pc0();saved1=pc1();mode=m;
  const U e0[3]={6,2,12},e1[3]={6,2,14};
  start(e0[mode],e1[mode]);active=true;return true;
}
inline void leave() {
  if(!active)return;
  if((pc0()|pc1())&0x80000000u)++errors;
  writeControl(0);writeCounters(saved0,saved1);writeControl(savedControl);active=false;
}
struct Owner {
  bool owns;
  explicit Owner(U m):owns(enter(m)) {}
  ~Owner(){if(owns)leave();}
};
struct Scope {
  int stage;Mark first{};
  explicit Scope(int s):stage(active?s:-1) { if(stage>=0)first=mark(); }
  ~Scope() {
    if(stage<0)return;
    const auto last=mark();auto& v=samples[stage];++v.calls;
    v.t+=last.t-first.t;v.a+=last.a-first.a;v.b+=last.b-first.b;
  }
};
struct Row { U frame,mode,contacts,awake;Sample stages[8]; };
inline Row rows[1024];inline int used=0;
inline void frame(U f,U contacts,U awake,bool measured) {
  if(measured){
    if(used>=1024)++errors;
    else { auto& r=rows[used++];r.frame=f;r.mode=mode;r.contacts=contacts;r.awake=awake;
      for(int s=0;s<8;++s)r.stages[s]=samples[s]; }
  }
  for(auto& s:samples)s={};
}
inline void flush(int phase) {
  char name[64];snprintf(name,sizeof(name),"physics-pmu-%02d.csv",phase);
  FILE* f=fopen(name,"wb");if(!f)++errors;
  if(f){fprintf(f,"phase,frame,mode,stage,contacts,awake,calls,ticks,pcr0,pcr1\n");
    for(int i=0;i<used;++i){const auto& r=rows[i];for(int s=0;s<8;++s){const auto& v=r.stages[s];
      if(v.calls)fprintf(f,"%d,%u,%u,%d,%u,%u,%u,%u,%u,%u\n",phase,r.frame,r.mode,s,r.contacts,r.awake,v.calls,v.t,v.a,v.b);}}
    fclose(f);}
  printf("PHYSICSPMU phase=%d frames=%d errors=%u file=%d\n",phase,used,errors,f!=nullptr);fflush(stdout);used=0;
}
// Volatile reads force one real load at each cache-line stride. Warm 4KiB fits
// D$, while four 128KiB scans cannot. Calibration is outside measured frames.
alignas(64) inline volatile unsigned char calibration[128*1024];
__attribute__((noinline)) inline U touch(U bytes,U repeats) {
  U sum=0;for(U r=0;r<repeats;++r)for(U i=0;i<bytes;i+=64)sum+=calibration[i];return sum;
}
inline bool calibrate() {
  if(control()&(1u<<31)){++errors;return false;}
  const U ctl=control(),a=pc0(),b=pc1();
  start(1,1);const auto cycleStart=mark();const U sink=touch(4096,64);const auto cycleEnd=mark();
  const U dt=cycleEnd.t-cycleStart.t,da=cycleEnd.a-cycleStart.a,db=cycleEnd.b-cycleStart.b;
  touch(4096,1);start(6,6);const U small0=pc1();touch(4096,64);const U small=pc1()-small0;
  touch(128*1024,1);start(6,6);const U big0=pc1();touch(128*1024,4);const U big=pc1()-big0;
  const bool ok=dt>0 && da>dt*9/10 && da<dt*11/10 && db>dt*9/10 && db<dt*11/10 && small<128 && big>4096 && big<16384;
  if(!ok)++errors;
  writeControl(0);writeCounters(a,b);writeControl(ctl);
  printf("PHYSICSPMUCAL ticks=%u cycle0=%u cycle1=%u fit4k=%u scan128k=%u sink=%u passed=%d\n",dt,da,db,small,big,sink,ok);fflush(stdout);return ok;
}
} // namespace PhysicsPmu
