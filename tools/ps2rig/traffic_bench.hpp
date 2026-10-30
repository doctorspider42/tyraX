// Private normal-clock traffic accounting; writes only between phases.
#pragma once
#include <cmath>
#include <cstdio>
#include <vector>
#include <new>
#include <kernel.h>
namespace TrafficBench {
using U = unsigned;
inline U ticks() { U t; asm volatile("mfc0 %0,$9" : "=r"(t)::"memory"); return t; }
constexpr float Hz = 294912000.f;
enum Counter { Vehicle, Core, Physics, Scene, Wheels, Glass, End,
               Steps, PhysicsSteps, FullModels, Kinematic, PaintGrants,
               WallMax, FloorMax, MeshMax, PropMax, Errors, Count };
inline const char* names[Count] = {
  "vehicle_inclusive_ticks","traffic_core_ticks","physics_ticks",
  "scene_inclusive_ticks","wheels_inclusive_ticks","glass_inclusive_ticks",
  "end_frame_ticks","model_substeps","full_physics_steps","full_model_calls","kinematic_calls",
  "non_driver_paint_grants","world_wall_highwater","world_floor_highwater",
  "world_mesh_highwater","prop_candidate_highwater","errors"};
struct Row {
  U phase=0,frame=0,period=0,body=0,c[Count]{},lap[7]{};
  U hq=0,civ=0,hqNear=0,civNear=0,hqFront=0,civFront=0,hqLod0=0,civLod0=0,hqSubmitted=0,civSubmitted=0;
  float elapsed=0,dt=0,z=0,speed=0,maxDistance=0;
};
inline Row current;
inline std::vector<Row> rows;
inline int phase=0,frame=0,wantedHQ=0,wantedCiv=0;
inline bool ready=false,finished=false,previous=false;
inline bool detailed=false;
inline int allocationStage=-1,allocationOwner=-1;
inline U phaseStart=0,frameStart=0;
inline bool paintSeen[32]{};
inline bool submittedSeen[32]{};
inline std::vector<int> objectSlot;
inline const int populations[10]={1,3,5,10,13,13,10,5,3,1};
inline float elapsed() { return float(U(ticks()-phaseStart))/Hz; }
inline bool warm() { return !ready || current.elapsed<2.f; }
inline bool driving() { return ready && current.elapsed>=2.f && current.elapsed<5.f; }
inline void resetRow() { current={};for(auto& b:paintSeen)b=false;for(auto& b:submittedSeen)b=false; }
inline void submitted(int object){
  if(ready && object>=0 && size_t(object)<objectSlot.size()){
    const int vi=objectSlot[size_t(object)];if(vi>=0 && vi<32)submittedSeen[vi]=true;
  }
}
inline void high(Counter c,U v){if(v>current.c[c])current.c[c]=v;}
inline void paint(int vi,bool driver){
  if(ready && !driver && vi>=0 && vi<32 && !paintSeen[vi]){
    paintSeen[vi]=true;++current.c[PaintGrants];
  }
}
struct Scope {
  Counter id;U start;bool active;int prior;
  explicit Scope(Counter c):id(c),start(ticks()),active(ready&&!finished),prior(allocationStage){allocationStage=int(c);}
  ~Scope(){if(active)current.c[id]+=U(ticks()-start);allocationStage=prior;}
};
struct ObjectScope {
  int prior;
  explicit ObjectScope(int object):prior(allocationOwner){allocationOwner=object;}
  ~ObjectScope(){allocationOwner=prior;}
};
inline void lap(int i,U& prior){if(detailed){const U now=ticks();current.lap[i]+=U(now-prior);prior=now;}}
inline void flush(){
  char name[80];std::snprintf(name,sizeof(name),"traffic-bench-%02d.csv",phase);
  FILE* f=std::fopen(name,"w");
  if(f){
    std::fprintf(f,"phase,frame,elapsed,dt,period_ticks,body_ticks,");
    for(int k=0;k<Count;++k)std::fprintf(f,"%s,",names[k]);
    for(int k=0;k<7;++k)std::fprintf(f,"vehicle_lap%d_ticks,",k);
    std::fprintf(f,"hq,civ,hq_near,civ_near,hq_front,civ_front,hq_lod0,civ_lod0,hq_submitted,civ_submitted,car_z,car_speed,max_camera_distance\n");
    for(const auto& r:rows){
      std::fprintf(f,"%u,%u,%.9g,%.9g,%u,%u,",r.phase,r.frame,r.elapsed,r.dt,r.period,r.body);
      for(auto c:r.c)std::fprintf(f,"%u,",c);
      for(auto c:r.lap)std::fprintf(f,"%u,",c);
      std::fprintf(f,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g\n",r.hq,r.civ,r.hqNear,r.civNear,r.hqFront,r.civFront,r.hqLod0,r.civLod0,r.hqSubmitted,r.civSubmitted,r.z,r.speed,r.maxDistance);
    }
    std::fclose(f);
  }
  std::printf("TRAFFICBENCH phase=%d population=%d rows=%u file=%d normal_clock=1\n",phase,populations[phase],unsigned(rows.size()),f!=nullptr);
  std::fflush(stdout);rows.clear();
}
} // namespace TrafficBench
