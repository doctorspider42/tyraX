#pragma once
#include <deque>
#include <vector>
#include "debug/motion_probe.hpp"
#include "renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_package.hpp"
#include "math/plane.hpp"
namespace StaticPlan {
using namespace Tyra;
struct Key {
  const void* bag;const void* vertices;const void* sts;const void* colors;const void* normals;
  unsigned count,size,maximum,bboxVersion,first;
  bool operator==(const Key& b)const{return bag==b.bag&&vertices==b.vertices&&sts==b.sts&&colors==b.colors&&normals==b.normals&&count==b.count&&size==b.size&&maximum==b.maximum&&bboxVersion==b.bboxVersion&&first==b.first;}
};
struct Entry{Key key{};std::vector<StaPipBagPackage> records;float planes[56]{};unsigned planeFlags=0;};
inline std::deque<Entry> entries;
inline bool samePackage(const StaPipBagPackage&a,const StaPipBagPackage&b){
  return a.bag==b.bag&&a.vertices==b.vertices&&a.sts==b.sts&&a.colors==b.colors&&a.normals==b.normals&&a.size==b.size&&a.isInFrustum==b.isInFrustum&&a.clipPlaneMask==b.clipPlaneMask&&a.guardBandOnly==b.guardBandOnly&&a.indexOf1By3BBox==b.indexOf1By3BBox&&a.endIndexOf1By3BBox==b.endIndexOf1By3BBox;
}
inline unsigned capture(float*out,const Plane*view,const Plane*clip,bool masks){
  unsigned k=0;for(unsigned set=0;set<2;++set){const Plane*p=set?clip:view;unsigned n=set?8:6;
    for(unsigned i=0;i<n;++i){out[k++]=p?p[i].normal.x:0;out[k++]=p?p[i].normal.y:0;out[k++]=p?p[i].normal.z:0;out[k++]=p?p[i].distance:0;}}
  return unsigned(view!=nullptr)|(unsigned(clip!=nullptr)<<1)|(unsigned(masks)<<2);
}
template<class Build> StaPipBagPackage* run(unsigned short*out,const Key&key,const Plane*view,const Plane*clip,bool masks,Build build){
  if(!MotionProbe::target)return build(nullptr);
  const unsigned slot=MotionProbe::slots++;
  if(slot>=512){MotionProbe::valid=false;return build(nullptr);}
  while(entries.size()<=slot)entries.emplace_back();
  Entry&e=entries[slot];
  const bool match=e.key==key&&!e.records.empty();
  const unsigned mode=MotionProbe::variant();
  float planes[56];unsigned flags=0;
  if(mode==2||!MotionProbe::sample)flags=capture(planes,view,clip,masks);
  const bool frozen=match&&mode==2&&e.planeFlags==flags&&memcmp(e.planes,planes,sizeof(planes))==0;
  StaPipBagPackage* result=nullptr;
  if(mode&&match){
    *out=static_cast<unsigned short>(e.records.size());
    if(MotionProbe::enabled)++MotionProbe::descHits;
    if(frozen){result=e.records.data();if(MotionProbe::enabled)++MotionProbe::viewHits;MotionProbe::add(MotionProbe::Packages,*out);}
    else result=build(e.records.data());
    if(STATIC_PLAN_VERIFY || MotionProbe::offset==250){
    std::vector<StaPipBagPackage> candidate(result,result+*out);
    unsigned savedCounters[MotionProbe::Count];memcpy(savedCounters,MotionProbe::counters,sizeof(savedCounters));
    unsigned expectedCount=*out;auto*original=build(nullptr);
    memcpy(MotionProbe::counters,savedCounters,sizeof(savedCounters));
    bool equal=expectedCount==*out;
    for(unsigned i=0;equal&&i<*out;++i)equal=samePackage(candidate[i],original[i]);
    ++MotionProbe::cumulativeChecks;if(!equal){++MotionProbe::cumulativeErrors;MotionProbe::valid=false;}
    if(MotionProbe::enabled){++MotionProbe::checks;if(!equal)++MotionProbe::errors;}
    result=e.records.data();
    }
  }else{
    result=build(nullptr);
    if(MotionProbe::enabled&&mode)++MotionProbe::misses;
  }
  if(!MotionProbe::sample||mode){
    if(result!=e.records.data()){
      if(MotionProbe::resident+*out*sizeof(StaPipBagPackage)>1024*1024){MotionProbe::valid=false;return result;}
      MotionProbe::resident-=e.records.size()*sizeof(StaPipBagPackage);
      e.records.assign(result,result+*out);MotionProbe::resident+=e.records.size()*sizeof(StaPipBagPackage);
    }
    e.key=key;
    if(mode!=1){e.planeFlags=flags;memcpy(e.planes,planes,sizeof(planes));}
  }
  if(MotionProbe::enabled)for(unsigned i=0;i<*out;++i){
    if(result[i].isInFrustum==IN_FRUSTUM)++MotionProbe::packagesIn;
    else if(result[i].isInFrustum==OUTSIDE_FRUSTUM)++MotionProbe::packagesOut;
    else ++MotionProbe::packagesPartial;
  }
  return result;
}
}
