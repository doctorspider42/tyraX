#include "tyrax2-normal-census.hpp"
#include <cassert>
int main() {
  using namespace NormalCensus;
  int a,b;
  observe(&a,16,1,1,nullptr,0,0,0);
  assert(totals[0].cold==1 && totals[0].vertices==16);
  beginBag();observe(&a,16,1,1,&a,16,1,1);
  outside();assert(totals[0].hits==1 && totals[0].outside==0);
  beginBag();observe(&b,16,1,1,&a,16,1,1);
  beginBag();observe(&a,16,1,1,&b,16,1,1);
  outside();root();
  assert(totals[0].conflicts==2 && totals[0].repeat==1);
  assert(totals[0].outside==1 && totals[0].outsideVertices==16 && totals[0].roots==1);
  bucket=2;beginBag();observe(&a,16,1,2,&a,16,1,1);
  assert(totals[2].changed==1 && totals[2].repeat==0);
  dump(0);assert(used==0 && bucket==0 && !missed && totals[2].calls==0);
}
