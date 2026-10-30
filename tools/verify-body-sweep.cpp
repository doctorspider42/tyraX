// Full-walk oracle including order, inclusive edges, unsupported bounds and reuse.
#include "ps2rig/body_sweep.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
using namespace Tyra;
static bool valid(const BroadBox& b){for(int k=0;k<3;++k)if(!std::isfinite(b.mn[k])||!std::isfinite(b.mx[k])||b.mn[k]>b.mx[k])return false;return true;}
static bool overlap(const BroadBox& a,const BroadBox& b){for(int k=0;k<3;++k)if(a.mn[k]>b.mx[k]||a.mx[k]<b.mn[k])return false;return true;}
int main(){
  BodySweep index;std::vector<int> out,expected;
  index.queryBox({},out);assert(out.empty());
  std::mt19937 rng(0x5a900);std::uniform_real_distribution<float> pos(-100,100),extent(.001F,20);
  unsigned queries=0;
  for(int scene=0;scene<400;++scene){
    const int n=scene%10==0?1024:1+rng()%192;
    std::vector<BroadBox> boxes(size_t(n),BroadBox{});std::vector<bool> present(size_t(n),false);
    index.begin(n);
    for(int i=n-1;i>=0;--i){if(rng()%11==0)continue;auto& b=boxes[size_t(i)];present[size_t(i)]=true;
      for(int k=0;k<3;++k){const float x=pos(rng),e=extent(rng);b.mn[k]=x-e;b.mx[k]=x+e;}
      // Large spanning rows, ties and invalid entries must not hide later rows.
      if(i%29==0){b.mn[scene%3]=-1e30F;b.mx[scene%3]=1e30F;}
      if(i%37==0)b.mn[0]=std::numeric_limits<float>::quiet_NaN();
      if(i%41==0)b.mn[1]=b.mx[1]+1;
      if(i%43==0)b.mx[2]=std::numeric_limits<float>::infinity();
      index.set(i,b,false);if(i%13==0)index.set(i,b,false);
    }
    index.commit();
    for(int q=0;q<300;++q){BroadBox area;
      if(q<n && present[size_t(q)])area=boxes[size_t(q)];
      else for(int k=0;k<3;++k){const float x=pos(rng),e=extent(rng);area.mn[k]=x-e;area.mx[k]=x+e;}
      if(q%17==0)area.mx[0]=area.mn[0];
      index.queryBox(area,out);expected.clear();
      for(int i=0;i<n;++i)if(present[size_t(i)] && (!valid(area)||!valid(boxes[size_t(i)])||overlap(area,boxes[size_t(i)])))expected.push_back(i);
      assert(out==expected);++queries;
    }
    if(scene%7==0){index.clear();index.queryBox({},out);assert(out.empty());}
  }
  printf("Body sweep: %u independent full-walk comparisons passed, stable original IDs and conservative invalid-bound fallback\n",queries);
}
