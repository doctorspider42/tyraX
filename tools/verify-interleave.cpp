#include <cstdint>
#include <vector>
#include <cstdio>
#include <cassert>
#include <cmath>
#include "../vendor/tyra/engine/inc/renderer/renderer_settings.hpp"
using u32=uint32_t;
int INTERLEAVE_PASSES=1;constexpr bool DEBUG_SHOW_PROFILER=false;
#define TYRA_LOG(...) ((void)0)
static u32 ticks=0;static int clockReads=0;static u32 ilObservationTicks(){++clockReads;return ticks;}
namespace Tyra{struct HardwareTrace{inline static bool active=false;};struct StaPipBag{};struct Vif1Queue{static bool effective;static bool recordingPipelined(){return effective;}};bool Vif1Queue::effective=true;}
struct Settings {
 Tyra::VideoMode video=Tyra::VideoMode::Auto;
 Tyra::DisplayMode display=Tyra::DisplayMode::Progressive480p;
 Tyra::ColorDepth color=Tyra::ColorDepth::Bits32;
 bool field=false,wide=false,dither=true;
 float width=448,height=448,renderHeight=448,rasterWidth=448,rasterHeight=448;
 Tyra::VideoMode getVideoMode()const{return video;}
 Tyra::DisplayMode getDisplayMode()const{return display;}
 Tyra::ColorDepth getColorDepth()const{return color;}
 bool isFieldRendering()const{return field;}bool getWidescreen()const{return wide;}
 bool getDither()const{return dither;}float getWidth()const{return width;}
 float getHeight()const{return height;}float getRenderHeightF()const{return renderHeight;}
 float getRasterWidthF()const{return rasterWidth;}float getRasterHeightF()const{return rasterHeight;}
};
struct Core {
 u32 generation=0,stall=0;bool requested=true,limiter=true,yield=false;
 struct GS{u32 buffers=2;struct Target{int frameAddress=0,frameWidth=448,scissorX0=0,scissorX1=447,scissorY0=0,scissorY1=447,offsetX16=0,offsetY16=0;}target;
 u32 getFrameBufferCount()const{return buffers;}Target getRasterTarget()const{return target;}}gs;
 struct BLSS{bool enabled=false,network=false;int lowW=224,lowH=224;
 bool isEnabled()const{return enabled;}bool usesNetwork()const{return network;}
 int getLowResW()const{return lowW;}int getLowResH()const{return lowH;}}blss;
 Settings settings;u32 getRecordingGeneration()const{return generation;}
 const Settings&getSettings()const{return settings;}bool getFrameLimit()const{return limiter;}
 bool getFrameYield()const{return yield;}bool getFramePipeline()const{return requested;}u32 getStallTotal()const{return stall;}
};
struct Engine{struct Renderer{Core core;}renderer;};
struct Portal{int scene,object,target;};static std::vector<Portal> PORTALS;static int PORTAL_COUNT=0;
struct Object{bool active=true,visible=true;};
struct TerrainGame{
 Engine storage;Engine*engine=&storage;int mockCameraRig=0;bool splitPassActive=false;u32 sceneGeneration=0;int currentScene=0;
 struct Context{bool cameraOverride=false;uintptr_t cameraSource=0;}scriptCtx;
 std::vector<Object> runtimeObjects;std::vector<unsigned char>portalLiveFlags;
 std::vector<Tyra::StaPipBag*>heavyBags;size_t heavyNext=0;int heavyDrawn=0,heavyPrevDrawn=16,ilLastBlendAt=-1;
 void ilAccount(u32);bool interleaveBegin(bool serializedCost=false);
#include "actual-fields.inc"
};
#include "actual-methods.inc"
static unsigned checks=0;
#define CHECK(x) do{++checks;assert(x);}while(0)
struct Driver{TerrainGame g;bool last=false,previous=false;bool started=false;unsigned accepted=0;
 bool step(u32 work=0,u32 pacing=100){auto&c=g.engine->renderer.core;if(started){ticks+=work?work:(last?1000:1300);ticks+=pacing;c.stall+=pacing;}++c.generation;
 bool sampled=g.ilHaveMark&&g.ilWarmup==0&&g.ilProbing;bool oldOrder=g.ilMarkActive;
 bool result=g.interleaveBegin();if(sampled){CHECK(last==previous);CHECK(last==oldOrder);++accepted;}previous=last;last=result;started=true;return result;}
};
int main(){
 {Driver d;for(int i=0;i<55;i++)d.step();CHECK(!d.g.ilProbing);CHECK(d.g.ilChoice);CHECK(d.g.ilWins==4);CHECK(d.accepted==32);for(int i=0;i<102;i++)d.step();CHECK(d.g.ilProbing);}
 {Driver d;for(int i=0;i<6;i++)d.step();d.g.ilChoice=true;++d.g.sceneGeneration;d.step();CHECK(d.g.ilChoice&&d.g.ilHaveMark&&d.g.ilWarmup==2&&d.g.ilAccepted==0&&d.g.ilPair==0);}
 {Driver d;d.g.ilChoice=true;d.step();d.step();auto&c=d.g.engine->renderer.core;c.generation+=4;d.step();CHECK(d.g.ilWarmup==2&&d.g.ilChoice);d.g.interleaveBegin();CHECK(d.g.ilWarmup==2&&d.g.ilAccepted==0);}
 {for(bool choice:{false,true}){Driver d;d.g.ilChoice=choice;d.step();int old=clockReads;for(int i=0;i<25;i++){d.g.engine->renderer.core.generation+=2;CHECK(d.g.interleaveBegin()==choice);CHECK(!d.g.ilHaveMark&&d.g.ilAccepted==0&&d.g.ilPair==0);}CHECK(clockReads==old);++d.g.engine->renderer.core.generation;d.g.interleaveBegin();CHECK(d.g.ilHaveMark&&d.g.ilWarmup==2);}}
 {Driver d;d.step();d.step();d.g.splitPassActive=true;int old=clockReads;CHECK(!d.g.interleaveBegin());CHECK(!d.g.ilHaveMark&&clockReads==old);d.g.splitPassActive=false;d.step();CHECK(d.g.ilWarmup==2);for(int mode:{0,2}){INTERLEAVE_PASSES=mode;old=clockReads;CHECK(d.g.interleaveBegin()==(mode==2));CHECK(!d.g.ilHaveMark&&clockReads==old);}INTERLEAVE_PASSES=1;}
 {Driver d;for(int i=0;i<5;i++)d.step();Tyra::Vif1Queue::effective=false;d.step();CHECK(d.g.ilWarmup==2&&d.g.ilAccepted==0);Tyra::Vif1Queue::effective=true;d.step();CHECK(d.g.ilWarmup==2);auto&c=d.g.engine->renderer.core;c.requested=false;d.step();CHECK(d.g.ilWarmup==2);c.settings.video=Tyra::VideoMode::NTSC;d.step();CHECK(d.g.ilWarmup==2);c.gs.buffers=3;d.step();CHECK(d.g.ilWarmup==2);c.settings.field=true;d.step();CHECK(d.g.ilWarmup==2);c.blss.enabled=true;d.step();CHECK(d.g.ilWarmup==2);c.limiter=false;d.step();CHECK(d.g.ilWarmup==2);}
 {Driver d;for(int i=0;i<5;i++)d.step();auto&c=d.g.engine->renderer.core;
 auto reset=[&](){d.step();CHECK(d.g.ilWarmup==2&&d.g.ilAccepted==0&&d.g.ilPair==0);};
 c.settings.display=Tyra::DisplayMode::HiDef1080i;reset();CHECK(c.settings.video==Tyra::VideoMode::Auto&&c.gs.buffers==2&&!c.settings.field);
 c.settings.display=Tyra::DisplayMode::Pal576i;reset();
 c.settings.color=Tyra::ColorDepth::Bits16;reset();c.settings.color=Tyra::ColorDepth::Hybrid;reset();
 c.settings.width=512;reset();c.settings.height=512;reset();c.settings.renderHeight=256;reset();
 c.settings.rasterWidth=256;reset();c.settings.rasterHeight=128;reset();
 c.blss.lowW=128;reset();c.blss.lowH=112;reset();c.blss.network=true;reset();
 c.settings.wide=true;reset();c.settings.dither=false;reset();
 c.gs.target.frameWidth=512;reset();c.gs.target.scissorX0=1;reset();c.gs.target.scissorX1=511;reset();
 c.gs.target.scissorY0=1;reset();c.gs.target.scissorY1=511;reset();
 d.step();int before=d.g.ilWarmup;c.gs.target.frameAddress+=1234;c.gs.target.offsetX16+=4;c.gs.target.offsetY16-=4;
 d.step();CHECK(d.g.ilWarmup==before-1); // bank rotation/jitter are not topology.
 d.g.ilChoice=true;c.settings.width=NAN;int old=clockReads;CHECK(d.g.interleaveBegin());CHECK(clockReads==old&&!d.g.ilHaveMark);
 }
 {Driver d;d.step();d.step();d.g.engine->renderer.core.yield=true;d.step();CHECK(d.g.ilWarmup==2&&d.g.ilAccepted==0);}
 {Driver d;d.g.ilChoice=false;d.step();int old=clockReads;Tyra::HardwareTrace::active=true;CHECK(!d.g.interleaveBegin());CHECK(clockReads==old&&!d.g.ilHaveMark);Tyra::HardwareTrace::active=false;CHECK(!d.g.interleaveBegin(true));CHECK(clockReads==old&&!d.g.ilHaveMark);d.step();CHECK(d.g.ilWarmup==2);}
 {PORTAL_COUNT=2;PORTALS={{0,0,1},{0,1,0}};Driver d;d.g.runtimeObjects.resize(2);d.g.portalLiveFlags={1,0};d.step();d.step();d.g.portalLiveFlags={0,1};d.step();CHECK(d.g.ilWarmup==2&&d.g.ilViews==2&&d.g.ilAccepted==0);PORTAL_COUNT=0;PORTALS.clear();}
 {Driver d;d.step();d.step();d.g.scriptCtx.cameraOverride=true;d.g.scriptCtx.cameraSource=12;d.step();CHECK(d.g.ilWarmup==2);d.step();d.g.scriptCtx.cameraSource=13;d.step();CHECK(d.g.ilWarmup==2);d.g.mockCameraRig=1;d.step();CHECK(d.g.ilWarmup==2);}
 {Driver d;d.g.ilChoice=false;d.g.scriptCtx.cameraOverride=true;d.g.scriptCtx.cameraSource=17;d.step();d.step();d.g.scriptCtx.cameraSource=0;int old=clockReads;for(int i=0;i<20;i++)CHECK(!d.g.interleaveBegin());CHECK(clockReads==old&&!d.g.ilHaveMark&&d.g.ilAccepted==0&&d.g.ilPair==0&&!d.g.ilChoice);d.g.ilChoice=true;CHECK(d.g.interleaveBegin());CHECK(clockReads==old);d.g.scriptCtx.cameraSource=18;d.step();CHECK(d.g.ilHaveMark&&d.g.ilWarmup==2&&d.g.ilPair==0);}
 {PORTAL_COUNT=1;PORTALS={{0,0,1}};Driver d;d.g.runtimeObjects.resize(2);d.g.portalLiveFlags={1};d.step();for(int i=0;i<5;i++)d.step();d.g.runtimeObjects[0].visible=false;d.step();CHECK(d.g.ilWarmup==2);d.step();d.g.portalLiveFlags[0]=0;d.step();CHECK(d.g.ilWarmup==2);PORTAL_COUNT=0;PORTALS.clear();}
 {Driver d;d.g.engine->renderer.core.generation=UINT32_MAX-1;ticks=UINT32_MAX-100;d.g.engine->renderer.core.stall=UINT32_MAX-50;d.step();d.step();CHECK(d.g.ilGeneration==0&&d.g.ilWarmup==1);}
 {Driver d;d.step();auto&c=d.g.engine->renderer.core;++c.generation;ticks+=10;c.stall+=20;d.g.interleaveBegin();CHECK(d.g.ilWarmup==2&&d.g.ilAccepted==0);++c.generation;ticks+=0x80000000u;d.g.interleaveBegin();CHECK(d.g.ilWarmup==2);}
 {TerrainGame g;g.ilWarmup=0;g.ilMarkActive=true;for(int i=0;i<4;i++)g.ilAccount(0x7ffffffeu);CHECK(g.ilPairOn==0x7ffffffeu);g.ilReset();CHECK(g.ilPairOn==0&&g.ilPairOff==0&&g.ilBlockSum==0&&g.ilSumOn==0&&g.ilSumOff==0);}
 {Driver d;for(int i=0;i<55;i++){d.step(d.started?(d.last?2000:1000):0);}CHECK(!d.g.ilChoice&&!d.g.ilProbing&&d.g.ilWins==0);}
 std::printf("PASS actual settled selector checks=%u accepted=32/48 blockIntervals zeroAddedWaits hostOnly\n",checks);
}
