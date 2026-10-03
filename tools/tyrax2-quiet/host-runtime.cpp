#define PRIVATE_QUIET_HOST 1
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "quiet_runtime.hpp"
namespace QuietCadence {U clockValue=0;U hostTicks(){return clockValue;}}
int main(int argc,char** argv){assert(argc==3||argc==4);using namespace QuietCadence;unsigned selected=unsigned(std::atoi(argv[1])),mixed=unsigned(std::atoi(argv[2])),requested=argc==4?unsigned(std::atoi(argv[3])):1;
 {FILE* f=fopen("quiet-control.cfg","w");assert(f);fprintf(f,"%u\n",selected);fclose(f);}
 QuietRuntime::init("quiet-control.cfg","quiet-cadence.log");assert(valid);QuietRuntime::config(1,1,1,512,448,60000,requested,0,512,448,1,1);
 puts("LOG: QUIETWORKLOAD model=firstPerson nightKnown=0 authoredSaveValuesUnchanged=1 scriptModeOpaque=1");
 unsigned stall=0;
 for(unsigned i=0;i<5400;++i){clockValue=0xf0000000U+i*10000;QuietRuntime::begin(stall);const unsigned offset=i%1800,p=i/1800;
  if(offset==750||offset==1155)printf("LOG: QUIETCONTEXT phase=%u offset=%u scene=0 night=0 video=1 display=1 depth=1 width=512 height=448 refreshMilliHz=60000 requested=%u dtBits=3c888888 clockBits=41200000 rasterWidth=512 rasterHeight=448 scaleX=1 scaleY=1 ilChoice=0 ilProbing=1 ilBlock=0 ilFrame=1 ilAccepted=0 ilPipelined=%u frameYield=0 source=hostSpecification\n",p,offset,requested,requested);
  clockValue+=8000;stall+=1000;
  if(!mixed||i%17!=0){onPresent(clockValue,requested?PresentKind::Pipeline:PresentKind::Synchronous,requested?i+1:0,1);if(mixed&&i%19==0)onPresent(clockValue+100,PresentKind::Synthetic,0,1);}
  clockValue+=1000;QuietRuntime::afterLoop(stall);
 }
 assert(complete()&&QuietRuntime::done&&QuietRuntime::file==nullptr);printf("PASS actual quiet runtime5400 order%u mixed%u\n",selected,mixed);
}
