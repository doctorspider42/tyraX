// Private shared-clock prototype. No host I/O while a physics packet is live.
#pragma once
#include <cmath>
#include <cstdio>
#include <vector>
namespace Vehicle_playground { namespace UnifiedTimeline {
inline float multiplier=1,unitScale=1;
inline bool enabled=false;
struct Sleep {float seconds=0;int exported=0;};
inline std::vector<Sleep> sleep;
struct Row {
  unsigned frame,steps,physics,vehicles,bodies,cars,errors,ticks;
  float frameDt,stepDt;
  unsigned enabled=0,hashA=2166136261u,hashB=0x9e3779b9u;
};
inline std::vector<Row> rows;
inline unsigned generation=~0u;
inline void word(Row& r,unsigned v){r.hashA=(r.hashA^v)*16777619u;r.hashB^=v+0x9e3779b9u+(r.hashB<<6)+(r.hashB>>2);}
inline void real(Row& r,float v){unsigned bits;__builtin_memcpy(&bits,&v,4);word(r,bits);}
inline float threshold(float value){return unitScale==1?value:value*unitScale;}
inline float threshold2(float value){return unitScale==1?value:value*unitScale*unitScale;}
inline void configure(){
  FILE* f=fopen("unified-step.cfg","r");float value=1;
  if(f){if(fscanf(f,"%f",&value)!=1)value=0;fclose(f);}
  if(value!=1 && value!=2){printf("CB ERROR invalid unified step config\n");CB::finished=true;return;}
  multiplier=value;
  printf("UNIFIEDCLOCK startup multiplier=%g\n",multiplier);fflush(stdout);
}
template<class Objects>void begin(const Objects& objects,unsigned scene,float dt){
  if(generation!=scene || sleep.size()!=objects.size()){
    sleep.assign(objects.size(),{});generation=scene;
  }
  for(size_t i=0;i<objects.size();++i){
    const auto& o=objects[i];auto& s=sleep[i];
    if(o.restFrames!=s.exported)s.seconds=float(o.restFrames)*dt;
    if(!o.restFrames)s.seconds=0;
  }
}
inline int beforeSleep(int index,int rest,float h){
  if(rest==PHYS_ASLEEP)return rest;
  auto& s=sleep[size_t(index)];if(!rest)s.seconds=0;
  return int(s.seconds/h);
}
inline void afterSleep(int index,int before,int after,float h){
  auto& s=sleep[size_t(index)];
  if(after==0)s.seconds=0;
  else if(after!=PHYS_ASLEEP && after>before)s.seconds+=h;
}
inline int exportSleep(int index,int rest,float dt){
  auto& s=sleep[size_t(index)];
  if(rest==0)s.seconds=0;
  if(rest!=PHYS_ASLEEP){rest=int(s.seconds/dt);if(s.seconds>0 && rest==0)rest=1;}
  s.exported=rest;return rest;
}
inline void flush(int phase){
  char name[80];snprintf(name,sizeof(name),"unified-timeline-%d.csv",phase);
  FILE* f=fopen(name,"w");unsigned errors=0,physics=0,vehicles=0;
  if(f)fprintf(f,"frame,steps,physics,vehicles,bodies,cars,errors,ticks,frame_dt,step_dt,enabled,hash_a,hash_b\n");
  for(const auto& r:rows){errors+=r.errors;physics+=r.physics;vehicles+=r.vehicles;
    if(f)fprintf(f,"%u,%u,%u,%u,%u,%u,%u,%u,%.9g,%.9g,%u,%u,%u\n",r.frame,r.steps,r.physics,r.vehicles,r.bodies,r.cars,r.errors,r.ticks,r.frameDt,r.stepDt,r.enabled,r.hashA,r.hashB);
  }
  if(f)fclose(f);
  printf("UNIFIEDCLOCK phase=%d frames=%u physics=%u vehicles=%u errors=%u file=%d\n",phase,unsigned(rows.size()),physics,vehicles,errors,f!=nullptr);fflush(stdout);rows.clear();
}
} }
