// Independent all-pairs oracle for tight swept-body grid admission.
#include "physics/broad_phase.hpp"
#include "physics/convex_collision.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
using namespace Tyra;
static BroadBox broad(CollisionBounds b) {
  return {{b.mn.x-1e-4f,b.mn.y-1e-4f,b.mn.z-1e-4f},
          {b.mx.x+1e-4f,b.mx.y+1e-4f,b.mx.z+1e-4f}};
}
int main() {
  std::mt19937 rng(0x54494748);
  std::uniform_real_distribution<float> pos(-10,10),unit(-1,1),size(.03f,2);
  unsigned retained=0,queries=0,tightCandidates=0,sphereCandidates=0;
  struct Body {CollisionBounds actual,tight;BroadBox sphere;bool awake;};
  for(int scene=0;scene<200;++scene) {
    std::vector<Body> bodies(128);
    BroadPhase tightGrid,sphereGrid;
    tightGrid.begin(int(bodies.size()));sphereGrid.begin(int(bodies.size()));
    for(size_t i=0;i<bodies.size();++i) {
      auto& b=bodies[i];b.awake=(rng()%3)!=0;
      const CollisionV3 p={pos(rng),pos(rng),pos(rng)};
      const CollisionV3 motion=b.awake?CollisionV3{unit(rng)*3,unit(rng)*3,unit(rng)*3}:CollisionV3{};
      float radius=0;
      if(i%5==0) {
        radius=size(rng);
        b.actual={cvSub(p,{radius,radius,radius}),cvAdd(p,{radius,radius,radius})};
        b.actual.mn=cvSub(b.actual.mn,{std::max(0.f,motion.x),std::max(0.f,motion.y),std::max(0.f,motion.z)});
        b.actual.mx=cvSub(b.actual.mx,{std::min(0.f,motion.x),std::min(0.f,motion.y),std::min(0.f,motion.z)});
        b.tight=b.actual;
      } else {
        const float yaw=unit(rng)*3.14f,pitch=unit(rng)*3.14f;
        const float cy=std::cos(yaw),sy=std::sin(yaw),cx=std::cos(pitch),sx=std::sin(pitch);
        const float r[9]={cy,sy*sx,sy*cx,0,cx,-sx,-sy,cy*sx,cy*cx};
        // General off-centre point hulls, not only symmetric primitive boxes.
        CollisionV3 local[24],world[24];CollisionBounds bounds{{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f}};
        const CollisionV3 off={unit(rng),unit(rng),unit(rng)},scale={size(rng),size(rng),size(rng)};
        for(int k=0;k<24;++k) {
          auto& v=local[k];v={off.x+unit(rng)*scale.x,off.y+unit(rng)*scale.y,off.z+unit(rng)*scale.z};
          radius=std::max(radius,std::sqrt(cvDot(v,v)));
          bounds.mn={std::min(bounds.mn.x,v.x),std::min(bounds.mn.y,v.y),std::min(bounds.mn.z,v.z)};
          bounds.mx={std::max(bounds.mx.x,v.x),std::max(bounds.mx.y,v.y),std::max(bounds.mx.z,v.z)};
          world[k]={p.x+(r[0]*v.x+r[1]*v.y+r[2]*v.z),p.y+(r[3]*v.x+r[4]*v.y+r[5]*v.z),p.z+(r[6]*v.x+r[7]*v.y+r[8]*v.z)};
        }
        const auto transformed=collisionTransformBounds(bounds,r,p);
        for(const auto& v:world) {
          assert(v.x>=transformed.mn.x && v.x<=transformed.mx.x);
          assert(v.y>=transformed.mn.y && v.y<=transformed.mx.y);
          assert(v.z>=transformed.mn.z && v.z<=transformed.mx.z);
        }
        b.actual=convexMotionBounds(world,24,motion);
        b.tight=b.awake?b.actual:transformed;
      }
      const CollisionV3 previous=cvSub(p,motion);
      b.sphere={{std::min(p.x,previous.x)-radius,std::min(p.y,previous.y)-radius,std::min(p.z,previous.z)-radius},
                {std::max(p.x,previous.x)+radius,std::max(p.y,previous.y)+radius,std::max(p.z,previous.z)+radius}};
      tightGrid.set(int(i),broad(b.tight),false);sphereGrid.set(int(i),b.sphere,false);
    }
    tightGrid.commit();sphereGrid.commit();
    for(size_t i=0;i<bodies.size();++i)if(bodies[i].awake) {
      ++queries;std::vector<int> near,oldNear,expected,got,oldGot;
      tightGrid.queryBox(broad(bodies[i].actual),near);sphereGrid.queryBox(bodies[i].sphere,oldNear);
      tightCandidates+=unsigned(near.size());sphereCandidates+=unsigned(oldNear.size());
      auto eligible=[&](int j){return j!=int(i) && (!bodies[j].awake || j>int(i)) && collisionBoundsOverlap(bodies[i].actual,bodies[j].actual);};
      for(size_t j=0;j<bodies.size();++j)if(eligible(int(j)))expected.push_back(int(j));
      for(int j:near)if(eligible(j))got.push_back(j);
      for(int j:oldNear)if(eligible(j))oldGot.push_back(j);
      assert(got==expected && oldGot==expected);
      retained+=unsigned(expected.size());
    }
  }
  // Cancellation at large off-centre coordinates must still round outward.
  const CollisionBounds local{{99999.875f,-200000.25f,49999.75f},
                              {100000.125f,-199999.75f,50000.25f}};
  const float angle=.731f,c=std::cos(angle),s=std::sin(angle);
  const float r[9]={c,0,s,0,1,0,-s,0,c};
  const CollisionV3 p{-107931.5f,200000.f,29591.f};
  const auto bound=collisionTransformBounds(local,r,p);
  for(int i=0;i<8;++i) {
    const double x=(i&1)?local.mx.x:local.mn.x;
    const double y=(i&2)?local.mx.y:local.mn.y;
    const double z=(i&4)?local.mx.z:local.mn.z;
    const double wx=p.x+double(r[0])*x+double(r[1])*y+double(r[2])*z;
    const double wy=p.y+double(r[3])*x+double(r[4])*y+double(r[5])*z;
    const double wz=p.z+double(r[6])*x+double(r[7])*y+double(r[8])*z;
    assert(wx>=bound.mn.x && wx<=bound.mx.x);
    assert(wy>=bound.mn.y && wy<=bound.mx.y);
    assert(wz>=bound.mn.z && wz<=bound.mx.z);
  }
  std::printf("tight body grid: %u awake queries, %u exact-envelope pairs retained; candidates %u -> %u, order unchanged\n",queries,retained,sphereCandidates,tightCandidates);
}
