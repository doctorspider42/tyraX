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

 private:
  std::vector<RendererCoreTextureBuffers> currentAllocations;

  void initClut();
  void registerAllocation(const RendererCoreTextureBuffers& t_buffers);
  void unregisterAllocation(const u32& textureId);

  RendererCoreGS* gs;
  RendererCoreTextureSender sender;
  Path3* path3;
};

}  // namespace Tyra
