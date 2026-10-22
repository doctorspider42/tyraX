#include "tyrax2-paint-lifecycle.hpp"
#include <cassert>
#include <iostream>
int main() {
  using namespace PaintLife;
  State s;
  alignas(16) float normals[8]={0,0,1,0,1,0,0,0};
  alignas(16) float colors[8]{};
  float f[3]={0,0,1},h[3]={1,0,0};
  assert(s.changed(normals,2,1) && !s.needsEe(false));
  s.accept(normals,2,1,f,h);
  assert(!s.changed(normals,2,1));
  assert(s.changed(normals+4,2,1) && s.changed(normals,1,1) && s.changed(normals,2,2));
  assert(!s.needsEe(true) && s.needsEe(false));
  for(unsigned repeat=0;repeat<1000;++repeat) {
    for(unsigned m=0;m<6;++m) {
      mode=m;
      bool vu=m!=0&&m!=2&&m!=4;
      bool refresh=s.needsEe(vu);
      if(refresh) {
        expected(normals,s,colors); expected(normals+4,s,colors+4);
        s.eeReady=true;
      }
      check(s,vu,false,refresh,repeat%3,normals,colors,2);
      assert(errors==0);
      assert(s.forward[2]==1 && s.half[0]==1);
    }
    // A new accepted basis invalidates EE, even when a VU draw was last.
    s.accept(normals,2,repeat+2,f,h);
    assert(s.needsEe(false) && !s.needsEe(true));
  }
  assert(heldRefreshes==1000 && modeMask==63 && lodMask==7);
  // Corrupting cached EE colour must be visible to the independent oracle.
  expected(normals,s,colors); expected(normals+4,s,colors+4); s.eeReady=true;
  colors[0]+=2; mode=0; unsigned before=errors;
  check(s,false,false,false,0,normals,colors,2); assert(errors>before);
  State fresh; assert(!fresh.valid && fresh.changed(normals,2,1));
  std::cout<<"PASS 6000 mode transitions; source/count/stamp invalidation; stale-colour control\n";
}
