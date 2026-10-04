#define PRIVATE_QUIET_HOST 1
#include "quiet_runtime.hpp"
#include <cstdlib>
#include <cstring>
#include <initializer_list>
static uint32_t now=0;namespace NightSampler{U hostTicks(){return now;}}
static unsigned checks=0;void check(bool ok){++checks;if(!ok)exit(1);}
int main(int argc,char** argv){if(argc!=6)return 2;unsigned kind=unsigned(atoi(argv[1])),order=unsigned(atoi(argv[2])),joint=unsigned(atoi(argv[3])),restored=unsigned(atoi(argv[4]));for(const char* bad:{"","0 0 0","0 0 0 0 0","+0 0 0 0","0 -1 0 0","1 0 127 0","1 0 31 1","2 0 31 0","2 0 31 32","0 2 0 0","0 0 0 4294967296","0 0 0 0x","3 0 0 0","3 0 31 1","3 2 0 1","3 0 0 8","3 0 0 4294967296"}){FILE* f=fopen("bad.cfg","w");fputs(bad,f);fclose(f);NightPlan::Config c{};check(!NightPlan::readConfig("bad.cfg",c));}remove("bad.cfg");FILE* cfg=fopen("plan.cfg","w");fprintf(cfg,"%u %u %u %u\n",kind,order,joint,restored);fclose(cfg);check(NightRuntime::init("plan.cfg",argv[5]));NightRuntime::config(0,0,0,640,448,60000,1,1,640,448,1,1);
 unsigned previousReads=0;
 for(unsigned index=0;index<5400;++index){NightRuntime::begin(0);unsigned phase=index/1800,offset=index%1800;if(offset==750||offset==1155)printf("LOG: NIGHTCONTEXT phase=%u offset=%u scene=0 night=1 video=0 display=0 depth=0 width=640 height=448 refreshMilliHz=60000 requested=1 dtBits=3c888889 clockBits=00000000 rasterWidth=640 rasterHeight=448 scaleX=1 scaleY=1 ilChoice=0 ilProbing=0 ilBlock=0 ilFrame=0 ilAccepted=0 ilPipelined=1 frameYield=1\n",phase,offset);if(offset==750||offset==1155)printf("LOG: NIGHTCAMERA phase=%u offset=%u positionX=00000000 positionY=00000000 positionZ=00000000 lookX=00000000 lookY=00000000 lookZ=3f800000 sceneGeneration=0 mask=%u\n",phase,offset,NightAblation::mask);
 NightAblation::Counter extraSaved[3];memcpy(extraSaved,NightAblation::extraCounter,sizeof(extraSaved));NightAblation::Counter saved[7];memcpy(saved,NightAblation::commonCounter,sizeof(saved));for(unsigned bit:{1u,2u,4u,8u,16u})if(!NightAblation::skip(bit)&&bit!=8)NightAblation::submitted(bit);
 if(!NightAblation::disabled(NightAblation::LightEffects))for(unsigned bit:{1u,2u,4u})if(!NightAblation::extraSkip(bit))NightAblation::extraSubmitted(bit);
 if(!NightAblation::collectCounters)check(memcmp(extraSaved,NightAblation::extraCounter,sizeof(extraSaved))==0);
 if(!NightAblation::collectCounters)check(memcmp(saved,NightAblation::commonCounter,sizeof(saved))==0);
 now+=100;NightSampler::onPresent(now,NightSampler::PresentKind::Pipeline,index+1,0);NightRuntime::afterLoop(0);now+=10;
 if(offset==1799){check(NightSampler::reads-previousReads==(NightSampler::mode(phase)?262u:6u));previousReads=NightSampler::reads;}
 }
 check(NightSampler::complete());fprintf(stderr,"HOSTCHECKS=%u\n",checks);remove("plan.cfg");}
