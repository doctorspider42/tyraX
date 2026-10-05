#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include "debug/night_ablation.hpp"
#include "stapip_pool_color_table.hpp"
namespace Tyra {
using u32=uint32_t;struct Vec4{float x,y,z,w;};
struct ColorBag{Vec4* many;const uint32_t* contentVersion;const void* single;};
struct TextureBag{Vec4* coordinates;bool coordinatesAreNormals;};
enum {PipelineInfoBagFrustumCulling_Precise=2,StaPipCullTextureColor=1};
struct Info{bool fullClipChecks,spotLit,dynLightPick;int frustumCulling;};
struct StaPipBag {const ExperimentalPoolTable::View* experimentalPoolTable;Vec4* vertices;TextureBag* texture;ColorBag* color;uint32_t count;bool stripped;void* lighting;void* billboard;Info* info;};
struct StaPipQBuffer {StaPipBag* bag;Vec4 *vertices,*sts,*colors;uint32_t size;bool stripped=false,poolTableSliceValid=false,poolTableApplied=false;ExperimentalPoolTable::Slice poolTableSlice{};void capturePoolTable(StaPipBag*,u32,u32);};
#include "actual-capture.inc"
struct Program{int name=StaPipCullTextureColor;int getName(){return name;}};
struct Repo{bool overridden=false;bool hasAnyOverride(){return overridden;}}repository;
static Program prog;static Program* dBufferPrograms[]={&prog};
unsigned getQBufferIndex(StaPipQBuffer*){return 0;}
void route(StaPipQBuffer* buffer){
#include "actual-route.inc"
}
}
static unsigned checks=0;
static void need(bool x){++checks;if(!x){std::fprintf(stderr,"FAIL routing check=%u\n",checks);std::exit(1);}}
int main(){using namespace Tyra;using namespace ExperimentalPoolTable;ColorBits c[8];Vec4 verts[768]{},sts[768]{},colors[768];
 for(unsigned m=0;m<8;++m){c[m]={{0x42000000u+m*0x1000,0x42200000u+m*0x1000,0x42300000u+m*0x1000,0x43000000u}};for(unsigned k=0;k<96;++k)std::memcpy(&colors[m*96+k],&c[m],16);}
 uint32_t generation=42;View view{c,8,768,42};TextureBag tex{sts,false};ColorBag cb{colors,&generation,nullptr};Info info{true,false,false,PipelineInfoBagFrustumCulling_Precise};StaPipBag bag{&view,verts,&tex,&cb,768,false,nullptr,nullptr,&info};StaPipQBuffer b{};b.bag=&bag;b.vertices=verts+75;b.sts=sts+75;b.colors=colors+75;b.size=75;
 for(bool selected:{false,true})for(bool enabled:{false,true})for(bool collect:{false,true}){b.poolTableSliceValid=false;b.capturePoolTable(&bag,75,75);need(b.poolTableSliceValid);NightAblation::poolTableSelected=selected;NightAblation::setPoolTableEnabled(enabled);NightAblation::collectCounters=collect;NightAblation::poolTableCounter={};auto old=NightAblation::poolTableCounter;route(&b);need(b.poolTableApplied==enabled);if(!selected||!collect)need(std::memcmp(&old,&NightAblation::poolTableCounter,sizeof old)==0);else {need(NightAblation::poolTableCounter.eligible==1);need(NightAblation::poolTableCounter.coldCompared==1);need(NightAblation::poolTableCounter.coldMismatches==0);}}
 NightAblation::poolTableSelected=true;NightAblation::setPoolTableEnabled(true);NightAblation::collectCounters=true;
 for(unsigned reason=0;reason<8;++reason){b.poolTableSliceValid=false;b.capturePoolTable(&bag,75,75);Info old=info;
  if(reason==0){b.stripped=true;}if(reason==1){info.fullClipChecks=false;}if(reason==2){info.spotLit=true;}if(reason==3){info.dynLightPick=true;}if(reason==4){info.frustumCulling=0;}if(reason==5){tex.coordinatesAreNormals=true;}if(reason==6){repository.overridden=true;}if(reason==7){prog.name=5;}
  NightAblation::poolTableCounter={};route(&b);need(!b.poolTableApplied);need(NightAblation::poolTableCounter.fallback==1);info=old;b.stripped=false;tex.coordinatesAreNormals=false;repository.overridden=false;prog.name=StaPipCullTextureColor;
 }
 for(unsigned reason=0;reason<5;++reason){b.poolTableSliceValid=false;auto* oldSt=b.sts;auto* oldCol=b.colors;
  if(reason==0){b.sts++;}if(reason==1){b.colors++;}if(reason==2){generation++;}if(reason==3){view.total--;}if(reason==4){bag.stripped=true;}
  b.capturePoolTable(&bag,75,75);need(!b.poolTableSliceValid);b.sts=oldSt;b.colors=oldCol;generation=42;view.total=768;bag.stripped=false;
 }
 b.poolTableSliceValid=false;b.capturePoolTable(&bag,75,75);colors[80].x+=1;NightAblation::poolTableCounter={};route(&b);need(!b.poolTableApplied);need(NightAblation::poolTableCounter.coldMismatches==1);need(!NightAblation::valid);
 std::printf("PASS routing checks=%u actualCaptureAndRouteExtracted=1 coldCounterNoWrites=1 malformedSourceFallback=1 sharedSDKTypesShim=1 nativeExecuted=0\n",checks);
}
