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

#include <tamtypes.h>
#include <vector>
#include "./texture_repository.hpp"
#include "./renderer_core_texture_sender.hpp"
#include "renderer/core/paths/path3/path3.hpp"
#include "./renderer_core_texture_buffers.hpp"

namespace Tyra {

/**
 * Modified by TyraX: GS VRAM residency counters. Cheap (a handful of
 * integer increments per bind) and always compiled, but only *reported* in
 * debug builds - RendererCoreTexture::traceFrame() logs through TYRA_LOG,
 * which NDEBUG compiles away. See docs/gs-vram.md.
 */
struct RendererCoreVRamStats {
  u32 binds = 0;        // useTexture() calls that need a VRAM allocation
  u32 hits = 0;         // ...of which were already resident
  u32 uploads = 0;      // PATH3 texture transfers (allocate + send)
  u32 reuploads = 0;    // ...of which were re-sending an evicted texture
  u32 evictions = 0;    // allocations dropped to make room
  u32 evictionEvents = 0;  // binds that had to drop anything at all
  u32 resident = 0;     // allocations resident right now
  u32 peakResident = 0;
  /**
   * Frees of an allocation that was NOT the newest one. Ordinary with the
   * free-list heap (streaming layers do it on every unload) and tracked only
   * because it used to be fatal: upstream's free() rewound a bump pointer to
   * the freed address, handing the next allocation memory that still-live
   * textures above it were rendering from.
   */
  u32 unorderedFrees = 0;
  float minFreeMB = 4.0F;  // low-water mark of the free VRAM report
};

class RendererCoreTexture {
 public:
  RendererCoreTexture();
  ~RendererCoreTexture();

  clutbuffer_t clut;
  TextureRepository repository;

  RendererCoreTextureBuffers useTexture(const Texture* t_tex);

  /**
   * Called by user after changing texture wrap settings
   * Updates texture packet without reallocate it
   */
  RendererCoreTextureBuffers updateTextureInfo(const Texture* t_tex);

  /** Called by renderer during initialization */
  void init(RendererCoreGS* gs, Path3* path3);

  /** Called by renderer during rendering */
  void updateClutBuffer(texbuffer_t* clutBuffer);

  /** Modified by TyraX: releases a freed texture's GS VRAM and its
   * texbuffer structs (no-op when the texture was never uploaded). Called
   * by TextureRepository::free()/removeById(); the old removeBufferId()
   * path only tombstoned the allocation entry and leaked both. */
  void freeTextureBuffers(const u32& texId);

  /** Modified by TyraX: drop every VRAM texture allocation (the
   * runtime display-mode switch rebuilds the whole VRAM layout). Textures
   * re-upload lazily on their next use. */
  void evictAll();

  /** Modified by TyraX: made public so per-frame dynamic textures (the
   * VU0-raytraced mirror) can tell "re-upload into the existing
   * allocation" (updateTextureInfo) apart from "allocate + upload"
   * (useTexture) after an eviction flush. Returns id == 0 when the
   * texture has no GS allocation. */
  RendererCoreTextureBuffers getAllocatedBuffersByTextureId(const u32& id);

  /** Modified by TyraX: VRAM residency counters (see the struct). */
  RendererCoreVRamStats stats;

  /** Modified by TyraX: called once per frame by RendererCore::endFrame().
   * Logs a VRAMSTAT line whenever something interesting happened (an
   * eviction) and a periodic summary otherwise; no-op in release. */
  void traceFrame();

 private:
  std::vector<RendererCoreTextureBuffers> currentAllocations;

  /** Modified by TyraX: texture ids that have already been uploaded once,
   * so a repeat upload can be counted as a re-upload (the cost eviction
   * actually creates). Only grows with the number of distinct textures. */
  std::vector<u32> everUploaded;

  u32 frameCounter = 0;

  /** Modified by TyraX: monotonic bind counter driving the LRU/MRU choice,
   * and the value it held when the current frame started. */
  u32 useSeq = 0;
  u32 frameStartSeq = 0;
  u32 prevFrameStartSeq = 0;

  u32 lastLoggedEvictions = 0;
  RendererCoreVRamStats lastLogged;

  void initClut();

  /** Modified by TyraX: index of the allocation to evict next (-1 if none). */
  int pickVictim() const;

  /** Modified by TyraX: evict until `t_tex` fits. */
  void makeRoomFor(const Texture* t_tex);

  void registerAllocation(const RendererCoreTextureBuffers& t_buffers);
  void unregisterAllocation(const u32& textureId);

  RendererCoreGS* gs;
  RendererCoreTextureSender sender;
  Path3* path3;
};

}  // namespace Tyra
