// Contact identity, invalidation, uncapped storage and island partition gates.
#include "physics/contact_history.hpp"
#include <cassert>
#include <cstdio>
using namespace Tyra;
int main() {
  ContactHistory h;
  ContactMemory c{3,7,1,2,0,{.2f,0,0},{-.2f,0,0},{0,1,0},{},0};
  h.begin(1,1.f/60);
  int id=h.add(c,true);assert(h[id].normalImpulse==0);
  h[id].normalImpulse=2;h[id].friction={.2f,0,0};
  h.begin(1,1.f/60);id=h.add(c,true);assert(h.hits==1 && h[id].normalImpulse==2);
  assert(h[h.add(c,true)].normalImpulse==0); // one-to-one manifold matching
  h[id].normalImpulse=2;
  h.begin(1,1.f/60);auto changed=c;changed.revisionA++;
  assert(h[h.add(changed,true)].normalImpulse==0);
  h.begin(1,1.f/60);id=h.add(c,true);h[id].normalImpulse=3;
  h.begin(1,1.f/30);assert(h[h.add(c,true)].normalImpulse==0);
  h.begin(2,1.f/30);assert(h[h.add(c,true)].normalImpulse==0);
  h.begin(2,1.f/30);id=h.add(c,true);h[id].normalImpulse=3;
  h.begin(2,1.f/30);changed=c;changed.state=2;
  assert(h[h.add(changed,true)].normalImpulse==0);
  h.begin(2,1.f/30);id=h.add(c,true);h[id].normalImpulse=3;
  h.begin(2,1.f/30);changed=c;changed.anchorB.x+=.04f;
  assert(h[h.add(changed,true)].normalImpulse==0);
  h.begin(2,1.f/30);id=h.add(c,true);h[id].normalImpulse=3;
  h.begin(2,1.f/30);changed=c;changed.normal={1,0,0};
  assert(h[h.add(changed,true)].normalImpulse==0);
  // More history than the old 384-contact reserve, including hash collisions.
  h.begin(4,1.f/60);
  for(int i=0;i<4096;++i) {auto x=c;x.a=i;id=h.add(x,true);h[id].normalImpulse=float(i+1);}
  h.begin(4,1.f/60);
  for(int i=4095;i>=0;--i){auto x=c;x.a=i;assert(h[h.add(x,true)].normalImpulse==float(i+1));}
  assert(h.hits==4096);
  struct C {int a,b;};
  std::vector<C> contacts={{0,-1},{2,-1},{1,0},{3,2},{0,1},{2,3}};
  ContactIslands islands;islands.begin(4);islands.link(1,0);islands.link(3,2);
  islands.arrange(contacts);assert(islands.count==2);
  assert((islands.order==std::vector<int>{0,2,4,1,3,5}));
  assert(islands.root(0)!=islands.root(2)); // shared world never connects them
  SeparatingAxisHistory axes;axes.begin(1);axes.acquire(1,2)={1,0,0};
  axes.begin(1);assert(axes.acquire(1,2).x==1);assert(axes.acquire(2,1).x==0);
  axes.begin(2);assert(axes.acquire(1,2).x==0);
  std::puts("contact history: invalidations, one-to-one matching, 4096 records and stable islands passed");
}
