#include <cstdio>
#include <cstring>
#include <kernel.h>
#include <timer.h>
#include <graph.h>
#include <draw.h>
#include <dma.h>
#include <packet2.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <gif_tags.h>
#include <screenshot.h>

using U = unsigned;
inline U tick() { U t; asm volatile("mfc0 %0,$9" : "=r"(t)); return t; }
inline volatile U& reg(U a) { return *reinterpret_cast<volatile U*>(a); }
inline volatile u64& csr() { return *reinterpret_cast<volatile u64*>(0x12001000); }
struct Settings {
  int getWidth() const { return 448; }
  float getRenderHeightF() const { return 448; }
  int getFrameBufferPsm() const { return GS_PSM_32; }
  bool isHybridDitherActive() const { return true; }
};
struct RendererCoreGS {
  struct RasterTarget { int frameAddress, frameWidth, scissorX0, scissorX1,
                           scissorY0, scissorY1, offsetX16, offsetY16; };
  Settings config;
  Settings* settings = &config;
  framebuffer_t frameBuffers[2]{{0,448,448,GS_PSM_32,0},
                                {200704,448,448,GS_PSM_16,0}};
  zbuffer_t zBuffer{DRAW_ENABLE,ZTEST_METHOD_GREATER_EQUAL,301056,GS_ZBUF_32,0};
  bool rasterRedirected = false;
  RasterTarget redirect{};
  int context = 0;
  float screenCenter = 2048;
  packet2_t* hybridPacket = nullptr;
  int getFieldYOffset16() const { return 0; }
  RasterTarget getRasterTarget() const;
  qword_t* emitRasterRestore(qword_t*,bool);
  void emitHybridPresent(u8);
  void frameWaitGif(int,int) {}
  void path3Fence() {}
  void frameSendPacket(packet2_t*,int,bool) {}
};
RendererCoreGS::RasterTarget RendererCoreGS::getRasterTarget() const {
  if (rasterRedirected) return redirect;

  RasterTarget t;
  const int w = static_cast<int>(settings->getWidth());
  // The PHYSICAL buffer height (half the logical one in InterlacedField).
  const int h = static_cast<int>(settings->getRenderHeightF());
  t.frameAddress = static_cast<int>(frameBuffers[context].address);
  t.frameWidth = static_cast<int>(frameBuffers[context].width);
  t.scissorX0 = 0;
  t.scissorX1 = w - 1;
  t.scissorY0 = 0;
  t.scissorY1 = h - 1;
  t.offsetX16 = static_cast<int>((screenCenter - w / 2.0F) * 16.0F);
  t.offsetY16 =
      static_cast<int>((screenCenter - h / 2.0F) * 16.0F) + getFieldYOffset16();
  return t;
}

