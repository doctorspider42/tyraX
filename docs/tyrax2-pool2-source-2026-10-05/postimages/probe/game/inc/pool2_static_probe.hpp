#pragma once
#include <cstdio>
#include <cstring>
#include "debug/night_ablation.hpp"
#include "renderer/3d/pipeline/static/core/stapip_pool_color_table.hpp"

// Correctness ELF only. The debugger must stop inside this function after READY.
extern "C" { volatile unsigned pool2ProbeReady = 0; }
extern "C" __attribute__((noinline,noreturn)) void pool2ProbeHalt() {
  for (;;) { __asm__ volatile("nop" ::: "memory"); }
}
namespace Pool2StaticProbe {
using namespace Tyra;
inline unsigned arm=0,caseId=0,repeats=0,completed=0;
inline bool initialized=false;
alignas(16) inline Vec4 positions[192],sts[192];
alignas(16) inline Color colors[192];
alignas(16) inline ExperimentalPoolTable::ColorBits memberColors[2];
inline ExperimentalPoolTable::View view;
inline u32 generation=1;
inline M4x4 model=M4x4::Identity;
inline StaPipInfoBag info;
inline StaPipColorBag color;
inline StaPipTextureBag texture;
inline StaPipBag bag;
inline Vec4 eye(0.0F,0.0F,-20.0F),at(0.0F,0.0F,0.0F),up(0.0F,1.0F,0.0F);

[[noreturn]] inline void reject(const char* why) {
  printf("LOG: POOL2PROBE_REJECT reason=%s\n",why);fflush(stdout);
  pool2ProbeReady=0xbad00001u;pool2ProbeHalt();
}
inline void init(Engine* engine,StaticPipeline& pipeline) {
  // Strict arm case repeat: arm0/1, case96/192, repeat1..3; trailing data rejected.
  FILE* f=fopen("host:pool2-probe.cfg","r");int a=-1,c=-1,r=-1;char trailing;
  if(!f)reject("missing_config");
  const int got=fscanf(f,"%d %d %d %c",&a,&c,&r,&trailing);fclose(f);
  if(got!=3 || a<0 || a>1 || (c!=96&&c!=192) || r<1 || r>3)reject("invalid_config");
  arm=unsigned(a);caseId=unsigned(c);repeats=unsigned(r);
  NightAblation::collectCounters=false;
  if(!NightAblation::setMask(0) || !NightAblation::setExtraMask(0) ||
     !NightAblation::setWildVariant(0,false) || !NightAblation::setPoolTableEnabled(arm!=0))
    reject("gate_setup");
  NightAblation::poolTableSelected=true;
  engine->renderer.core.setFramePipeline(true);
  engine->renderer.core.disableFog();
  engine->renderer.core.postFx.setBloom(0);
  engine->renderer.core.postFx.setGrain(0);
  engine->renderer.core.postFx.setMotionBlur(0);
  engine->renderer.core.postFx.setGodRays(0);
  engine->renderer.core.postFx.setDepthOfField(20.0F,1.0F,0);
  engine->renderer.setClearScreenColor(Color(0.0F,0.0F,0.0F,128.0F));
  pipeline.setRenderer(&engine->renderer.core);
  pipeline.core.setVU1Clipping(false);
  texture.texture=engine->renderer.getTextureRepository().add(
      FileUtils::fromCwd("hud/flare-corona.png"));
  if(!texture.texture)reject("texture_missing");
  texture.coordinates=sts;texture.coordinatesAreNormals=false;texture.contentVersion=&generation;
  const Color c0(31.0F,63.0F,95.0F,128.0F),c1(95.0F,15.0F,1.0F,128.0F);
  static_assert(sizeof(Color)==16,"exact source color qword");
  memcpy(&memberColors[0],&c0,16);memcpy(&memberColors[1],&c1,16);
  for(unsigned i=0;i<caseId;i+=3) {
    const unsigned tri=i/3;const float x=-4.0F+float(tri%8),y=-4.0F+float(tri/8);
    positions[i]=Vec4(x,y,0.0F,1.0F);
    positions[i+1]=Vec4(x+0.75F,y,0.0F,1.0F);
    positions[i+2]=Vec4(x,y+0.75F,0.0F,1.0F);
    sts[i]=Vec4(0.0F,0.0F,1.0F,0.0F);
    sts[i+1]=Vec4(1.0F,0.0F,1.0F,0.0F);
    sts[i+2]=Vec4(0.0F,1.0F,1.0F,0.0F);
    for(unsigned j=0;j<3;++j)memcpy(&colors[i+j],&memberColors[(i+j)/96],16);
  }
  view.colors=memberColors;view.members=caseId/96;view.total=caseId;view.generation=generation;
  info.model=&model;info.shadingType=TyraShadingGouraud;
  info.frustumCulling=PipelineInfoBagFrustumCulling_Precise;info.fullClipChecks=true;
  info.dynLightPick=false;info.spotLit=false;info.fogDisabled=true;
  info.dateLit=false;info.blssProxy=false;info.zTestType=PipelineZTest_Standard;
  info.blendingEnabled=true;info.additiveBlendFix=0;info.subtractiveBlendFix=0;
  color.single=nullptr;color.many=colors;color.contentVersion=&generation;
  bag.info=&info;bag.color=&color;bag.texture=&texture;bag.lighting=nullptr;bag.billboard=nullptr;
  bag.vertices=positions;bag.count=caseId;bag.packageSize=75;bag.stripped=false;
  bag.contentVersion=&generation;bag.bboxVersion=generation;bag.experimentalPoolTable=&view;
  initialized=true;
  printf("LOG: POOL2PROBE_CONFIG arm=%u vertices=%u members=%u repeats=%u package=75 alpha=128\n",
         unsigned(arm),unsigned(caseId),unsigned(view.members),unsigned(repeats));fflush(stdout);
}
inline void loop(Engine* engine,StaticPipeline& pipeline) {
  if(!initialized || completed>=repeats)reject("invalid_loop");
  const bool cold=completed==0;
  NightAblation::collectCounters=cold;
  if(cold)NightAblation::poolTableCounter=NightAblation::PoolTableCounter{};
  const auto saved=NightAblation::poolTableCounter;
  engine->renderer.beginFrame(CameraInfo3D(&eye,&at,&up));
  engine->renderer.renderer3D.usePipeline(pipeline);
  pipeline.core.render(&bag);
  engine->renderer.endFrame();
  engine->renderer.core.synchronizeFrame();
  NightAblation::collectCounters=false;
  if(!NightAblation::valid)reject("gate_invalid");
  const auto& q=NightAblation::poolTableCounter;
  const unsigned packages=(caseId+74)/75;
  if(cold && (q.invocations!=packages || q.eligible!=packages || q.fallback!=0 ||
     q.applied!=(arm?packages:0) || q.sourceVertices!=caseId ||
     q.coldCompared!=packages || q.coldMismatches!=0 || q.invalid!=0))reject("cold_route_or_oracle");
  if(!cold && memcmp(&saved,&q,sizeof q)!=0)reject("warm_counter_write");
  ++completed;
  printf("LOG: POOL2PROBE_FRAME frame=%u cold=%u arm=%u eligible=%u applied=%u compared=%u mismatches=%u\n",
         unsigned(completed),unsigned(cold?1:0),unsigned(arm),unsigned(q.eligible),unsigned(q.applied),
         unsigned(q.coldCompared),unsigned(q.coldMismatches));fflush(stdout);
  if(completed==repeats) {
    printf("LOG: POOL2PROBE_READY arm=%u vertices=%u repeats=%u completed=%u warmBypass=0\n",
           unsigned(arm),unsigned(caseId),unsigned(repeats),unsigned(completed));fflush(stdout);
    pool2ProbeReady=0x504f4f32u;pool2ProbeHalt();
  }
}
}
