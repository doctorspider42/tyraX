// Conservative rejection oracle: every reference SAT/CCD hit must survive.
#include <cassert>
#include <cstdio>
#include <random>
#include "physics/convex_collision.hpp"
using namespace Tyra;
int main() {
  std::mt19937 rng(0x534154);
  std::uniform_real_distribution<float> pos(-8,8), size(.05f,3), angle(-3,3);
  int hits=0, rejects=0;
  for(int trial=0;trial<50000;++trial) {
    CollisionV3 v[2][8], axes[2][3], motion[2];
    for(int k=0;k<2;++k) {
      float y=angle(rng),x=angle(rng),cy=std::cos(y),sy=std::sin(y),cx=std::cos(x),sx=std::sin(x);
      axes[k][0]={cy,0,-sy}; axes[k][1]={sy*sx,cx,cy*sx}; axes[k][2]={sy*cx,-sx,cy*cx};
      CollisionV3 c={pos(rng),pos(rng),pos(rng)};
      float h[3]={size(rng),size(rng),size(rng)};
      for(int i=0;i<8;++i) {
        v[k][i]=c;
        for(int j=0;j<3;++j)v[k][i]=cvAdd(v[k][i],cvMul(axes[k][j],h[j]*((i&(1<<j))?1.f:-1.f)));
      }
      motion[k]={pos(rng),pos(rng),pos(rng)};
    }
    ConvexView a={v[0],8,axes[0],3,axes[0],3},b={v[1],8,axes[1],3,axes[1],3};
    ConvexContact contact;
    bool hit=convexContact(a,b,contact)||convexSweepContact(a,b,motion[0],motion[1],contact);
    bool overlap=collisionBoundsOverlap(convexMotionBounds(v[0],8,motion[0]),convexMotionBounds(v[1],8,motion[1]));
    if(hit){++hits;assert(overlap);}if(!overlap)++rejects;
  }
  // A pass-through with separated endpoints must still enter narrow phase.
  CollisionV3 a[2]={{4,-1,-1},{6,1,1}},b[2]={{-1,-1,-1},{1,1,1}};
  assert(collisionBoundsOverlap(convexMotionBounds(a,2,{10,0,0}),convexMotionBounds(b,2,{0,0,0})));
  assert(hits>1000 && rejects>1000);
  std::printf("50000 swept/rotated box pairs: %d reference hits retained, %d conservative rejections\n",hits,rejects);
}
