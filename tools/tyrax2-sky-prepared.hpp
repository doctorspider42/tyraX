// Private A04 geometry-owned metadata; never retains view decisions or DMA data.
#pragma once
#include <vector>
#include <math.h>
namespace SkyPrepared {
template<class V> class Store {
 public:
  struct Box { V corners[2]; unsigned offset=0, count=0; };
  const V* source=nullptr;
  unsigned count=0, version=0, parent=0, step=0, perParent=0;
  std::vector<Box> boxes;

  void build(const V* vertices,unsigned size,unsigned stamp,unsigned maximum) {
    source=nullptr; boxes.clear(); count=size; version=stamp; parent=maximum;
    step=((maximum/6)/3)*3;
    if(!vertices || !size || size%3 || !step || maximum%3)return;
    perParent=(maximum+step-1)/step;
    const unsigned stride=size<maximum*2?size:maximum;
    boxes.reserve((size+maximum-1)/maximum*perParent);
    for(unsigned base=0;base<size;base+=stride){
      const unsigned end=base+stride<size?base+stride:size;
      for(unsigned start=base;start<end;start+=step){
        Box box;box.offset=start;box.count=end-start<step?end-start:step;
        box.corners[0]=vertices[start];box.corners[1]=vertices[start];
        for(unsigned i=start;i<start+box.count;++i){
          const V& v=vertices[i];
          if(!isfinite(v.x)||!isfinite(v.y)||!isfinite(v.z)){boxes.clear();return;}
          if(v.x<box.corners[0].x)box.corners[0].x=v.x;
          if(v.y<box.corners[0].y)box.corners[0].y=v.y;
          if(v.z<box.corners[0].z)box.corners[0].z=v.z;
          if(v.x>box.corners[1].x)box.corners[1].x=v.x;
          if(v.y>box.corners[1].y)box.corners[1].y=v.y;
          if(v.z>box.corners[1].z)box.corners[1].z=v.z;
        }
        boxes.push_back(box);
      }
    }
    source=vertices;
  }

  const Box* get(const V* vertices,unsigned size,unsigned stamp,unsigned maximum,
                 unsigned offset,unsigned n) const {
    if(!source || source!=vertices || count!=size || version!=stamp ||
       parent!=maximum || !n || offset>=count || n>count-offset)return nullptr;
    const unsigned i=count<parent*2?offset/step:
      (offset/parent)*perParent+(offset%parent)/step;
    if(i>=boxes.size())return nullptr;
    const Box& b=boxes[i];
    return b.offset==offset && b.count==n?&b:nullptr;
  }
};
}
