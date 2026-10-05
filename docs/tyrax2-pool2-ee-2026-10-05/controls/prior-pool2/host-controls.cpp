#define PRIVATE_QUIET_HOST 1
#include "quiet_runtime.hpp"
#include <cstdlib>
#include <cstring>
#include <initializer_list>
static uint32_t now=0;namespace NightSampler{U hostTicks(){return now;}}
static unsigned checks=0;void check(bool ok){++checks;if(!ok)exit(1);}
static void tableGuardControls(){using namespace NightAblation;
 poolTableSelected=false;collectCounters=true;poolTableCounter={};setPoolTableEnabled(true);observePoolTable(true,75,true,false);check(poolTableCounter.invocations==0&&valid);
 poolTableSelected=true;collectCounters=false;observePoolTable(true,0,false,true);check(poolTableCounter.invalid==0&&valid);
 for(unsigned type=0;type<5;++type){poolTableCounter={};valid=true;collectCounters=true;
  if(type==0)observePoolTable(true,0,true,false);
  if(type==1)observePoolTable(true,76,true,false);
  if(type==2)observePoolTable(false,1,false,true);
  if(type==3){poolTableCounter.invocations=~uint32_t(0);observePoolTable(false,1,true,false);}
  if(type==4)observePoolTable(true,1,true,true);
  check(!valid);check(type==4?poolTableCounter.coldMismatches==1:poolTableCounter.invalid>0);
 }
 poolTableCounter={};valid=true;collectCounters=false;poolTableSelected=false;setPoolTableEnabled(false);
}
int main(int argc,char** argv){if(argc!=6)return 2;tableGuardControls();unsigned kind=unsigned(atoi(argv[1])),order=unsigned(atoi(argv[2])),joint=unsigned(atoi(argv[3])),restored=unsigned(atoi(argv[4]));for(const char* bad:{"","0 0 0","0 0 0 0 0","+0 0 0 0","0 -1 0 0","1 0 127 0","1 0 31 1","2 0 31 0","2 0 31 32","0 2 0 0","0 0 0 4294967296","0 0 0 0x","3 0 0 0","3 0 31 1","3 2 0 1","3 0 0 8","3 0 0 4294967296","4 0 0 0","5 0 0 1","6 0 31 0","5 2 0 0","6 0 0 7","7 0 0 1","7 0 31 0","7 2 0 0","7 0 0 4294967296"}){FILE* f=fopen("bad.cfg","w");fputs(bad,f);fclose(f);NightPlan::Config c{};check(!NightPlan::readConfig("bad.cfg",c));}remove("bad.cfg");FILE* cfg=fopen("plan.cfg","w");fprintf(cfg,"%u %u %u %u\n",kind,order,joint,restored);fclose(cfg);check(NightRuntime::init("plan.cfg",argv[5]));NightRuntime::config(0,0,0,640,448,60000,1,1,640,448,1,1);
 unsigned previousReads=0;
 for(unsigned index=0;index<5400;++index){NightRuntime::begin(0);unsigned phase=index/1800,offset=index%1800;check(NightAblation::poolTableSelected==(kind==7));check(NightAblation::poolTableEnabled==(kind==7&&((phase==1)!=(order==1))));if(offset==750||offset==1155)printf("LOG: NIGHTCONTEXT phase=%u offset=%u scene=0 night=1 video=0 display=0 depth=0 width=640 height=448 refreshMilliHz=60000 requested=1 dtBits=3c888889 clockBits=00000000 rasterWidth=640 rasterHeight=448 scaleX=1 scaleY=1 ilChoice=0 ilProbing=0 ilBlock=0 ilFrame=0 ilAccepted=0 ilPipelined=1 frameYield=1\n",phase,offset);if(offset==750||offset==1155)printf("LOG: NIGHTCAMERA phase=%u offset=%u positionX=00000000 positionY=00000000 positionZ=00000000 lookX=00000000 lookY=00000000 lookZ=3f800000 sceneGeneration=0 mask=%u\n",phase,offset,NightAblation::mask);
 const auto tableSaved=NightAblation::poolTableCounter;NightAblation::Counter extraSaved[3];memcpy(extraSaved,NightAblation::extraCounter,sizeof(extraSaved));NightAblation::WildCounter wildSaved[2]={NightAblation::wildPlaneCounter,NightAblation::wildConeCounter};
 NightAblation::Counter saved[7];memcpy(saved,NightAblation::commonCounter,sizeof(saved));for(unsigned bit:{1u,2u,4u,8u,16u})if(!NightAblation::skip(bit)&&bit!=8)NightAblation::submitted(bit);
 if(!NightAblation::disabled(NightAblation::LightEffects))for(unsigned bit:{1u,2u,4u})if(!NightAblation::extraSkip(bit))NightAblation::extraSubmitted(bit);
 if(!NightAblation::collectCounters)check(memcmp(extraSaved,NightAblation::extraCounter,sizeof(extraSaved))==0);
 if(kind==7){NightAblation::observePoolTable(true,75u,true,false);NightAblation::observePoolTable(false,76u,true,false);}
 if(!NightAblation::collectCounters)check(memcmp(&tableSaved,&NightAblation::poolTableCounter,sizeof(tableSaved))==0);
 if(kind==5||kind==6){NightAblation::observeWild(kind,true,NightAblation::wildActive(kind),false,false,true,false,2u,1u);NightAblation::observeWild(kind,false,false,true,false,true,false,1u,0u);}
 if(!NightAblation::collectCounters){check(memcmp(&wildSaved[0],&NightAblation::wildPlaneCounter,sizeof(wildSaved[0]))==0);check(memcmp(&wildSaved[1],&NightAblation::wildConeCounter,sizeof(wildSaved[1]))==0);}
 if(!NightAblation::collectCounters)check(memcmp(saved,NightAblation::commonCounter,sizeof(saved))==0);
 now+=100;NightSampler::onPresent(now,NightSampler::PresentKind::Pipeline,index+1,0);NightRuntime::afterLoop(0);now+=10;
 if(offset==1799){check(NightSampler::reads-previousReads==(NightSampler::mode(phase)?262u:6u));previousReads=NightSampler::reads;}
 }
 check(NightSampler::complete());fprintf(stderr,"HOSTCHECKS=%u\n",checks);remove("plan.cfg");}
