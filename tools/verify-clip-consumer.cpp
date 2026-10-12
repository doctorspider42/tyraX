#include <cassert>
#include <cstdio>
#include "debug/clip_consumer.hpp"
struct V { unsigned x,y,z,w; };
struct Color { bool many; };
struct Bag { void* texture; Color* color; void* lighting; };
struct Buffer { unsigned size,clipPlaneMask;bool stripped;Bag* bag;V *vertices,*sts,*colors,*normals; };
int main(){
 V data[4][12]{};Color color{};Bag bag{nullptr,&color,nullptr};
 Buffer b{12,1,false,&bag,data[0],data[1],data[2],data[3]};
 unsigned checks=0;
 for(unsigned mask=0;mask<8;++mask){
  bag.texture=(mask&1)?&bag:nullptr;color.many=mask&2;bag.lighting=(mask&4)?&bag:nullptr;
  ClipConsumer::observe=true;ClipConsumer::reset();ClipConsumer::input(&b);
  const unsigned baseline=ClipConsumer::hash;
  assert(ClipConsumer::calls==1 && ClipConsumer::verts==12);++checks;
  for(unsigned stream=0;stream<4;++stream){
   const bool active=stream==0 || (mask&(1u<<(stream-1)));
   data[stream][5].z=123;
   ClipConsumer::reset();ClipConsumer::input(&b);
   assert((ClipConsumer::hash!=baseline)==active);++checks;
   data[stream][5].z=0;
  }
  ClipConsumer::reset();ClipConsumer::input(&b);ClipConsumer::input(&b);
  assert(ClipConsumer::calls==2 && ClipConsumer::verts==24 && ClipConsumer::hash!=baseline);++checks;
  ClipConsumer::observe=false;ClipConsumer::reset();ClipConsumer::input(&b);
  assert(ClipConsumer::hash==2166136261U && ClipConsumer::calls==0);++checks;
 }
 printf("CLIPCONSUMER host checks=%u passed=1\n",checks);
}
