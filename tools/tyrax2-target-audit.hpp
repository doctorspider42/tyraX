#pragma once
#include <tyra>
#include <screenshot.h>
#include <cstdio>
#include <cstring>
#include "debug/target_audit.hpp"

class TargetAuditGame : public Tyra::Game {
  Tyra::Engine* engine;
  Tyra::StaticPipeline pipe;
  Tyra::StaPipBag bag;
  Tyra::StaPipInfoBag info;
  Tyra::StaPipColorBag color;
  Tyra::M4x4 model;
  alignas(16) Tyra::Vec4 vertices[10][6];
  Tyra::Color tint=Tyra::Color(90,170,210,128);
  Tyra::Vec4 eye=Tyra::Vec4(0,0,0,1),look=Tyra::Vec4(0,0,1,1);
  unsigned step=0,frames=0,version=1;
  FILE* report=nullptr;

  void configure() {
    const float n=engine->renderer.core.getSettings().getNear(), z=n*8;
    const unsigned scene=step/3;
    const float xy[6][2]={{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};
    const float centers[10][2]={{0,0},{2,0},{-2,0},{0,2},{0,-2},
                               {1.428148F,0},{-1.428148F,0},{0,1.428148F},{0,-1.428148F},{0,0}};
    for(unsigned b=0;b<10;++b) for(unsigned i=0;i<6;++i) {
      float depth=z, x=centers[b][0]*z+xy[i][0]*z*.13F;
      float y=centers[b][1]*z+xy[i][1]*z*.13F;
      if(scene==1 && b==0 && xy[i][0]<0) depth=n*.5F;
      if(scene==2 && b==9) depth=-z;
      if(scene==3 && b==9) depth=engine->renderer.core.getSettings().getFar()*1.1F;
      if(scene==4 && b==9) {x=xy[i][0]*z*3;y=xy[i][1]*z*3;}
      vertices[b][i].set(x,y,depth,1);
    }
    model.identity();
    if(scene==5) {model.rotateZ(.33F);model.translateX(z*.11F);}
    ++version;bag.bboxVersion=version;
    TargetAudit::tested=TargetAudit::rejected=0;
    FlushCache(0);
  }
  void capture(bool env) {
    auto& core=engine->renderer.core;
    core.synchronizeFrame();core.sync.align2D();
    unsigned address,width,height,psm;
    if(env) {
      auto* t=core.envMap.getTexture()->vramResident;
      address=t->address;width=height=128;psm=t->psm;
    } else {
      auto* fb=core.gs.getPreviousRealFrameBuffer();
      address=fb->address;width=fb->width;height=fb->height;psm=fb->psm;
    }
    TYRA_ASSERT(psm==0 && width<=1024,"Expected CT32 raster");
    char name[64];snprintf(name,sizeof(name),"target-%s-%02u.bin",env?"env":"main",step);
    FILE* out=fopen(Tyra::FileUtils::fromCwd(name).c_str(),"wb");
    TYRA_ASSERT(out,"Target output open failed");
    unsigned header[4]={0x31544754,step,width,height};
    TYRA_ASSERT(fwrite(header,sizeof(header),1,out)==1,"Header write failed");
    static unsigned line[1024] __attribute__((aligned(16)));
    for(unsigned y=0;y<height;++y) {
      TYRA_ASSERT(ps2_screenshot(line,address/64,0,y,width,1,psm),"Target readback failed");
      FlushCache(0);
      TYRA_ASSERT(fwrite(line,4,width,out)==width,"Target write failed");
    }
    fclose(out);
  }
 public:
  explicit TargetAuditGame(Tyra::Engine* e):engine(e){}
  void init() override {
    engine->renderer.setClearScreenColor(Tyra::Color(13,23,37));
    Tyra::PlanesClipAlgorithm::clipMargin=-(engine->renderer.core.getSettings().getNear()+.15F);
    pipe.setRenderer(&engine->renderer.core);pipe.core.setVU1Clipping(true);
    engine->renderer.renderer3D.usePipeline(pipe);
    info.model=&model;info.fullClipChecks=true;info.dynamicLightReceive=false;info.fogDisabled=true;
    info.frustumCulling=Tyra::PipelineInfoBagFrustumCulling_Precise;
    color.single=&tint;bag.info=&info;bag.color=&color;bag.count=6;
    report=fopen(Tyra::FileUtils::fromCwd("target-audit.csv").c_str(),"w");
    TYRA_ASSERT(report,"Cannot open target report");
    fprintf(report,"step,scene,enabled,tested,rejected\n");configure();
  }
  void loop() override {
    if(step>=18){SleepThread();return;}
    TargetAudit::enabled=step%3==1;
    auto& core=engine->renderer.core;
    Tyra::CameraInfo3D camera(&eye,&look);
    engine->renderer.beginFrame(camera);
    const auto saved=core.renderer3D.getViewProj();
    core.envMap.begin(Tyra::Color(17,29,43));
    Tyra::Vec4 envLook=look;
    if(step/3==5) envLook.set(.18F,.05F,1,1);
    core.renderer3D.pushEnvView(eye,envLook,110,128);
    pipe.core.beginSubmissionBatch();
    for(unsigned b=0;b<10;++b) {bag.vertices=vertices[b];pipe.core.render(&bag);}
    pipe.core.endSubmissionBatch();
    core.renderer3D.popEnvView(camera);core.envMap.end();
    TYRA_ASSERT(!TargetAudit::size && !memcmp(saved.data,core.renderer3D.getViewProj().data,sizeof(saved.data)),"View restore mismatch");
    bag.vertices=vertices[0];pipe.core.render(&bag);
    engine->renderer.endFrame();core.synchronizeFrame();
    if(++frames==4) {
      capture(true);capture(false);
      fprintf(report,"%u,%u,%u,%u,%u\n",step,step/3,unsigned(TargetAudit::enabled),TargetAudit::tested,TargetAudit::rejected);
      fflush(report);
      TYRA_ASSERT(TargetAudit::enabled?TargetAudit::rejected>0:TargetAudit::rejected==0,"Target arm coverage failed");
      frames=0;++step;
      if(step<18) configure();
      else {fclose(report);printf("TARGETAUDITDONE cases=18 valid=1\n");}
    }
  }
};
