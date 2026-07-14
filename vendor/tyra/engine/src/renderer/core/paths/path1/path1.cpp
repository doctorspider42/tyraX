/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: setDoubleBuffer() records that VU1 is configured.
*/

#include "renderer/core/paths/path1/path1.hpp"
#include "debug/debug.hpp"

extern u32 VU1DrawFinish_CodeStart __attribute__((section(".vudata")));
extern u32 VU1DrawFinish_CodeEnd __attribute__((section(".vudata")));

namespace Tyra {

// VU1 micro memory holds 2048 instructions (16 KB / 8 bytes).
static constexpr u32 VU1_MICRO_MEM_SIZE = 2048;

Path1::Path1() {
  vu1Configured = false;
  doubleBufferPacket = packet2_create(2, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  drawFinishPacket = packet2_create(10, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  uploadDrawFinishProgram();
  prepareDrawFinishPacket();
}

void Path1::uploadDrawFinishProgram() {
  int count = (&VU1DrawFinish_CodeEnd - &VU1DrawFinish_CodeStart) / 2;
  if (count & 1) count++;

  // Modified by TyraX: park the draw-finish helper at the very top of
  // VU1 micro memory (2048 instructions) instead of the old 1000 - count.
  // The pipeline caches upload their programs from address 0 upward, and the
  // GS-hardware-fog VU1 programs pushed the static pipeline's eight programs
  // to ~1064 instructions total - straight through 1000 - count, overwriting
  // this helper. A clobbered draw-finish program never emits its GS FINISH,
  // so RendererCoreSync::align3D() (the post-fx PATH1 barrier) spin-waited
  // forever and any bloom/grain/grading scene froze on the Tyra splash. The
  // top of micro memory keeps it clear of the pipeline programs; the assert in
  // createProgramsCache() catches a future overflow loudly instead.
  drawFinishAddr = VU1_MICRO_MEM_SIZE - count;

  packet2_t* packet2 = packet2_create(10, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);
  packet2_vif_add_micro_program(packet2, drawFinishAddr,
                                &VU1DrawFinish_CodeStart,
                                &VU1DrawFinish_CodeEnd);
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_send_packet2(packet2, DMA_CHANNEL_VIF1, true);
  packet2_free(packet2);
}

void Path1::addDrawFinishTag(packet2_t* packet) {
  prim_t prim;
  prim.type = PRIM_TRIANGLE;
  prim.shading = PRIM_SHADE_GOURAUD;
  prim.mapping = 1;
  prim.fogging = 0;
  prim.blending = 1;
  prim.antialiasing = 0;
  prim.mapping_type = PRIM_MAP_ST;
  prim.colorfix = PRIM_UNFIXED;

  packet2_utils_vu_open_unpack(packet, 10, true);
  {
    packet2_utils_gif_add_set(packet, 1);
    packet2_utils_gs_add_draw_finish_giftag(packet);
    packet2_utils_gs_add_prim_giftag(packet, &prim, 0,
                                     ((u64)GIF_REG_RGBAQ) << 0, 1, 0);
  }
  packet2_utils_vu_close_unpack(packet);

  packet2_utils_vu_add_start_program(packet, drawFinishAddr);
}

void Path1::sendDrawFinishTag() {
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_send_packet2(drawFinishPacket, DMA_CHANNEL_VIF1, true);
}

Path1::~Path1() {
  packet2_free(doubleBufferPacket);
  packet2_free(drawFinishPacket);
}

void Path1::prepareDrawFinishPacket() {
  addDrawFinishTag(drawFinishPacket);
  packet2_utils_vu_add_end_tag(drawFinishPacket);
}

u32 Path1::uploadProgram(VU1Program* program, const u32& address) {
  // TYRA_LOG("Uploading VU1 program. Size: ", program->getProgramSize(),
  //          ", name: ", program->getStringName(), ", address:", address);

  auto packetSize = program->getPacketSize() + 1;  // + end tag

  packet2_t* packet2 =
      packet2_create(packetSize, P2_TYPE_NORMAL, P2_MODE_CHAIN, true);

  packet2_vif_add_micro_program(packet2, address, program->getStart(),
                                program->getEnd());

  packet2_utils_vu_add_end_tag(packet2);

  dma_channel_wait(DMA_CHANNEL_VIF1, 0);
  dma_channel_send_packet2(packet2, DMA_CHANNEL_VIF1, true);
  dma_channel_wait(DMA_CHANNEL_VIF1, 0);

  program->setDestinationAddress(address);

  packet2_free(packet2);

  return address + program->getProgramSize();
}

packet2_t* Path1::createProgramsCache(VU1Program** programs, const u32& count,
                                      const u32& address) {
  u32 packetSize = 1;  // + end tag
  for (u32 i = 0; i < count; i++) {
    packetSize += programs[i]->getPacketSize();
  }

  packet2_t* packet2 =
      packet2_create(packetSize, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);

  u32 currentAddr = address;
  for (u32 i = 0; i < count; i++) {
    programs[i]->setDestinationAddress(currentAddr);
    packet2_vif_add_micro_program(packet2, currentAddr, programs[i]->getStart(),
                                  programs[i]->getEnd());
    currentAddr += programs[i]->getProgramSize() + 1;
  }

  // The programs must stay below the draw-finish helper parked at the top of
  // micro memory - otherwise they overwrite it and the post-fx PATH1 barrier
  // (RendererCoreSync::align3D) hangs waiting for a GS FINISH that never comes.
  TYRA_ASSERT(currentAddr <= drawFinishAddr,
              "VU1 pipeline programs overflow into the draw-finish program");

  packet2_utils_vu_add_end_tag(packet2);

  return packet2;
}

/** Set double buffer settings */
void Path1::setDoubleBuffer(const u16& startingAddress, const u16& bufferSize) {
  packet2_reset(doubleBufferPacket, false);
  packet2_utils_vu_add_double_buffer(doubleBufferPacket, startingAddress,
                                     bufferSize);

  // TYRA_LOG("VU1 double buffer: starting addr: ", startingAddress,
  //          ", offset: ", startingAddress + bufferSize, ", size: ",
  //          bufferSize);

  packet2_utils_vu_add_end_tag(doubleBufferPacket);
  dma_channel_send_packet2(doubleBufferPacket, DMA_CHANNEL_VIF1, true);
  // Modified by TyraX: pipelines call this when they bring VU1 up
  // (after their own dma_channel_initialize(VIF1)) - from here on the PATH1
  // draw-finish handshake is functional and endFrame() may arm its barrier.
  vu1Configured = true;
}

}  // namespace Tyra
