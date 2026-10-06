#pragma once
// PRIVATE scope: ordinary CPU elapsed time, no drain/wait, cold export only.
#include "night_sampler.hpp"
#include <string.h>
namespace VehicleProbe {
struct Sample {unsigned frame,dtBits,planned,executed,enabled,elapsed;};
inline Sample samples[128]{};
inline unsigned captured[3]{},start=0,index=0;
inline bool inside=false;
inline bool timer(unsigned phase,unsigned i){return phase==0 || (phase==1?(i>=40&&i<88):(i<40||i>=88));}
inline void begin(float dt){
 const unsigned frame=NightSampler::frame,phase=frame/1800,offset=frame%1800;
 inside=phase<3&&offset>=900&&offset<1028;if(!inside)return;
 index=offset-900;
 if(samples[index].frame==frame&&frame!=0)NightSampler::valid=false;
 unsigned bits;memcpy(&bits,&dt,4);
 samples[index]={frame,bits,0,0,unsigned(timer(phase,index)),0};
 if(samples[index].enabled)start=NightSampler::ticks();
}
inline void planned(unsigned n){if(inside)samples[index].planned=n;}
inline void executed(){if(inside)++samples[index].executed;}
inline void end(){
 if(!inside)return;
 auto& s=samples[index];
 if(s.enabled){s.elapsed=NightSampler::ticks()-start;if(!NightSampler::deltaValid(s.elapsed))NightSampler::valid=false;}
 if(s.planned<1||s.planned>4||s.executed!=s.planned||s.frame!=NightSampler::frame)NightSampler::valid=false;
 ++captured[s.frame/1800];inside=false;
}
}
