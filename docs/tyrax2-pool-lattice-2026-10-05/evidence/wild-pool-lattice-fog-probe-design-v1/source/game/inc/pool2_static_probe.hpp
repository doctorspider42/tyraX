#pragma once
#ifndef POOL2_EE_PROBE_CLIP
#define POOL2_EE_PROBE_CLIP 0
#endif
static_assert(POOL2_EE_PROBE_CLIP==0,"lattice probe inside-only");
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
alignas(16) inline Color colors[192]; // preexpanded baseline for the current color epoch
alignas(16) inline Color lazyColors[192]; // owned persistent candidate backing
alignas(16) inline uint32_t ready[192];
alignas(16) inline Color previousLazyColors[192];
alignas(16) inline uint32_t previousReady[192];
inline u32 colorGeneration=0;
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
     !NightAblation::setWildVariant(0,false) || !NightAblation::setPoolTableEnabled(true))
    reject("gate_setup");
  NightAblation::poolTableSelected=true;
  NightAblation::poolLatticeSelected=true;
  NightAblation::poolLatticeEnabled=arm!=0;
  engine->renderer.core.setFramePipeline(true);
  engine->renderer.core.setFog(Color(23.0F,47.0F,71.0F,128.0F),19.0F,23.0F);
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
  // Exact original4x4-cell patch: reused source positions AND ST have equal bits.
  static const unsigned delta[6]={0,1,6,0,6,5};
  for(unsigned i=0;i<caseId;++i) {
    const unsigned member=i/96,occurrence=i%96,cell=occurrence/6;
    const unsigned grid=(cell/4)*5+cell%4+delta[occurrence%6];
    const unsigned ix=grid%5,iz=grid/5;
    // Binary fractions and finite varyingz exercise distinct perspective Q.
    positions[i]=Vec4(-4.0F+float(member)*4.0F+float(ix),
                     -2.0F+float(iz),
                     float(ix*ix+iz)*0.0625F+float(member)*0.125F,1.0F);
    sts[i]=Vec4(float(ix)*0.25F,float(iz)*0.25F,1.0F,0.0F);
    memcpy(&colors[i],&memberColors[member],16);
  }
  memset(static_cast<void*>(lazyColors),0xa5,sizeof lazyColors);
  memset(ready,0,sizeof ready);
  view.colors=memberColors;view.members=caseId/96;view.total=caseId;view.generation=colorGeneration;
  view.expanded=static_cast<void*>(lazyColors);
  view.ready=ready;
  info.model=&model;info.shadingType=TyraShadingGouraud;
  info.frustumCulling=PipelineInfoBagFrustumCulling_Precise;info.fullClipChecks=true;
  info.dynLightPick=false;info.spotLit=false;info.fogDisabled=false;
  info.dateLit=false;info.blssProxy=false;info.zTestType=PipelineZTest_Standard;
  info.blendingEnabled=true;info.additiveBlendFix=0;info.subtractiveBlendFix=0;
  color.single=nullptr;color.many=lazyColors;color.contentVersion=&colorGeneration;
  bag.info=&info;bag.color=&color;bag.texture=&texture;bag.lighting=nullptr;bag.billboard=nullptr;
  bag.vertices=positions;bag.count=caseId;bag.packageSize=75;bag.stripped=false;
  bag.contentVersion=&generation;bag.bboxVersion=generation;bag.experimentalPoolTable=&view;
  initialized=true;
  printf("LOG: POOL2PROBE_CONFIG arm=%u vertices=%u members=%u repeats=%u package=75 alpha=128\n",
         unsigned(arm),unsigned(caseId),unsigned(view.members),unsigned(repeats));fflush(stdout);
  printf("LOG: POOL2EEPROBE_CONFIG kind=8 clip=0 lazy=1 table=1 lattice=%u fog=1 fogStart=19 fogEnd=23 oracle=0\n",
         unsigned(arm));fflush(stdout);
}
inline void loop(Engine* engine,StaticPipeline& pipeline) {
  if(!initialized || completed>=repeats)reject("invalid_loop");
  const bool cold=completed==0;
  // Both arms author the same explicit color epoch before every render.
  memcpy(previousLazyColors,lazyColors,sizeof lazyColors);
  memcpy(previousReady,ready,sizeof ready);
  ++colorGeneration;view.generation=colorGeneration;
  const Color epoch0(31.0F+float(completed),63.0F,95.0F,128.0F);
  const Color epoch1(95.0F-float(completed),15.0F,1.0F,128.0F);
  memcpy(&memberColors[0],&epoch0,16);memcpy(&memberColors[1],&epoch1,16);
  for(unsigned i=0;i<caseId;++i)memcpy(&colors[i],&memberColors[i/96],16);
  // Oracle collection itself materializes eligible colors: keep it disabled.
  NightAblation::collectCounters=false;
  const auto saved=NightAblation::poolTableCounter;
  const auto savedLattice=NightAblation::poolLatticeCounter;
  engine->renderer.beginFrame(CameraInfo3D(&eye,&at,&up));
  engine->renderer.renderer3D.usePipeline(pipeline);
  pipeline.core.render(&bag);
  engine->renderer.endFrame();
  engine->renderer.core.synchronizeFrame();
  NightAblation::collectCounters=false;
  if(!NightAblation::valid)reject("gate_invalid");
  const auto& q=NightAblation::poolTableCounter;
  if(memcmp(&saved,&q,sizeof q)!=0 ||
     memcmp(&savedLattice,&NightAblation::poolLatticeCounter,sizeof savedLattice)!=0)
    reject("disabled_counter_write");
  unsigned readyCount=0;
  for(unsigned i=0;i<caseId;++i) {
    if(ready[i]==colorGeneration) {
      ++readyCount;
      if(!arm || memcmp(&lazyColors[i],&colors[i],16)!=0)reject("fallback_color_bytes");
    } else {
      if(ready[i]>=colorGeneration || ready[i]!=previousReady[i])reject("invalid_ready_generation");
      if(memcmp(&lazyColors[i],&previousLazyColors[i],16)!=0)reject("unready_source_write");
    }
  }
  if(readyCount!=0)reject("inside_case_materialized_source");
  printf("LOG: POOL2EEPROBE_SOURCE clip=%u ready=%u total=%u generation=%u coldOracle=0\n",
         unsigned(POOL2_EE_PROBE_CLIP),readyCount,unsigned(caseId),unsigned(colorGeneration));fflush(stdout);
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
