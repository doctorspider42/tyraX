/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include <dma.h>
#include <packet2_utils.h>
#include <vector>
#include "debug/debug.hpp"
#include "math/m4x4.hpp"
#include "renderer/renderer_settings.hpp"
#include "renderer/models/color.hpp"
#include "./stapip_qbuffer.hpp"
#include "./stapip_program_type.hpp"
#include "./stapip_program_name.hpp"
#include "./stapip_programs_repository.hpp"
#include "./stapip_clipper.hpp"
#include "renderer/core/paths/path1/path1.hpp"
#include "renderer/core/renderer_core.hpp"
#include "renderer/core/texture/renderer_core_texture_buffers.hpp"

namespace Tyra {

class StaPipQBufferRenderer {
 public:
  StaPipQBufferRenderer();
  ~StaPipQBufferRenderer();

  void init(RendererCore* t_core, prim_t* prim, lod_t* lod);

  void reinitVU1();

  void setClipperMVP(M4x4* mvp) { clipper.setMVP(mvp); }

  StaPipQBuffer* getBuffer();

  // Modified by TyraX: non-const - also pushes the per-mesh
  // object-space spot light to the EE clipper.
  void sendObjectData(StaPipBag* bag, M4x4* mvp,
                      RendererCoreTextureBuffers* texBuffers);

  // Modified by TyraX: the dynamic light this bag renders with (picked per
  // bag by StaPipCore::render from the flashlight + scene lights). Null =
  // fall back to the global flashlight state.
  void setBagLight(const RendererCoreSpotLight* light) { bagLight = light; }

  void setMaxVertCount(const u32& count);

  void setInfo(PipelineInfoBag* bag);

  /** Fast render with culling */
  void cull(StaPipQBuffer* buffer);

  /** Slower render with clipping */
  void clip(StaPipQBuffer* buffer);

  /**
   * Modified by TyraX: route clip-classified packages to the VU1 clip
   * program family instead of the EE clipper + as_is programs. Swaps the
   * uploaded program set (micro memory can't hold cull + as_is + clip at
   * once), so call it right after setRenderer() / between frames only.
   */
  void setVU1Clipping(const bool& enabled);

  /** TyraX addition: which MATERIAL CLASSES keep a resident VU1 program
   * (docs/vu-framework.md). VU1 micro memory holds ~2042 instruction slots and
   * the full set of ten sits right under that ceiling, so a project that never
   * draws a lit mesh is paying ~380 instructions for programs it cannot reach.
   * Dropping a class frees that room for a program the user wrote.
   *
   * One bit per class; Color is always kept, because it is what everything else
   * falls back to. Never calling this keeps every class, i.e. exactly the
   * behaviour before it existed. */
  enum StaPipProgramClass {
    StaPipClassColor = 1 << 0,
    StaPipClassDirLights = 1 << 1,
    StaPipClassTextureDirLights = 1 << 2,
    StaPipClassTextureColor = 1 << 3,
    StaPipClassTextureEnv = 1 << 4,
    StaPipClassAll = 0x1F,
  };
  void setResidentClasses(const u32& mask);
  const u32& getResidentClasses() const { return residentClasses; }

  /** TyraX addition: install a game-supplied microprogram over a built-in slot
   * and make it resident (docs/vu-framework.md). Rebuilds the program cache and
   * re-uploads it, so it is safe to call after init - and it must be, because a
   * game only learns about its own programs once its scene is up. */
  void setProgramOverride(const StaPipProgramName& name,
                          StaPipVU1Program* program);
  /** TyraX addition: several overrides, ONE rebuild. Swapping a whole look at
   * run time touches every material class, and doing that through the single
   * setter above would drain the pipeline and re-upload the program cache once
   * PER CLASS. A null entry restores the engine's own program for that slot.
   *
   * Microcode is a u32 range in EE memory and createProgramsCache assigns the
   * micro-memory addresses when it builds the packet, so alternative programs
   * cost EE RAM and nothing in VU1 micro memory - only the active set is ever
   * uploaded. That is what makes swapping a look affordable at all. */
  void setProgramOverrides(const StaPipProgramName* names,
                           StaPipVU1Program* const* programs, u32 count);
  const bool& isVU1ClippingEnabled() const { return vu1Clipping; }

