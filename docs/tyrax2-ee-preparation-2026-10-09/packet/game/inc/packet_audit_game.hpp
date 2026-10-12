#pragma once
#include <tyra>
#include <screenshot.h>
#include <cstdio>
#include "debug/packet_audit.hpp"

namespace PacketAudit {
bool legacy = false;
unsigned checks = 0, errors = 0, culls = 0, clips = 0, replays = 0;
void emitted(const qword_t* data, unsigned count, bool linear) {
  unsigned found = 0;
  for (unsigned i = 0; i < count; ++i) {
    if (data[i].dw[1] != GS_REG_TEX1) continue;
    ++found; ++checks;
    const unsigned mag = (data[i].dw[0] >> 5) & 1;
    const unsigned min = (data[i].dw[0] >> 6) & 7;
    if (mag != unsigned(linear) || min != unsigned(linear)) ++errors;
  }
  TYRA_ASSERT(found == 1, "Expected exactly one emitted TEX1");
}
}

class PacketAuditGame : public Tyra::Game {
  Tyra::Engine* engine;
  Tyra::StaticPipeline pipe;
  Tyra::StaPipBag bag;
  Tyra::StaPipInfoBag info;
  Tyra::StaPipColorBag color;
  Tyra::StaPipTextureBag tex;
  Tyra::M4x4 model;
  alignas(16) Tyra::Vec4 vertices[3][6], uv[6];
  Tyra::Color tint = Tyra::Color(128,128,128,128);
  Tyra::Vec4 eye = Tyra::Vec4(0,0,0,1), look = Tyra::Vec4(0,0,1,1);
  unsigned step = 0, frame = 0;
  FILE* report = nullptr;

  void capture() {
    auto& core = engine->renderer.core;
    core.synchronizeFrame(); core.sync.align2D();
    auto* fb = core.gs.getPreviousRealFrameBuffer();
    TYRA_ASSERT(fb && fb->psm == 0 && fb->width <= 1024, "Expected CT32 readback");
    char name[64]; snprintf(name,sizeof(name),"packet-raster-%02u.bin",step);
    FILE* out = fopen(Tyra::FileUtils::fromCwd(name).c_str(),"wb");
    TYRA_ASSERT(out, "Cannot open packet raster");
    unsigned header[4] = {0x31544150,step,fb->width,fb->height};
    TYRA_ASSERT(fwrite(header,sizeof(header),1,out)==1,"Raster header failed");
    static unsigned line[1024] __attribute__((aligned(16)));
    for (unsigned y=0;y<fb->height;++y) {
      TYRA_ASSERT(ps2_screenshot(line,fb->address/64,0,y,fb->width,1,fb->psm),"Readback failed");
      FlushCache(0);
      TYRA_ASSERT(fwrite(line,4,fb->width,out)==fb->width,"Raster write failed");
    }
    fclose(out);
  }
 public:
  explicit PacketAuditGame(Tyra::Engine* e):engine(e) {}
  void init() override {
    engine->renderer.setClearScreenColor(Tyra::Color(13,23,37));
    const float n=engine->renderer.core.getSettings().getNear(), z=n*8;
    Tyra::PlanesClipAlgorithm::clipMargin=-(n+.15F);
    pipe.setRenderer(&engine->renderer.core); pipe.core.setVU1Clipping(true);
    engine->renderer.renderer3D.usePipeline(pipe);
    model.identity(); info.model=&model; info.fullClipChecks=true;
    info.frustumCulling=Tyra::PipelineInfoBagFrustumCulling_Precise;
    info.dynamicLightReceive=false; info.fogDisabled=true;
    const float xy[6][2]={{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};
    for(unsigned s=0;s<3;++s) for(unsigned i=0;i<6;++i) {
      vertices[s][i].set(xy[i][0]*z*.22F+(s==2?z*.7F:0),xy[i][1]*z*.22F,
                         s==1&&xy[i][0]<0?n*.5F:z,1);
      uv[i].set((xy[i][0]+1)*.5F,(xy[i][1]+1)*.5F,1,0);
    }
    tex.texture=engine->renderer.getTextureRepository().add(Tyra::FileUtils::fromCwd("packet-audit.png"));
    tex.coordinates=uv; color.single=&tint;
    bag.info=&info; bag.color=&color; bag.texture=&tex; bag.count=6;
    report=fopen(Tyra::FileUtils::fromCwd("packet-audit.csv").c_str(),"w");
    TYRA_ASSERT(report,"Cannot open packet report");
    fprintf(report,"step,legacy,shape,batch,checks,errors,culls,clips,replays\n");
  }
  void loop() override {
    if(step>=18) { SleepThread(); return; }
    const unsigned shape=step%3, batch=(step/3)%2;
    PacketAudit::legacy=step/6==1; bag.vertices=vertices[shape];
    Tyra::CameraInfo3D camera(&eye,&look); engine->renderer.beginFrame(camera);
    if(batch) pipe.core.beginSubmissionBatch();
    const bool modes[6]={false,false,true,true,false,true};
    for(bool linear:modes) {
      info.textureMappingType=linear?Tyra::TyraLinear:Tyra::TyraNearest;
      pipe.core.render(&bag);
    }
    if(batch) pipe.core.endSubmissionBatch();
    engine->renderer.endFrame(); engine->renderer.core.synchronizeFrame();
    if(++frame==4) {
      capture();
      fprintf(report,"%u,%u,%u,%u,%u,%u,%u,%u,%u\n",step,unsigned(PacketAudit::legacy),shape,batch,
              PacketAudit::checks,PacketAudit::errors,PacketAudit::culls,PacketAudit::clips,PacketAudit::replays);
      fflush(report);
      TYRA_ASSERT(PacketAudit::checks==24,"Missing emitted packet checks");
      TYRA_ASSERT(PacketAudit::legacy?PacketAudit::errors>0:PacketAudit::errors==0,"Filter oracle failed");
      if(shape==1) TYRA_ASSERT(PacketAudit::clips>0,"Near case did not clip");
      if(shape==0) TYRA_ASSERT(PacketAudit::replays>0,"Direct case did not replay");
      PacketAudit::checks=PacketAudit::errors=PacketAudit::culls=PacketAudit::clips=PacketAudit::replays=0;
      frame=0; ++step;
      if(step==18) { fclose(report); printf("PACKETAUDITDONE cases=18 valid=1\n"); }
    }
  }
};
