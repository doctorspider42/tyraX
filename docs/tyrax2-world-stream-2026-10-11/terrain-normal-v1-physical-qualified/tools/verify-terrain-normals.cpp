#define TERRAIN_NORMALS_HOST
#include "tyrax2-terrain-normals.hpp"
#include <cassert>
#include <cmath>
int main(){
  using namespace TerrainNormals;
  float h[81*81]{};
  unsigned calls=0;
  auto compute=[&](int x,int z){++calls;return Normal{float(x),float(z),-0.0F};};
  ensure(1,h,81,81,4,4,compute);
  assert(calls==2*81*81 && source.size()*sizeof(Normal)==78732);
  for(int z=0;z<81;++z)for(int x=0;x<81;++x){
    const auto n=at(x,z);assert(n.x==x && n.y==z && std::signbit(n.z));
  }
  for(int i=0;i<64;++i){enabled=i%2;ensure(1,h,81,81,4,4,compute);}
  assert(calls==2*81*81);
  ensure(2,h,81,81,4,4,compute);assert(calls==4*81*81);
  float other[81*81]{};
  ensure(2,other,81,81,4,4,compute);assert(calls==6*81*81);
  ensure(2,other,81,81,8,4,compute);assert(calls==8*81*81);
  ensure(2,other,7,5,8,4,compute);assert(source.size()==35);
  assert(at(6,4).x==6 && at(6,4).y==4);
  reset();assert(builds==0 && uses==0 && audits==0 && source.size()==35);
  std::puts("Terrain normal storage/key/index checks PASS; no target/FPS proof");
}
