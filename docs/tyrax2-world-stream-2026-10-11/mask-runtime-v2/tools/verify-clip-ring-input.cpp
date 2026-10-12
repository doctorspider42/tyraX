#include "tyrax2-clip-ring-input.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
struct V { unsigned bits[4]; };
struct Color { bool many; };
struct Bag { bool texture,lighting;Color* color; };
struct Buffer {
  Bag* bag;V *vertices,*sts,*colors,*normals;
  unsigned size,clipPlaneMask,nativeClipParity;
  bool nativeClipStrip;
};
struct Result { unsigned hash,triangles,vertices; };
static Result finish(){return {ClipConsumer::hash,ClipConsumer::calls,ClipConsumer::verts};}
int main(){
  using namespace ClipConsumer;
  unsigned cases=0;bool parityRejected=false,maskRejected=false,attributeRejected=false;
  for(unsigned streams=0;streams<8;++streams)
    for(unsigned first=0;first<8;++first)
      for(unsigned count=3;count<=72;++count){
        const unsigned triangles=count-2;
        std::array<std::vector<V>,4> source,expanded;
        for(unsigned channel=0;channel<4;++channel){
          source[channel].resize(first+count);
          for(unsigned i=0;i<first+count;++i)
            for(unsigned lane=0;lane<4;++lane)
              source[channel][i].bits[lane]=1009*i+71*channel+lane;
          for(unsigned t=0;t<triangles;++t)
            for(unsigned corner=0;corner<3;++corner){
              const unsigned local=((t+first)&1U)&&corner<2?1-corner:corner;
              expanded[channel].push_back(source[channel][first+t+local]);
            }
        }
        Color c{bool(streams&2)};Bag bag{bool(streams&1),bool(streams&4),&c};
        Buffer legacy{&bag,nullptr,nullptr,nullptr,nullptr,0,63,0,false};
        observe=true;reset();
        for(unsigned t=0;t<triangles;t+=4){
          legacy.size=3*std::min(4U,triangles-t);
          legacy.vertices=expanded[0].data()+t*3;legacy.sts=expanded[1].data()+t*3;
          legacy.colors=expanded[2].data()+t*3;legacy.normals=expanded[3].data()+t*3;
          input(&legacy);
        }
        auto expected=finish();reset();
        Buffer native{&bag,nullptr,nullptr,nullptr,nullptr,0,63,0,true};
        for(unsigned t=0;t<triangles;t+=7){
          native.size=std::min(7U,triangles-t)+2;native.nativeClipParity=(first+t)&1;
          native.vertices=source[0].data()+first+t;native.sts=source[1].data()+first+t;
          native.colors=source[2].data()+first+t;native.normals=source[3].data()+first+t;
          input(&native);
        }
        const auto actual=finish();
        assert(actual.hash==expected.hash && actual.triangles==expected.triangles &&
               actual.vertices==expected.vertices && nativeCalls>0 && nativeTriangles==triangles);
        native.size=count;native.nativeClipParity=first&1;
        native.vertices=source[0].data()+first;native.sts=source[1].data()+first;
        native.colors=source[2].data()+first;native.normals=source[3].data()+first;
        reset();native.nativeClipParity^=1;input(&native);parityRejected|=hash!=expected.hash;
        reset();native.nativeClipParity=first&1;native.clipPlaneMask=31;input(&native);
        maskRejected|=hash!=expected.hash;native.clipPlaneMask=63;
        if(streams&2){source[2][first].bits[0]^=1;reset();input(&native);
          attributeRejected|=hash!=expected.hash;source[2][first].bits[0]^=1;}
        reset();observe=true;noteFallback(triangles);
        assert(fallbackCalls==1 && fallbackTriangles==triangles);
        reset();assert(fallbackCalls==0 && fallbackTriangles==0);
        observe=false;input(&native);noteFallback(triangles);
        assert(calls==0 && verts==0 && nativeCalls==0 && fallbackCalls==0 && fallbackTriangles==0);
        ++cases;
      }
  assert(parityRejected && maskRejected && attributeRejected);
  std::cout<<"PASS canonical ordered triangles cases="<<cases<<" controls=4\n";
}
