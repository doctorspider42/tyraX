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
#include <draw_buffers.h>

namespace Tyra {

struct RendererCoreTextureBuffers {
  /** Texture id */
  u32 id;

  /**
   * Texture data.
   * Used in: 4bpp, 8bpp, 24bpp, 32bpp.
   */
  texbuffer_t* core;

  /**
   * Texture pallete data.
   * Used in: 4bpp, 8bpp (otherwise nullptr).
   */
  texbuffer_t* clut;

  /**
   * Modified by TyraX: monotonic bind counter at the last useTexture() of
   * this allocation. Only meaningful on the entries RendererCoreTexture
   * keeps in its residency list (copies handed to callers are snapshots) -
   * it is what the eviction policy sorts on, and comparing it against the
   * sequence number the current frame started at answers "was this bound
   * this frame?".
   */
  u32 lastUsedSeq;
};

}  // namespace Tyra