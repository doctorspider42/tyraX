// Private single-pass vehicle/prop contact integration experiment.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
namespace Vehicle_playground { namespace VehicleContacts {
using namespace runtime_detail;
struct Car {
  int object=-1,vehicle=-1;
  RbV oldOrigin{},oldCentre{},centre{},velocity{},angular{},originVelocity{};
  float oldR[9]{},R[9]{},half[3]{},localCentre[3]{},mass=1,invYaw=0;
};
inline bool enabled=false;
inline bool isolation=false;
inline unsigned clockMultiplier=1;
inline float dt=.02f;
inline std::vector<Car> cars;
inline std::vector<int> of;
inline float component(RbV v,int k){return k==0?v.x:k==1?v.y:v.z;}
inline int index(int object){return enabled && object>=0 && size_t(object)<of.size()?of[size_t(object)]:-1;}
inline bool isCar(int object){return index(object)>=0;}
struct Row {
  unsigned frame=0,enabled=0,fullSteps=0,cars=0,pairs=0,contacts=0,impulseUpdates=0,woken=0,errors=0;
  unsigned referencePairs=0,referenceHits=0,missing=0;
  unsigned shapeChecks=0;
  unsigned combinedTicks=0,finishTicks=0,vehicleSteps=0,hashA=2166136261u,hashB=0x9e3779b9u;
  float maxDepth=0,maxMomentumRatio=0,carCorrection=0,maxShapeRatio=0;
};
inline Row current;
inline std::vector<Row> rows;
inline void word(unsigned value){current.hashA=(current.hashA^value)*16777619u;current.hashB^=value+0x9e3779b9u+(current.hashB<<6)+(current.hashB>>2);}
inline void real(float value){unsigned bits;__builtin_memcpy(&bits,&value,4);word(bits);}
inline void configure(){
  FILE* f=fopen("vehicle-contacts.cfg","r");unsigned isolate=0,multiplier=1;
  if(f){if(fscanf(f,"%u %u",&isolate,&multiplier)!=2)isolate=9;fclose(f);}
  if(isolate>1 || (multiplier!=1 && multiplier!=2)){printf("CB ERROR invalid vehicle contact configuration\n");CB::finished=true;return;}
  isolation=isolate!=0;clockMultiplier=multiplier;
  printf("VEHCONTACTMODE isolate=%u multiplier=%u\n",isolate,multiplier);fflush(stdout);
}
inline void begin(){current={};current.frame=unsigned(CB::frame);current.enabled=enabled;}
inline void impulse(RbV av,RbV bv,RbV an,RbV bn,float ma,float mb,RbV p){
  // An immovable sleeping/static partner exchanges momentum with the world.
  if(!Tyra::vu0SatVerify || ma<=0 || mb<=0)return;
  ++current.impulseUpdates;
  const float before[6]={av.x,av.y,av.z,bv.x,bv.y,bv.z};
  const float after[6]={an.x,an.y,an.z,bn.x,bn.y,bn.z};const float push[3]={p.x,p.y,p.z};
  for(int k=0;k<3;++k){
    const float residual=(after[k]-before[k])/ma+(after[k+3]-before[k+3])/mb;
    const float budget=32.f*1.1920929e-7f*(fabsf(before[k]/ma)+fabsf(before[k+3]/mb)+fabsf(push[k])+1e-6f);
    const float ratio=fabsf(residual)/budget;
    current.maxMomentumRatio=std::max(current.maxMomentumRatio,ratio);
    if(!std::isfinite(ratio) || ratio>1.f)++current.errors;
  }
}
inline void flush(int phase){
  char name[80];snprintf(name,sizeof(name),"vehicle-contacts-%d.csv",phase);FILE* f=fopen(name,"w");
  if(f)fprintf(f,"frame,enabled,full_steps,cars,pairs,contacts,impulse_updates,woken,errors,combined_ticks,finish_ticks,max_depth,max_momentum_ratio,car_correction,reference_pairs,reference_hits,missing,shape_checks,max_shape_ratio,vehicle_steps,hash_a,hash_b\n");
  unsigned errors=0,contacts=0,steps=0;
  for(const auto& r:rows){errors+=r.errors;contacts+=r.contacts;steps+=r.fullSteps;
    if(f)fprintf(f,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g,%u,%u,%u,%u,%.9g,%u,%u,%u\n",r.frame,r.enabled,r.fullSteps,r.cars,r.pairs,r.contacts,r.impulseUpdates,r.woken,r.errors,r.combinedTicks,r.finishTicks,r.maxDepth,r.maxMomentumRatio,r.carCorrection,r.referencePairs,r.referenceHits,r.missing,r.shapeChecks,r.maxShapeRatio,r.vehicleSteps,r.hashA,r.hashB);
  }
  if(f){fclose(f);}
  printf("VEHCONTACT phase=%d frames=%u full_steps=%u contacts=%u errors=%u file=%d\n",phase,unsigned(rows.size()),steps,contacts,errors,f!=nullptr);fflush(stdout);rows.clear();
}
} }
