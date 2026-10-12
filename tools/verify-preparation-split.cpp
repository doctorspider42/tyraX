#define PREPARATION_SPLIT_HOST
#include "tyrax2-preparation-split.hpp"
#include <cassert>
#include <cstdio>
namespace PreparationSplit { U now=0, reads=0; U tick(){++reads;return now;} }
int main(){
  using namespace PreparationSplit;
  for(U mode=0;mode<3;++mode){
    reads=0;now=0;begin(mode);
    {Scope a(StaPip);now=10;
      {Scope b(StaPip);now=20;
        {Scope q(Queue);now=30;{Scope producer(StaPip);now=35;}now=40;}
        now=50;}
      now=60;}
    boundary();{Scope q(Queue);now=70;{Scope qq(Queue);now=80;}now=90;}end();
    assert(!errors && !depth && current==Other);
    if(mode==0){assert(reads==0 && parts[0].staCalls==0 && parts[1].queueCalls==0);}
    else {assert(parts[0].staCalls==3 && parts[0].queueCalls==1 && parts[1].queueCalls==2);}
    if(mode==2){assert(parts[0].sta==45 && parts[0].queue==15 && parts[1].queue==30 && reads==8);}
    else assert(!reads && !parts[0].sta && !parts[0].queue && !parts[1].queue);
  }
  now=0xfffffff0;begin(2);{Scope q(Queue);now=0x10;}end();assert(parts[0].queue==32);
  begin(2);{Scope q(Queue);boundary();}end();assert(errors>0);
#if PREPARATION_SPLIT_GEOMETRY
  now=0;begin(2);
  {Scope setup(StaPip);now=10;{Scope geo(Geometry);now=20;
    {Scope q(Queue);now=30;{Scope deferred(Geometry);now=35;}now=40;}now=50;}now=60;}
  end();assert(!errors && parts[0].sta==20 && parts[0].geometry==25 && parts[0].queue==15);
  assert(parts[0].geometryCalls==2 && parts[0].reads==8);
  for(U m=0;m<2;++m){reads=0;begin(m);{Scope geo(Geometry);now+=10;}end();
    assert(!reads && !parts[0].geometry && parts[0].geometryCalls==m);}
  assert(sizeof(Sample)==72);
#else
  assert(sizeof(Sample)==56);
#endif
  puts("PASS: off/count/timed, nested same/different categories, callback, phase boundary, wrap and invalid boundary");
}
