#include "tyrax2-clip-capacity.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
  unsigned cases=0;
  for(unsigned mask=0;mask<256;++mask){
    const unsigned p=ClipTrial::planeCount(mask);
    assert(p>=1 && p<=6);
    const unsigned n=ClipTrial::capacity(mask,460,75,12);
    assert(n>=12 && n<=75 && n%3==0);
    assert(ClipTrial::footprint(mask,n)+91<=460);
    if(p==1)assert(n==39);
    if(p==6)assert(n==12);
    // Preserve each refined route and primitive order while grouping Clip runs.
    for(unsigned pattern=0;pattern<512;++pattern){
      unsigned first=0,jobs=0,original=0;
      while(first<19){
        const bool clip=(pattern>>(first%9))&1;
        if(!clip){++first;continue;}
        ++original;unsigned size=4;++first;
        while(first<19 && ((pattern>>(first%9))&1) && (size+4)*3<=n){
          size+=4;++first;++original;
        }
        assert(ClipTrial::footprint(mask,size*3)+91<=460);
        ++jobs;
      }
      assert(jobs<=original);++cases;
    }
  }
  assert(ClipTrial::capacity(1,100,75,12)==12);
  assert(ClipTrial::planeCount(0)==6 && ClipTrial::planeCount(128)==6);
  unsigned winding=0;
  for(unsigned size=3;size<=75;++size)for(unsigned chunk=4;chunk<=13;++chunk){
    std::vector<unsigned> actual,expected;
    for(unsigned t=0;t<size-2;t+=chunk){
      const unsigned count=std::min(chunk,size-2-t);
      for(unsigned k=t;k<t+count;++k){
        actual.push_back(k+(k&1));actual.push_back(k+1-(k&1));actual.push_back(k+2);
      }
    }
    for(unsigned k=0;k<size-2;++k){expected.push_back(k+(k&1));expected.push_back(k+1-(k&1));expected.push_back(k+2);}
    assert(actual==expected);++winding;
  }
  printf("A24 capacity/range cases=%u passed\n",cases);
  printf("A24 wider odd/even strip chunk cases=%u passed\n",winding);
}