  /** TyraX addition: the two quadwords a project's own microprogram reads
   * (docs/vu-authoring.md) - four numbers the game sets per mesh, and the
   * clock. They land at VU1_CUSTOM_PARAMS_ADDR / VU1_CUSTOM_TIME_ADDR, which
   * are inside the DIRECTIONAL-LIGHTS colour block, so they are uploaded only
   * for a bag with no lighting: a lit bag needs those addresses for its light
   * colours and would be corrupted by them.
   *
   * Off by default. A project with no custom program must not pay two extra
   * unpacked quadwords per mesh for a feature it does not use, so codegen turns
   * this on once at startup and never otherwise. */
  void setVuCustomEnabled(const bool& enabled) { vuCustomEnabled = enabled; }
  void setVuParams(const float& x, const float& y, const float& z,
                   const float& w) {
    vuParams[0] = x, vuParams[1] = y, vuParams[2] = z, vuParams[3] = w;
  }
  /** Seconds, plus its sine and cosine - computed here so a program that only
   * needs the whole mesh to pulse can skip its own 17-instruction series.
   * WRAP the value: the microprogram's range reduction folds through a 2^23
   * add and loses precision long before a float would. */
  void setVuTime(const float& seconds);

  /**
   * Modified by TyraX: particle billboards. The resident program set has no
   * room for the billboard family (the VU1-clipping set fills micro memory
   * to the brim), so the two billboard programs live in their own small
   * packet and are swapped in when a billboard bag renders - the same
   * upload mechanism a StaPip<->DynPip pipeline switch uses every frame.
   * The main set is lazily restored by the next non-billboard bag.
   */
  void ensureProgramSet(const bool& billboard);

  void flushBuffers();

  void clearLastProgramName();

  StaPipVU1Program* getCullProgramByBag(const StaPipBag* bag);

  StaPipVU1Program* getCullProgramByParams(const bool& isLightingEnabled,
                                           const bool& isTextureEnabled);

  const u16& getBufferSize() { return bufferSize; }

  void allocateOnUse();
  void deallocateOnUse();

 private:
  prim_t* prim;
  lod_t* lod;

  bool is1stDBufferFlushTime();
  bool is2ndDBufferFlushTime();

  void sendStaticData() const;
  void setProgramsCache();
  void uploadPrograms();
  void setDoubleBuffer();
  u16 getQBufferIndex(StaPipQBuffer* buffer);
  u16 qbuffersPacketSize;

  static const u16 buffersCount;

  StaPipVU1Program* getProgramByName(const StaPipProgramName& name);
  void addBuffersDataToPacket(const u32& from, const u32& to);
  void sendPacket();
  StaPipVU1Program* getAsIsProgramByBag(const StaPipBag* bag);
  // Modified by TyraX: VU1 clipping.
  StaPipVU1Program* getClipProgramByBag(const StaPipBag* bag);
  StaPipVU1Program* getCullProgramByType(const StaPipProgramType& programType);
  StaPipProgramType getDrawProgramTypeByBag(const StaPipBag* bag) const;
  StaPipProgramType getDrawProgramTypeByParams(
      const bool& isLightingEnabled, const bool& isTextureEnabled) const;
  packet2_t* programsPacket;
  // Modified by TyraX: on-demand billboard program set (see
  // ensureProgramSet).
  packet2_t* billboardProgramsPacket;
  bool billboardSetActive = false;

  packet2_t** packets;
  StaPipVU1Program** dBufferPrograms;
  StaPipQBuffer** buffers;
  packet2_t* staticDataPacket;
  packet2_t* objectDataPacket;

  RendererCore* rendererCore;

  StaPipProgramName lastProgramName;
  Path1* path1;
  StaPipClipper clipper;
  StaPipProgramsRepository repository;
  /** TyraX addition: see setResidentClasses. */
  u32 residentClasses = StaPipClassAll;
  /** TyraX addition: see setVuCustomEnabled. */
  bool vuCustomEnabled = false;
  float vuParams[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  float vuTime[4] = {0.0F, 0.0F, 1.0F, 1.0F};
  /** TyraX addition: the requested program's class is not resident - walk down
   * to one that is, rather than MSCAL-ing to an address nothing was uploaded
   * to. A dropped class then draws in a simpler style instead of tearing the
   * screen, which is the right failure for something the editor is supposed to
   * have proven unnecessary in the first place. */
  StaPipProgramName residentFallback(const StaPipProgramName& name) const;

  u16 bufferSize, nextBufferIndex, currentBufferIndex;
  // Modified by TyraX: VU1 buffer capacity, used by clip() to drain the
  // clipper output in buffer-sized chunks.
  u32 maxVertCount = 0;
  u8 context;
  // Modified by TyraX: VU1 clipping mode + the clip-space constants the
  // clip programs consume (see VU1_CLIP_CONSTS_ADDR / VU1_CLIP_PLANES_ADDR).
  bool vu1Clipping = false;
  float clipNearZ = 0.0F, clipFarZ = 0.0F;
  // Modified by TyraX: per-bag dynamic light (see setBagLight).
  const RendererCoreSpotLight* bagLight = nullptr;
};

}  // namespace Tyra
