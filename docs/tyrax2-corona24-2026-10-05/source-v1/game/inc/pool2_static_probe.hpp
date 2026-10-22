#pragma once
#include <cstdio>
#include <cstring>
#include "debug/night_ablation.hpp"
#include "renderer/core/gs/renderer_core_depth.hpp"
#include "renderer/3d/pipeline/static/core/stapip_corona_sprite.hpp"
extern "C" { volatile unsigned pool2ProbeReady=0; }
extern "C" __attribute__((noinline,noreturn)) void pool2ProbeHalt(){for(;;){__asm__ volatile("nop" ::: "memory");}}
namespace Pool2StaticProbe {
using namespace Tyra;
inline unsigned arm=0,caseId=0,repeats=0,completed=0,count=0;
inline bool initialized=false;
alignas(16) inline Vec4 positions[96],sts[96];
alignas(16) inline Color colors[96];
inline u32 generation=0;
inline M4x4 model=M4x4::Identity;
inline StaPipInfoBag info;inline StaPipColorBag color;inline StaPipTextureBag texture;inline StaPipBag bag;
inline ExperimentalCoronaSprite::TrianglePairs owner;
inline Vec4 eye(0,0,-20),at(0,0,0),up(0,1,0);
[[noreturn]] inline void reject(const char* why){printf("LOG: POOL2PROBE_REJECT reason=%s\n",why);fflush(stdout);pool2ProbeReady=0xbad00001u;pool2ProbeHalt();}
inline void init(Engine* engine,StaticPipeline& pipeline){
 FILE* f=fopen("host:pool2-probe.cfg","r");int a=-1,c=-1,r=-1;char trailing;
 if(!f)reject("missing_config");int got=fscanf(f,"%d %d %d %c",&a,&c,&r,&trailing);fclose(f);
 if(got!=3||a<0||a>1||c<1||c>4||r<1||r>3)reject("invalid_config");
 arm=a;caseId=c;repeats=r;count=caseId==4?96u:72u;
 if(RendererCoreDepth::bits!=24)reject("capability_depth_not_24");
 NightAblation::collectCounters=false;
 if(!NightAblation::setMask(0)||!NightAblation::setExtraMask(0)||!NightAblation::setWildVariant(0,false)||!NightAblation::setPoolTableEnabled(true))reject("gate_setup");
 NightAblation::poolTableSelected=true;NightAblation::coronaSpriteSelected=true;ExperimentalCoronaSprite::enabled=arm!=0;
 engine->renderer.core.setFramePipeline(true);
 engine->renderer.core.setFog(Color(23,47,71,128),19.0F,23.0F);
 // Symmetric active light: all four planar square corners have identical distance/cone factors.
 engine->renderer.core.setSpotLight(Color(4,8,12,128),Vec4(0,0,-1),Vec4(0,0,1),10.0F,80.0F,1.0F);
 engine->renderer.core.postFx.setBloom(0);engine->renderer.core.postFx.setGrain(0);engine->renderer.core.postFx.setMotionBlur(0);engine->renderer.core.postFx.setGodRays(0);engine->renderer.core.postFx.setDepthOfField(20,1,0);
 engine->renderer.setClearScreenColor(Color(0,0,0,128));pipeline.setRenderer(&engine->renderer.core);pipeline.core.setVU1Clipping(false);
 texture.texture=engine->renderer.getTextureRepository().add(FileUtils::fromCwd("hud/flare-corona.png"));if(!texture.texture)reject("texture_missing");
 texture.coordinates=sts;texture.coordinatesAreNormals=false;texture.contentVersion=&generation;
 static const unsigned corner[6]={0,1,2,0,2,3};
 static const float x[4]={0.5F,-0.5F,-0.5F,0.5F},y[4]={-0.5F,-0.5F,0.5F,0.5F};
 for(unsigned i=0;i<count;++i){unsigned k=corner[i%6];positions[i]=Vec4(x[k],y[k],caseId==2&&(k==1||k==2)?1.0F:0.0F,1);sts[i]=Vec4((k==1||k==2)?1.0F:0.0F,y[k]+0.5F,1,0);}
 info.model=&model;info.shadingType=TyraShadingGouraud;info.frustumCulling=PipelineInfoBagFrustumCulling_Precise;info.fullClipChecks=true;
 info.dynLightPick=false;info.spotLit=true;info.fogDisabled=false;info.dateLit=false;info.blssProxy=false;info.zTestType=PipelineZTest_Standard;
 info.blendingEnabled=true;info.additiveBlendFix=0;info.subtractiveBlendFix=0;
 color.single=nullptr;color.many=colors;color.contentVersion=&generation;
 bag.info=&info;bag.color=&color;bag.texture=&texture;bag.lighting=nullptr;bag.billboard=nullptr;bag.vertices=positions;bag.count=count;bag.packageSize=caseId==4?75u:72u;bag.stripped=false;
 bag.contentVersion=&generation;bag.bboxVersion=generation;bag.experimentalPoolTable=nullptr;
 owner={positions,sts,colors,count};bag.experimentalCoronaSource=&owner;
 initialized=true;
 printf("LOG: CORONAPROBE_CONFIG arm=%u case=%u vertices=%u repeats=%u package=%u fog=1 light=1 oracle=0 depth=24 capability=1\n",arm,caseId,count,repeats,unsigned(bag.packageSize));fflush(stdout);
}
inline void loop(Engine* engine,StaticPipeline& pipeline){
 if(!initialized||completed>=repeats)reject("invalid_loop");++generation;bag.bboxVersion=generation;
 for(unsigned i=0;i<count;++i)colors[i]=Color(31.0F+float(completed)+(caseId==3&&i%6==1?8.0F:0.0F),63.0F,95.0F,128.0F);
 NightAblation::collectCounters=false;const auto cc=NightAblation::coronaCounter;const auto pc=NightAblation::poolTableCounter;
 engine->renderer.beginFrame(CameraInfo3D(&eye,&at,&up));engine->renderer.renderer3D.usePipeline(pipeline);pipeline.core.render(&bag);engine->renderer.endFrame();engine->renderer.core.synchronizeFrame();
 if(!NightAblation::valid||NightAblation::collectCounters||memcmp(&cc,&NightAblation::coronaCounter,sizeof cc)||memcmp(&pc,&NightAblation::poolTableCounter,sizeof pc))reject("counter_or_gate_changed");
 ++completed;printf("LOG: CORONAPROBE_FRAME frame=%u arm=%u case=%u generation=%u counters=0\n",completed,arm,caseId,unsigned(generation));fflush(stdout);
 if(completed==repeats){printf("LOG: CORONAPROBE_READY arm=%u case=%u repeats=%u completed=%u\n",arm,caseId,repeats,completed);fflush(stdout);pool2ProbeReady=0x504f4f32u;pool2ProbeHalt();}
}
}
