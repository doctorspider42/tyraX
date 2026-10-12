#include "sky_prepared.hpp"
#include "sky_bounds.hpp"
#include <cassert>
#include <random>
#include <limits>
#include <iostream>
struct V {
  float x,y,z,w;
  // Match Tyra's nonstandard assignment return type.
  void operator=(const V& v){x=v.x;y=v.y;z=v.z;w=v.w;}
};
struct P { V normal; float distance; };
int main(){
  std::mt19937 rng(409);
  std::uniform_real_distribution<float> f(-1000,1000);
  SkyPrepared::Store<V> store;
  unsigned checked=0;
  for(unsigned maximum: {18u,36u,72u,75u,96u,150u}){
    for(unsigned size=3;size<=1200;size+=3){
      std::vector<V> vertices(size);
      for(auto& v:vertices)v={f(rng),f(rng),f(rng),1};
      store.build(vertices.data(),size,7,maximum);
      assert(store.source==vertices.data());
      unsigned step=((maximum/6)/3)*3;
      unsigned stride=size<maximum*2?size:maximum;
      for(unsigned base=0;base<size;base+=stride){
        const unsigned end=std::min(size,base+stride);
        for(unsigned start=base;start<end;start+=step){
          unsigned n=std::min(step,end-start);
          const auto* box=store.get(vertices.data(),size,7,maximum,start,n);
          assert(box);
          P planes[8];
          for(auto& p:planes)p={{f(rng),f(rng),f(rng),0},f(rng)};
          const auto a=SkyBounds::classify(vertices.data()+start,n,planes);
          const auto b=SkyBounds::classify(box->corners,2,planes);
          assert(a==b && SkyBounds::verify(vertices.data()+start,n,planes,b));
          assert(!store.get(vertices.data(),size,8,maximum,start,n));
          assert(!store.get(vertices.data(),size+3,7,maximum,start,n));
          assert(!store.get(vertices.data(),size,7,maximum+3,start,n));
          assert(!store.get(vertices.data(),size,7,maximum,start+1,n));
          assert(!store.get(vertices.data(),size,7,maximum,start,n+1));
          ++checked;
        }
      }
      vertices[0]={7,8,9,1};store.build(vertices.data(),size,8,maximum);
      assert(!store.get(vertices.data(),size,7,maximum,0,std::min(size,step)));
      assert(store.get(vertices.data(),size,8,maximum,0,std::min(size,step)));
      auto replacement=vertices;
      assert(!store.get(replacement.data(),size,8,maximum,0,std::min(size,step)));
      vertices[0].x=std::numeric_limits<float>::quiet_NaN();
      store.build(vertices.data(),size,9,maximum);
      assert(!store.source && store.boxes.empty());
    }
  }
  store.build(nullptr,3,1,75);assert(!store.source);
  V v[3]={{0,0,0,1},{0,0,0,1},{0,0,0,1}};
  store.build(v,0,1,75);assert(!store.source);
  store.build(v,2,1,75);assert(!store.source);
  store.build(v,3,1,17);assert(!store.source);
  std::cout << checked << " prepared slices verified\n";
}
