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

#include <vector>
#include "debug/debug.hpp"
#include "./stapip_qbuffer.hpp"
#include "renderer/renderer_settings.hpp"
#include "renderer/core/3d/clipper/planes_clip_algorithm.hpp"

namespace Tyra {

/**
 * Modified by tyra-editor: per-mesh spot light state. The as_is VU1 programs
 * cannot evaluate the spot light (vertices reach them already clipped and
 * perspective-divided), so the EE injects it into the vertex colors before
 * clipping interpolates them - keeps screen-edge triangles consistent with
 * the cull path, which computes the same formula on VU1.
 */
struct StaPipClipperSpot {
  bool enabled = false;
  Vec4 position;   // object space
  Vec4 direction;  // object space, normalized
  float color[3] = {0.0F, 0.0F, 0.0F};
  float invRange2 = 0.0F;
  float cosCut2 = 0.0F;
  float invSoft = 0.0F;
};

/**
 * @brief This class requires VU1 buffer with max buffsize/3 vertices.
 * It will clip triangles and fill the buffer.
 *
 * To be honest clipping algorithm should be moved to VU1 and "AsIs" VU1 program
 * should be renamed to "Clip" - I don't want to do it now, too much time.
 */
class StaPipClipper {
 public:
  StaPipClipper();
  ~StaPipClipper();

  /**
   * Modified by tyra-editor: clipping is drained in VU1-buffer-sized chunks.
   * A single triangle clipped against the frustum fans out into up to 7
   * triangles (21 verts), so one subpackage can produce more verts than a VU1
   * buffer holds (maxVertCount). clipToPool() runs the whole clip into an
   * internal, perspective-divided pool and returns its vertex count; the
   * renderer then copies it out with writeChunk() across as many VU1 draws as
   * needed. Splitting like this avoids the "Max buffer size in VU1" assert in
   * stapip_qbuffer.cpp.
   */
  u32 clipToPool(StaPipQBuffer* buffer);

  /** Copies [start, start + count) pooled verts into buffer. count must be
   * <= maxVertCount and a multiple of 3; buffer->bag must already be set. */
  void writeChunk(StaPipQBuffer* buffer, u32 start, u32 count);

  void init(const RendererSettings& settings);
  void setMaxVertCount(const u32& count);
  void setMVP(M4x4* mvp);
  void setSpot(const StaPipClipperSpot& t_spot) { spot = t_spot; }

 private:
  u32 maxVertCount;
  PlanesClipAlgorithm algorithm;
  M4x4* mvp;
  StaPipClipperSpot spot;

  Vec4 inputVerts[3];
  PlanesClipVertexPtrs inputTriangle[3];
  PlanesClipVertex clippedTriangle[9];

  void perspectiveDivide(std::vector<PlanesClipVertex>* vertices);
  void moveDataToBuffer(const std::vector<PlanesClipVertex>& vertices,
                        StaPipQBuffer* buffer);
};

}  // namespace Tyra
