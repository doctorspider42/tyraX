// Independent admission oracle: physics-category filtering must retain moving
// nonphysics solids, category flips, births/removals and ascending pair order.
#include "physics/broad_phase.hpp"
#include "physics/world_broad_phase.hpp"
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
  for(int backend=0;backend<2;++backend){
    Tyra::WorldBroadPhase cached;std::vector<int> out;
    const auto kind=static_cast<Tyra::BroadPhase::Kind>(backend);
    const Tyra::BroadBox near{{-1,-1,-1},{4,4,4}},far{{100,100,100},{101,101,101}},all{{-200,-200,-200},{200,200,200}};
    cached.begin(3,kind);cached.set(0,near,false,true);cached.set(1,near,true,true);cached.remove(2);
    cached.queryBox(all,out);assert((out==std::vector<int>{0,1}));const auto built=cached.staticBuildCount();
    cached.begin(3,kind);cached.set(0,near,false,false);cached.set(1,far,true,true);cached.remove(2);
    cached.queryBox(near,out);assert((out==std::vector<int>{0}) && cached.staticBuildCount()==built);
    cached.queryBox(far,out);assert((out==std::vector<int>{1}));
    // A mover settling and a physics->world role flip need no pose change.
    cached.begin(3,kind);cached.remove(0);cached.set(1,far,false,false);cached.set(2,near,false,false);
    cached.queryBox(all,out);assert((out==std::vector<int>{1,2}) && cached.staticBuildCount()>built);
    // A changed asset bound with the same role must invalidate the static index.
    cached.begin(3,kind);cached.remove(0);cached.set(1,near,false,true);cached.set(2,far,false,true);
    // The loose octree may return off-root always-candidates; exact tests filter them.
    cached.queryBox(near,out);
    out.erase(std::remove_if(out.begin(),out.end(),[&](int i){return !overlaps(near,i==1?near:far);}),out.end());
    assert((out==std::vector<int>{1}));
    cached.queryBox(far,out);
    out.erase(std::remove_if(out.begin(),out.end(),[&](int i){return !overlaps(far,i==1?near:far);}),out.end());
    assert((out==std::vector<int>{2}));
    // Many moving solids use a compact grid; reversed slot submission stays sorted.
    cached.begin(12,kind);
    for(int i=11;i>=0;--i)cached.set(i,i%2?near:far,true,true);
    cached.queryBox(near,out);assert((out==std::vector<int>{1,3,5,7,9,11}));
    const auto movers=cached.movingBuildCount();cached.queryBox(far,out);
    assert((out==std::vector<int>{0,2,4,6,8,10}) && cached.movingBuildCount()==movers);
    cached.clear();assert(!cached.initialized());cached.queryBox(all,out);assert(out.empty());
  }
  std::mt19937 rng(0x574f524c);std::uniform_real_distribution<float> p(-40,40),size(.01f,4);
  unsigned queries=0;
  for(int kind=0;kind<2;++kind){Tyra::WorldBroadPhase world;Tyra::BroadPhase general(static_cast<Tyra::BroadPhase::Kind>(kind));
    struct Body{Tyra::BroadBox box;bool present=false,moving=false;struct{bool physics=false;}data;};std::vector<Body> bodies(300);
    for(int frame=0;frame<100;++frame){
      if(frame%25==0){world.clear();general.clear();}
      world.begin(int(bodies.size()),static_cast<Tyra::BroadPhase::Kind>(kind));general.begin(int(bodies.size()));
      for(size_t i=0;i<bodies.size();++i){auto& b=bodies[i];
        // Category changes can occur even when pose/moving status are unchanged.
        if(frame==0 || rng()%3==0){b.present=rng()%9!=0;b.data.physics=rng()%2!=0;b.moving=rng()%3==0;}
        const bool changed=frame==0 || b.moving || (frame%13==0 && i%7==0);
        if(changed)for(int k=0;k<3;++k){b.box.mn[k]=p(rng);b.box.mx[k]=b.box.mn[k]+size(rng);}
        if(b.present)general.set(int(i),b.box,b.moving);
        if(b.present && !b.data.physics)world.set(int(i),b.box,b.moving,changed);else world.remove(int(i));
      }
      general.commit();
      for(int q=0;q<100;++q){Tyra::BroadBox area;
        for(int k=0;k<3;++k){area.mn[k]=p(rng);area.mx[k]=area.mn[k]+size(rng)*3;}
        std::vector<int> expected,out,fallback;world.queryBox(area,out);general.queryBox(area,fallback);
        assert(std::is_sorted(out.begin(),out.end()) && std::adjacent_find(out.begin(),out.end())==out.end());
        for(int i:out)assert(i>=0 && size_t(i)<bodies.size() && bodies[size_t(i)].present && !bodies[size_t(i)].data.physics);
        out.erase(std::remove_if(out.begin(),out.end(),[&](int i){return !overlaps(area,bodies[size_t(i)].box);}),out.end());
        for(size_t i=0;i<bodies.size();++i)if(bodies[i].present && !bodies[i].data.physics && overlaps(area,bodies[i].box))expected.push_back(int(i));
        assert(out==expected);++queries;
      }
    }
  }
  printf("world index: %u full-walk comparisons passed; category changes, movers and ordering retained\n", queries);
}
