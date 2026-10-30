// Independent admission oracle: physics-category filtering must retain moving
// nonphysics solids, category flips, births/removals and ascending pair order.
#include "physics/broad_phase.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
static bool overlaps(const Tyra::BroadBox& a,const Tyra::BroadBox& b){
  for(int k=0;k<3;++k){if(a.mn[k]>b.mx[k] || a.mx[k]<b.mn[k])return false;}
  return true;
}
int main(){
  std::mt19937 rng(0x574f524c);std::uniform_real_distribution<float> p(-40,40),size(.01f,4);
  unsigned queries=0;
  for(int kind=0;kind<2;++kind){Tyra::BroadPhase world(static_cast<Tyra::BroadPhase::Kind>(kind));
    struct Body{Tyra::BroadBox box;bool active=false,physics=false,moving=false;};std::vector<Body> bodies(300);
    for(int frame=0;frame<100;++frame){
      if(frame%25==0)world.clear();
      world.begin(int(bodies.size()));
      for(size_t i=0;i<bodies.size();++i){auto& b=bodies[i];
        // Category changes can occur even when pose/moving status are unchanged.
        if(frame==0 || rng()%3==0){b.active=rng()%9!=0;b.physics=rng()%2!=0;b.moving=rng()%3==0;}
        if(frame==0 || b.moving)for(int k=0;k<3;++k){b.box.mn[k]=p(rng);b.box.mx[k]=b.box.mn[k]+size(rng);}
        if(b.active && !b.physics)world.set(int(i),b.box,b.moving);
      }
      world.commit();
      for(int q=0;q<100;++q){Tyra::BroadBox area;
        for(int k=0;k<3;++k){area.mn[k]=p(rng);area.mx[k]=area.mn[k]+size(rng)*3;}
        std::vector<int> expected,out;world.queryBox(area,out);
        assert(std::is_sorted(out.begin(),out.end()) && std::adjacent_find(out.begin(),out.end())==out.end());
        for(int i:out)assert(i>=0 && size_t(i)<bodies.size() && bodies[size_t(i)].active && !bodies[size_t(i)].physics);
        out.erase(std::remove_if(out.begin(),out.end(),[&](int i){return !overlaps(area,bodies[size_t(i)].box);}),out.end());
        for(size_t i=0;i<bodies.size();++i)if(bodies[i].active && !bodies[i].physics && overlaps(area,bodies[i].box))expected.push_back(int(i));
        assert(out==expected);++queries;
      }
    }
  }
  printf("world index: %u full-walk comparisons passed, moving solids/categories/order retained\n",queries);
}