qword_t* RendererCoreGS::emitRasterRestore(qword_t* q, bool texFlush) {
  const RasterTarget t = getRasterTarget();

  // NLOOP counts every register row below - an undercount stalls the GIF
  // forever (the stray qword parses as a new giftag with a garbage NLOOP).
  const int nloop = texFlush ? 4 : 3;
  PACK_GIFTAG(q, GIF_SET_TAG(nloop, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  if (texFlush) {
    // The bracket just rendered into VRAM the scene is about to sample as a
    // texture - drop any stale texels of it from the GS texture cache.
    PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
    q++;
  }
  // Modified by TyraX: the frame format, not a constant - both raster targets
  // this can restore (a display buffer, or BLSS' low-res target) are allocated
  // in it, and it is PSMCT16 in a 16bpp project (docs/gs-vram.md).
  PACK_GIFTAG(q,
              GS_SET_FRAME(t.frameAddress >> 11, t.frameWidth >> 6,
                           settings->getFrameBufferPsm(), 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_SCISSOR(t.scissorX0, t.scissorX1, t.scissorY0,
                             t.scissorY1),
              GS_REG_SCISSOR_1);
  q++;
  // Raw, not draw_primitive_xyoffset: the offset carries BLSS' sub-pixel
  // jitter and the per-field bias in exact 1/16 units.
  PACK_GIFTAG(q, GS_SET_XYOFFSET(t.offsetX16, t.offsetY16), GS_REG_XYOFFSET_1);
  q++;
  // The drawing environment's alpha/depth test.
  q = draw_enable_tests(q, 0, &zBuffer);
  // ZBUF written EXPLICITLY and LAST, not left to draw_enable_tests. Every
  // bracket that gets here pointed ZBUF at its OWN depth buffer (the env map's
  // 128x128, the shadow map's 64x64), so "restore" has to include putting the
  // scene z back - and this must not depend on what a ps2sdk helper happens to
  // pack. It cost a debugging session: the shadow-map bracket came back with
  // ZBUF still on the 64x64 silhouette buffer, so every projected-shadow
  // receiver patch failed GEQUAL against the caster's own light-space depth
  // and no shadow was drawn at all. The mask is bracket-scoped (0 only inside
  // the BLSS low-res bracket - see allocateVramBuffers).
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(static_cast<int>(zBuffer.address) >> 11,
                          static_cast<int>(zBuffer.zsm), zBuffer.mask),
              GS_REG_ZBUF_1);
  q++;
  return q;
}

void RendererCoreGS::emitHybridPresent(u8 target) {
  const int w = static_cast<int>(frameBuffers[0].width);
  const int h = static_cast<int>(frameBuffers[0].height);
  int tw = 0, th = 0;
  while ((1 << tw) < w) ++tw;
  while ((1 << th) < h) ++th;
  constexpr int kColumn = 32;
  const int columns = (w + kColumn - 1) / kColumn;

  packet2_reset(hybridPacket, false);
  qword_t* q = hybridPacket->next;
  // NLOOP counts every A+D row below: 11 setup rows (TEXFLUSH, TEX0, TEX1,
  // CLAMP, FRAME, ZBUF, TEST, SCISSOR, XYOFFSET, DTHE, PRIM) plus 4 per column.
  // An undercount stalls the GIF forever.
  PACK_GIFTAG(q, GIF_SET_TAG(11 + columns * 4, 0, 0, 0, GIF_FLG_PACKED, 1),
              GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_TEXFLUSH(0), GS_REG_TEXFLUSH);
  q++;
  PACK_GIFTAG(q,
              GS_SET_TEX0(static_cast<int>(frameBuffers[0].address) >> 6,
                          w >> 6, GS_PSM_32, tw, th, 1, 1 /* decal */, 0, 0, 0,
                          0, 0),
              GS_REG_TEX0_1);
  q++;
  PACK_GIFTAG(q, GS_SET_TEX1(1, 0, 0, 0, 0, 0, 0), GS_REG_TEX1_1);
  q++;
  PACK_GIFTAG(q, GS_SET_CLAMP(2, 2, 0, w - 1, 0, h - 1), GS_REG_CLAMP_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_FRAME(static_cast<int>(frameBuffers[target].address) >> 11,
                           w >> 6, GS_PSM_16, 0),
              GS_REG_FRAME_1);
  q++;
  PACK_GIFTAG(q,
              GS_SET_ZBUF(static_cast<int>(zBuffer.address) >> 11,
                          static_cast<int>(zBuffer.zsm), 1),
              GS_REG_ZBUF_1);
  q++;
  // No alpha test, z test ALWAYS (and ZBUF masked above): a plain copy.
  PACK_GIFTAG(q, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, 1), GS_REG_TEST_1);
  q++;
  PACK_GIFTAG(q, GS_SET_SCISSOR(0, w - 1, 0, h - 1), GS_REG_SCISSOR_1);
  q++;
  PACK_GIFTAG(q, GS_SET_XYOFFSET(2048 << 4, 2048 << 4), GS_REG_XYOFFSET_1);
  q++;
  PACK_GIFTAG(q, GS_SET_DTHE(settings->isHybridDitherActive() ? 1 : 0),
              GS_REG_DTHE);
  q++;
  PACK_GIFTAG(q, GS_SET_PRIM(6 /* sprite */, 0, 1, 0, 0, 0, 1 /* uv */, 0, 0),
              GS_REG_PRIM);
  q++;
  for (int c = 0; c < columns; ++c) {
    const int x0 = c * kColumn;
    const int x1 = x0 + kColumn < w ? x0 + kColumn : w;
    // 1:1 texels: UVs at texel centres (+0.5 texel) at both ends.
    PACK_GIFTAG(q, GS_SET_UV(x0 * 16 + 8, 8), GS_REG_UV);
    q++;
    PACK_GIFTAG(q, GS_SET_XYZ((2048 + x0) << 4, 2048 << 4, 0), GS_REG_XYZ2);
    q++;
    PACK_GIFTAG(q, GS_SET_UV(x1 * 16 + 8, h * 16 + 8), GS_REG_UV);
    q++;
    PACK_GIFTAG(q, GS_SET_XYZ((2048 + x1) << 4, (2048 + h) << 4, 0),
                GS_REG_XYZ2);
    q++;
  }
  // Back to the 32-bit draw buffer: FRAME, SCISSOR, XYOFFSET (with the
  // per-field bias), TEST and ZBUF through the one shared restore, then DTHE
  // off again. CLAMP is left for Path3::clearScreen, which re-asserts REPEAT
  // at the start of every frame before any 3D draws (the REPEAT contract).
  q = emitRasterRestore(q, true);
  // Modified by TyraX: close PATH3 here. Leaving EOP clear keeps GIF on
  // this path until another PATH3 packet arrives, starving a PATH1 FINISH
  // at a between-frame mode switch after double-buffer hybrid presentation.
  PACK_GIFTAG(q, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;
  PACK_GIFTAG(q, GS_SET_DTHE(0), GS_REG_DTHE);
  q++;
  packet2_update(hybridPacket, q);
  frameWaitGif(DMA_CHANNEL_GIF, 0);
  path3Fence();  // Modified by TyraX: path3_fence.hpp
  frameSendPacket(hybridPacket, DMA_CHANNEL_GIF, true);
}

bool waitComplete(U timeout = 294912000u) {
  U begin = tick();
  while ((reg(0x1000a000) & 0x100) || !(csr() & 2))
    if (tick() - begin > timeout) return false;
  csr() = 2;
  asm volatile("sync.l" ::: "memory");
  return true;
}
bool sendSetup(packet2_t* p) {
  packet2_update(p,draw_finish(p->next));
  csr() = 2;
  dma_channel_send_packet2(p,DMA_CHANNEL_GIF,true);
  return waitComplete();
}
struct Trial { U tickStart,tickDone,aStart,bStart,aDone,bDone,late; };
Trial trials[120]{};
int readback(const char* path,U address,U psm) {
  alignas(64) static unsigned char line[448*4];
  FILE* f=std::fopen(path,"wb");if(!f)return -1;
  U header[4]={448,448,psm,1};bool ok=std::fwrite(header,sizeof(header),1,f)==1;
  U stride=psm==GS_PSM_32?4:2;
  for(U y=0;y<448&&ok;++y) {
    ok=ps2_screenshot(line,address/64,0,y,448,1,psm)!=0;
    if(ok)ok=std::fwrite(line,stride,448,f)==448;
  }
  if(std::fclose(f)!=0)ok=false;
  return ok?0:-1;
}
int main() {
  std::printf("COPYBOOT version=1 mode=480p width=448 height=448\n");
  std::fflush(stdout);
  graph_set_mode(GRAPH_MODE_NONINTERLACED,GRAPH_MODE_HDTV_480P,GRAPH_MODE_FRAME,GRAPH_DISABLE);
  for(U i=0;i<8;++i)graph_wait_vsync();
  U m0=*T0_MODE,m1=*T1_MODE,c0=*T0_COUNT,c1=*T1_COUNT,p0=*T0_COMP,p1=*T1_COMP;
  U mask=reg(0x1000f010),pending=reg(0x1000f000);
  if(m0||m1||((mask|pending)&((1u<<9)|(1u<<10)))) {
    std::printf("COPYDONE reserved=0 reason=timer-owned\n");std::fflush(stdout);return 0;
  }
  dma_channel_initialize(DMA_CHANNEL_GIF,nullptr,0);
  RendererCoreGS renderer;
  auto* init=packet2_create(256,P2_TYPE_NORMAL,P2_MODE_NORMAL,0);
  renderer.hybridPacket=packet2_create(128,P2_TYPE_NORMAL,P2_MODE_NORMAL,0);
  if(!init||!renderer.hybridPacket) { std::printf("COPYDONE reserved=0 reason=allocation\n");return 0; }
  packet2_update(init,draw_setup_environment(init->next,0,&renderer.frameBuffers[0],&renderer.zBuffer));
  // Same nonnegative matrix as the actual renderer; outside the timed copy.
  constexpr int matrix[16]={0,2,0,2,3,1,3,1,0,2,0,2,3,1,3,1};
  u64 dimx=0;for(U i=0;i<16;++i)dimx|=u64(matrix[i])<<(i*4);
  PACK_GIFTAG(init->next,GIF_SET_TAG(1,0,0,0,GIF_FLG_PACKED,1),GIF_REG_AD);++init->next;
  PACK_GIFTAG(init->next,dimx,GS_REG_DIMX);++init->next;
  constexpr int colors[4][3]={{37,91,173},{199,55,83},{67,211,109},{233,181,29}};
  PACK_GIFTAG(init->next,GIF_SET_TAG(14,0,0,0,GIF_FLG_PACKED,1),GIF_REG_AD);++init->next;
  PACK_GIFTAG(init->next,GS_SET_PRIM(6,0,0,0,0,0,0,0,0),GS_REG_PRIM);++init->next;
  PACK_GIFTAG(init->next,GS_SET_TEST(0,0,0,0,0,0,1,1),GS_REG_TEST_1);++init->next;
  for(U i=0;i<4;++i) {
    U x=2048+(i%2)*224,y=2048+(i/2)*224;
    PACK_GIFTAG(init->next,GS_SET_RGBAQ(colors[i][0],colors[i][1],colors[i][2],128,0x3f800000),GS_REG_RGBAQ);++init->next;
    PACK_GIFTAG(init->next,GS_SET_XYZ(x<<4,y<<4,0),GS_REG_XYZ2);++init->next;
    PACK_GIFTAG(init->next,GS_SET_XYZ((x+224)<<4,(y+224)<<4,0),GS_REG_XYZ2);++init->next;
  }
  if(!sendSetup(init)) { std::printf("COPYDONE reserved=0 reason=setup-timeout\n");return 0; }
  int srcShot=readback("host:copy-source.bin",0,GS_PSM_32);
  renderer.emitHybridPresent(1); // Original methods build, stubbed dispatch does not send.
  U originalQw=packet2_get_qw_count(renderer.hybridPacket);
  packet2_update(renderer.hybridPacket,draw_finish(renderer.hybridPacket->next));
  U qw=packet2_get_qw_count(renderer.hybridPacket);
  FILE* dump=std::fopen("host:copy-packet.bin","wb");
  if(dump){std::fwrite(renderer.hybridPacket->base,16,qw,dump);std::fclose(dump);}
  SyncDCache(renderer.hybridPacket->base,renderer.hybridPacket->next);
  int intr=DIntr();*T0_COUNT=0;*T1_COUNT=0;*T0_MODE=0x9e;*T1_MODE=0xae;
  asm volatile("sync.l" ::: "memory");if(intr)EIntr();
  for(U i=0;i<8;++i)graph_wait_vsync();
  U completed=0,errors=0;
  for(U i=0;i<120;++i) {
    graph_wait_vsync();
    if((reg(0x10009000)&0x100)||(reg(0x1000a000)&0x100)) {++errors;break;}
    auto& t=trials[i];t.aStart=*T0_COUNT;t.bStart=*T1_COUNT;
    csr()=2;t.tickStart=tick();
    reg(0x1000a010)=reinterpret_cast<U>(renderer.hybridPacket->base);
    reg(0x1000a020)=qw;asm volatile("sync.l" ::: "memory");reg(0x1000a000)=0x101;
    if(!waitComplete()) {++errors;break;}
    t.tickDone=tick();t.aDone=*T0_COUNT;t.bDone=*T1_COUNT;
    t.late=t.aDone>=t.bDone||t.aDone<t.aStart;
    ++completed;
  }
  intr=DIntr();*T0_MODE=0;*T1_MODE=0;*T0_COUNT=c0;*T1_COUNT=c1;*T0_COMP=p0;*T1_COMP=p1;
  asm volatile("sync.l" ::: "memory");if(intr)EIntr();
  int dstShot=errors? -1:readback("host:copy-dest.bin",200704,GS_PSM_16);
  for(U i=0;i<completed;++i){auto& t=trials[i];std::printf("COPY index=%u begin=%u done=%u a0=%u b0=%u a1=%u b1=%u late=%u\n",i,t.tickStart,t.tickDone,t.aStart,t.bStart,t.aDone,t.bDone,t.late);}
  std::printf("COPYDONE reserved=1 completed=%u errors=%u originalQw=%u finishQw=%u srcShot=%d dstShot=%d restored=%u\n",completed,errors,originalQw,qw,srcShot,dstShot,unsigned(*T0_MODE==m0&&*T1_MODE==m1&&*T0_COUNT==c0&&*T1_COUNT==c1&&*T0_COMP==p0&&*T1_COMP==p1));
  std::fflush(stdout);SleepThread();return 0;
}
