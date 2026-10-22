#pragma once
#include <tyra>
#include <screenshot.h>
#include <cstdio>
#include <cmath>
#include <cstring>

unsigned paintRasterCull = 0, paintRasterClip = 0;

class PaintRasterGame : public Tyra::Game {
  Tyra::Engine* engine;
  Tyra::StaticPipeline pipe;
  Tyra::StaPipBag bag;
  Tyra::StaPipInfoBag info;
  Tyra::StaPipColorBag color;
  Tyra::StaPipTextureBag tex;
  Tyra::M4x4 model;
  alignas(16) Tyra::Vec4 vertices[6], normals[6];
  alignas(16) Tyra::Color colors[6];
  Tyra::Vec4 eye, look;
  unsigned stamp = 1, step = 0, frames = 0;

  void configure() {
    const unsigned group = (step / 3) % 9, shape = group / 3, basis = group % 3;
    const bool fog = step >= 27;
    info.fogDisabled = !fog;
    const float near = engine->renderer.core.getSettings().getNear();
    if(fog) engine->renderer.core.setFog(Tyra::Color(70,95,120),near,near*12.0F);
    else engine->renderer.core.disableFog();
    printf("PAINTFOG step=%u enabled=%u scale=%f offset=%f\n",step,unsigned(fog),
           engine->renderer.core.fog.scale,engine->renderer.core.fog.offset);
    tex.paintVu = step % 3 == 1;
    const float n = engine->renderer.core.getSettings().getNear();
    const float z = n * 8.0F;
    const float xy[6][2] = {{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};
    for (unsigned i = 0; i < 6; ++i) {
      float x = xy[i][0] * z * .22F, y = xy[i][1] * z * .22F;
      float depth = z;
      if (shape == 1 && xy[i][0] < 0) depth = n * .5F;
      if (shape == 2) x += z * .7F;
      vertices[i].set(x,y,depth,1);
      float nx = xy[i][0] * .5F, ny = xy[i][1] * .35F, nz = -.75F;
      float len = sqrtf(nx*nx + ny*ny + nz*nz);
      normals[i].set(nx/len,ny/len,nz/len,0);
    }
    float a = float(basis) * .55F;
    tex.paintForward.set(sinf(a),0,cosf(a),0);
    tex.paintHalf.set(.35F-tex.paintForward.x,.85F,.35F-tex.paintForward.z,0);
    float h = sqrtf(tex.paintHalf.x*tex.paintHalf.x + tex.paintHalf.y*tex.paintHalf.y + tex.paintHalf.z*tex.paintHalf.z);
    tex.paintHalf.x /= h; tex.paintHalf.y /= h; tex.paintHalf.z /= h;
    for (unsigned i = 0; i < 6; ++i) {
      const auto& v = normals[i];
      float d = v.x*tex.paintForward.x + v.y*tex.paintForward.y + v.z*tex.paintForward.z;
      float f = .30F + .70F*(1.0F-fabsf(d));
      float s = v.x*tex.paintHalf.x + v.y*tex.paintHalf.y + v.z*tex.paintHalf.z;
      if (s < 0) s = 0;
      s *= s; s *= s; s *= s;
      float alpha = 1.0F + 220.0F*s;
      if (alpha > 255) alpha = 255;
      colors[i].r = colors[i].g = colors[i].b = 128.0F*f;
      colors[i].a = alpha;
      // Wrong uploaded colours ensure a disabled candidate prepass cannot pass.
      if (tex.paintVu) { colors[i].r = 3; colors[i].g = 11; colors[i].b = 29; colors[i].a = 5; }
    }
    ++stamp; bag.bboxVersion = stamp;
    paintRasterCull = paintRasterClip = 0;
    FlushCache(0);
  }

  void capture() {
    auto& core = engine->renderer.core;
    core.synchronizeFrame();
    core.sync.align2D();
    framebuffer_t* fb = core.gs.getPreviousRealFrameBuffer();
    TYRA_ASSERT(fb && fb->width > 0 && fb->width <= 1024 && fb->psm == 0,
                "Paint raster requires completed CT32 framebuffer");
    char name[64]; snprintf(name,sizeof(name),"paint-raster-%02u.bin",step);
    FILE* out = fopen(Tyra::FileUtils::fromCwd(name).c_str(),"wb");
    TYRA_ASSERT(out,"Paint raster capture open failed");
    unsigned header[8] = {0x31545250,step,fb->width,fb->height,fb->psm,
                         unsigned(tex.paintVu),paintRasterCull,paintRasterClip};
    unsigned wrote = fwrite(header,1,sizeof(header),out), refused = 0;
    static unsigned line[1024] __attribute__((aligned(16)));
    for (unsigned y=0; y<fb->height; ++y) {
      if (!ps2_screenshot(line,fb->address/64,0,y,fb->width,1,fb->psm)) ++refused;
      FlushCache(0);
      wrote += fwrite(line,1,fb->width*4,out);
    }
    fclose(out);
    TYRA_ASSERT(!refused && wrote == sizeof(header)+fb->width*fb->height*4,
                "Paint raster readback incomplete");
    printf("PAINTRASTER step=%u mode=%u shape=%u basis=%u cull=%u clip=%u bytes=%u valid=1\n",
           step,unsigned(tex.paintVu),(step/9)%3,(step/3)%3,paintRasterCull,paintRasterClip,wrote);
  }

 public:
  explicit PaintRasterGame(Tyra::Engine* e) : engine(e), eye(0,0,0,1), look(0,0,1,1) {}
  void init() override {
    engine->renderer.setClearScreenColor(Tyra::Color(13,23,37));
    // Match TerrainGame's world-unit clip plane before the pipeline reads it.
    Tyra::PlanesClipAlgorithm::clipMargin =
        -(engine->renderer.core.getSettings().getNear() + .15F);
    pipe.setRenderer(&engine->renderer.core);
    pipe.core.setVU1Clipping(true);
    engine->renderer.renderer3D.usePipeline(pipe);
    model.identity();
    info.model = &model;
    info.fullClipChecks = true;
    info.frustumCulling = Tyra::PipelineInfoBagFrustumCulling_Precise;
    info.shadingType = Tyra::TyraShadingGouraud;
    info.additiveBlendFix = 96;
    info.dynamicLightReceive = false;
    info.fogDisabled = true;
    tex.texture = engine->renderer.getTextureRepository().add(Tyra::FileUtils::fromCwd("paint-raster.png"));
    tex.coordinates = normals; tex.contentVersion = &stamp;
    tex.coordinatesAreNormals = true; tex.textureFunction = 3;
    tex.envRight.set(1,0,0,0); tex.envUp.set(0,1,0,0);
    color.many = colors; color.single = nullptr; color.contentVersion = &stamp;
    bag.info = &info; bag.color = &color; bag.texture = &tex;
    bag.vertices = vertices; bag.count = 6; bag.contentVersion = &stamp;
    configure();
  }
  void loop() override {
    if (step >= 54) { SleepThread(); return; }
    Tyra::CameraInfo3D camera(&eye,&look);
    engine->renderer.beginFrame(camera);
    pipe.core.render(&bag);
    engine->renderer.endFrame();
    engine->renderer.core.synchronizeFrame();
    if (++frames == 4) {
      capture(); frames = 0; ++step;
      if (step < 54) configure();
      else printf("PAINTRASTERDONE cases=54 valid=1\n");
    }
  }
};
