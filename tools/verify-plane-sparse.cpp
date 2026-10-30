// Compare sparse plane arithmetic with the existing generic half-space test.
#include "ps2rig/plane_sparse.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
using V=Tyra::CollisionV3;
static int reference(const float* lp,int np,V l,float* depth){
  int best=-1;float bd=1e30F;
  for(int k=0;k<np;++k){const float* pl=lp+k*4;
    const float d=pl[3]-(pl[0]*l.x+pl[1]*l.y+pl[2]*l.z);
    if(d<=0)return -1;
    if(d<bd){bd=d;best=k;}
  }
  *depth=bd;return best;
}
static unsigned bits(float f){unsigned u;std::memcpy(&u,&f,4);return u;}
static float value(unsigned u){float f;std::memcpy(&f,&u,4);return f;}
static unsigned tested=0,fast=0;
static void check(const float* p,V v){
  float a=123,b=123;const int x=reference(p,6,v,&a);
  const bool eligible=PlaneSparse::classify(p,6)&&PlaneSparse::normalPoint(v);
  const int y=eligible?PlaneSparse::inside(p,v,&b):reference(p,6,v,&b);
  ++tested;fast+=eligible;
  if(x!=y || (x>=0 && bits(a)!=bits(b))){
    printf("Mismatch p=%g,%g,%g face=%d/%d depth=%a/%a\n",v.x,v.y,v.z,x,y,double(a),double(b));
    std::abort();
  }
}
int main(){
  float p[24]={0,-1,0,1, 1,0,0,1, 0,0,-1,1, 0,0,1,1, -1,0,0,1, 0,1,0,1};
  assert(PlaneSparse::classify(p,6)&&!PlaneSparse::classify(p,5));
  for(int k=0;k<18;++k){const int i=(k/3)*4+k%3;const float old=p[i];
    p[i]=std::nextafter(old,old==1?2.0F:1.0F);assert(!PlaneSparse::classify(p,6));p[i]=old;}
  const float special[]={0.0F,-0.0F,std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min(),
    std::numeric_limits<float>::min(),-std::numeric_limits<float>::min(),std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),
    std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
  for(float x:special)for(int axis=0;axis<3;++axis){float v[3]={.25F,.5F,.75F};v[axis]=x;check(p,{v[0],v[1],v[2]});}
  const float near[]={std::nextafter(-1.0F,-2.0F),-1,std::nextafter(-1.0F,0.0F),-.5F,.5F,std::nextafter(1.0F,0.0F),1,std::nextafter(1.0F,2.0F)};
  for(float x:near)for(float y:near)for(float z:near)check(p,{x,y,z});
  std::mt19937 rng(0x501e);std::uniform_real_distribution<float>unit(-1,1);
  for(int i=0;i<10000000;++i){
    // Arbitrary normal float coordinates and offsets screen extreme ranges;
    // every other group stays inside an asymmetric box to exercise all faces.
    V v;
    if(i&1){v={value(rng()),value(rng()),value(rng())};for(int k=0;k<6;++k)p[k*4+3]=value(rng());}
    else {float h[3];for(float& x:h)x=std::ldexp(1.0F+std::fabs(unit(rng)),int(rng()%41)-20);
      p[3]=h[1];p[7]=h[0];p[11]=h[2]*1.1F;p[15]=h[2];p[19]=h[0]*1.1F;p[23]=h[1]*1.1F;
      v={unit(rng)*h[0]*.99F,unit(rng)*h[1]*.99F,unit(rng)*h[2]*.99F};}
    for(int k=0;k<6;++k)for(int a=0;a<3;++a)if(p[k*4+a]==0)p[k*4+a]=(rng()&1)?0.0F:-0.0F;
    check(p,v);
  }
  printf("Sparse planes: %u face/depth bit comparisons passed, %u eligible; boundaries and exceptional fallback covered\n",tested,fast);
}
