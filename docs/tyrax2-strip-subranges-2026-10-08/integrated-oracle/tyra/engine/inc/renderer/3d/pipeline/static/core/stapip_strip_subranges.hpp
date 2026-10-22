#pragma once
// Modified by TyraX: bounded CPU-only strip bounds; never caches view decisions.
#include <cmath>
#include <cstdint>
#ifndef TYRA_STAPIP_SUBRANGES_VERIFY
#define TYRA_STAPIP_SUBRANGES_VERIFY 1
#endif
namespace Tyra { namespace StaPipStripSubranges {
enum Route : unsigned char { Clip, Cull, Outside };
struct Bounds { float lo[3], hi[3]; };
struct Entry {
  const void* vertices=nullptr;
  unsigned count=0, step=0, version=0, content=0, epoch=0;
  Bounds boxes[32]{};
};

inline unsigned checks=0, failures=0;

template<class V> Bounds bounds(const V* vertices, unsigned first, unsigned count) {
  Bounds box{{vertices[first].x,vertices[first].y,vertices[first].z},
             {vertices[first].x,vertices[first].y,vertices[first].z}};
  for(unsigned i=first;i<first+count;++i) {
    const float xyz[]={vertices[i].x,vertices[i].y,vertices[i].z};
    for(unsigned j=0;j<3;++j) {
      if(!std::isfinite(xyz[j])) { box.lo[0]=NAN;return box; }
      if(xyz[j]<box.lo[j])box.lo[j]=xyz[j];
      if(xyz[j]>box.hi[j])box.hi[j]=xyz[j];
    }
  }
  return box;
}
class Cache {
  Entry entries[256]{};
 public:
 template<class V> const Entry* acquire(const V* vertices,unsigned count,unsigned step,
    unsigned version,unsigned content,unsigned epoch,bool& built) {
  built=false;
  if(!vertices||count<3||!step||(count-2+step-1)/step>32)return nullptr;
  const unsigned slot=((reinterpret_cast<uintptr_t>(vertices)>>4)^count^(step<<5))&255;
  auto& e=entries[slot];
  if(e.vertices!=vertices||e.count!=count||e.step!=step||e.version!=version||
      e.content!=content||e.epoch!=epoch) {
    e.vertices=vertices;e.count=count;e.step=step;e.version=version;e.content=content;e.epoch=epoch;
    for(unsigned t=0;t<count-2;t+=step) {
      const unsigned n=count-2-t<step?count-2-t:step;
      e.boxes[t/step]=bounds(vertices,t,n+2);
    }
    built=true;
  }
  return &e;
}
};
template<class P> float distance(const P& p,const float* xyz) {
  return p.distance+p.normal.x*xyz[0]+p.normal.y*xyz[1]+p.normal.z*xyz[2];
}
template<class P> float margin(const P& p,const Bounds& b) {
  return 0.00001F+0.00001F*(std::fabs(p.distance)+
    std::fabs(p.normal.x)*std::fmax(std::fabs(b.lo[0]),std::fabs(b.hi[0]))+
    std::fabs(p.normal.y)*std::fmax(std::fabs(b.lo[1]),std::fabs(b.hi[1]))+
    std::fabs(p.normal.z)*std::fmax(std::fabs(b.lo[2]),std::fabs(b.hi[2])));
}
template<class P> Route classify(const Bounds& b,const P* screen,const P* guard) {
  if(!std::isfinite(b.lo[0]))return Clip;
  float v[3];
  for(unsigned i=0;i<6;++i) {
    const float n[]={screen[i].normal.x,screen[i].normal.y,screen[i].normal.z};
    for(unsigned j=0;j<3;++j)v[j]=n[j]>=0?b.hi[j]:b.lo[j];
    if(distance(screen[i],v)<-margin(screen[i],b))return Outside;
  }
  for(unsigned i=0;i<8;++i) {
    const float n[]={guard[i].normal.x,guard[i].normal.y,guard[i].normal.z};
    for(unsigned j=0;j<3;++j)v[j]=n[j]>=0?b.lo[j]:b.hi[j];
    if(!(distance(guard[i],v)>margin(guard[i],b)))return Clip;
  }
  return Cull;
}
// Independent vertex witness, including every overlapping strip vertex.
template<class V,class P> bool verify(const V* v,unsigned first,unsigned n,
    const Bounds& cached,Route route,const P* screen,const P* guard) {
  const auto fresh=bounds(v,first,n+2);
  bool ok=true;
  for(unsigned j=0;j<3;++j)ok&=fresh.lo[j]==cached.lo[j]&&fresh.hi[j]==cached.hi[j];
  if(route==Outside) {
    bool separated=false;
    for(unsigned p=0;p<6;++p) {
      bool all=true;
      for(unsigned i=first;i<first+n+2;++i) {
        const double d=double(screen[p].distance)+double(screen[p].normal.x)*v[i].x+
          double(screen[p].normal.y)*v[i].y+double(screen[p].normal.z)*v[i].z;
        all&=d<0;
      }
      separated|=all;
    }
    ok&=separated;
  } else if(route==Cull) {
    for(unsigned p=0;p<8;++p)for(unsigned i=first;i<first+n+2;++i) {
      const double d=double(guard[p].distance)+double(guard[p].normal.x)*v[i].x+
        double(guard[p].normal.y)*v[i].y+double(guard[p].normal.z)*v[i].z;
      ok&=d>0;
    }
  }
  ++checks;if(!ok)++failures;
  return ok;
}
}


} // namespace Tyra
