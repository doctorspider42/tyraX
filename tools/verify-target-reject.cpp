#include "tyrax2-target-reject.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>

int main() {
  std::mt19937 rng(163);
  std::uniform_real_distribution<float> values(-100,100), small(-1,1);
  unsigned rejected=0, kept=0;
  for (unsigned trial=0;trial<200000;++trial) {
    float m[16], lo[3],hi[3];
    for (float& v:m) v=small(rng);
    for (unsigned i=0;i<3;++i) {lo[i]=values(rng);hi[i]=lo[i]+std::fabs(values(rng));}
    const float size=64.0F*(1+rng()%8);
    unsigned plane=TargetReject::outside(m,lo,hi,size);
    if (!plane) {++kept;continue;}
    ++rejected;
    const unsigned row=(plane-1)/2;
    const double sign=((plane-1)&1)?1:-1, band=(double(size)*.5+1)/2048;
    for (unsigned corner=0;corner<8;++corner) {
      double v=double(m[15])*band+sign*m[12+row];
      for (unsigned i=0;i<3;++i)
        v+=(double(m[4*i+3])*band+sign*m[4*i+row])*((corner&(1<<i))?hi[i]:lo[i]);
      assert(v<0);
    }
  }
  assert(rejected && kept);
  // Exact target edge and the one-pixel safety shell must remain accepted.
  float m[16]={};m[0]=m[5]=1;m[15]=1;
  float lo[3]={64.0F/2048,0,0},hi[3]={65.0F/2048,0,0};
  assert(!TargetReject::outside(m,lo,hi,128));
  lo[0]=hi[0]=66.0F/2048;
  assert(TargetReject::outside(m,lo,hi,128));
  m[7]=std::numeric_limits<float>::quiet_NaN();
  assert(!TargetReject::outside(m,lo,hi,128));
  m[7]=0;lo[1]=std::numeric_limits<float>::infinity();
  assert(!TargetReject::outside(m,lo,hi,128));
  printf("TARGETREJECT checks=200004 rejected=%u kept=%u valid=1\n",rejected,kept);
}
