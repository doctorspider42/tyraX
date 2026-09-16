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

#include <math3d.h>
#include <string>
#include "debug/debug.hpp"
#include "./bag/packaging/stapip_bag_package.hpp"
#include "./bag/stapip_bag.hpp"
#include "./stapip_probes.hpp"

namespace Tyra {

class StaPipQBuffer {
 public:
  StaPipQBuffer();
  ~StaPipQBuffer();

  void setMaxVertCount(const u32& count);
  // Modified by TyraX: switch the copy pools to their other side - call once
  // per packet SEND, right where the packet double buffer flips (see the pool
  // comment in stapip_qbuffer.cpp).
  static void flipPoolSide();

  /**
   * @brief Dont allocate any dynamic data in buffer.
   * Just copy pointers to input data.
   */
  void fillByPointer(const StaPipBagPackage& pkg);

  /**
   * @brief Point directly at one contiguous range of a wholly visible bag.
   * Avoids constructing package descriptors when the bag-level bbox already
   * proved every range is inside the frustum.
   */
  void fillByPointer(StaPipBag* bag, u32 offset, u32 count);

  /**
   * @brief Allocate dynamic data in buffer
   * And copy input data to it.
   */
  void fillByCopyMax(const StaPipBagPackage& pkg1, const StaPipBagPackage& pkg2,
                     const StaPipBagPackage& pkg3);

  /**
   * @brief Allocate dynamic data in buffer
   * And copy input data to it.
   */
  void fillByCopy1By2(const StaPipBagPackage& pkg1,
                      const StaPipBagPackage& pkg2);

  /**
   * @brief Allocate dynamic data in buffer
   * And copy input data to it.
   */
  void fillByCopy1By3(const StaPipBagPackage& pkg);

  /**
   * Modified by TyraX: expand `triCount` triangles of a STRIPPED package back
   * into an ordinary triangle list, into the copy pool.
   *
   * The clip programs and the EE clipper both loop by whole triangles over a
   * triangle list, so a stripped package that genuinely crosses a clip plane
   * cannot be handed to either as it stands. It is expanded here instead -
   * triangle i of the strip is (v[i], v[i+1], v[i+2]) - and the result is an
   * ordinary list qbuffer that every existing route already handles. The
   * buffer's own `stripped` flag is cleared, which is what makes the GIF tag
   * this buffer produces say PRIM_TRIANGLE, so one bag may mix the two.
   *
   * `firstTri` is an index into the package's triangles, not its vertices.
   * The expansion is 3x, so the caller must chunk it: at most maxVertCount / 3
   * triangles at a time, which is the same triangle budget the list path's
   * subpackages carry.
   */
  void fillByStripExpand(const StaPipBagPackage& pkg, u32 firstTri,
                         u32 triCount);

  /**
   * @brief Deallocate dynamic data if it was allocated and allocate new data
   * specified by size.
   * @param size 48 is max
   */
  void reallocateManually(const u16& size);

  bool any() const;

  StaPipBag* bag;

  Vec4* vertices;
  Vec4* sts;
  Vec4* colors;
  Vec4* normals;
  u32 size;
  /** Conservative OR of the source packages' exact VU clip-plane masks. */
  u8 clipPlaneMask;

  /**
   * Modified by TyraX: this buffer's vertices are a triangle STRIP, so the
   * GIF tag the microprogram writes must carry PRIM_TRIANGLE_STRIP. It is per
   * BUFFER and not per bag on purpose - a stripped bag's clip-routed packages
   * are expanded back to lists (fillByStripExpand) and travel in the same
   * flush as its stripped ones.
   */
  bool stripped;

  /**
   * Modified by TyraX: which package of the current bag's RETAINED command
   * block this buffer is, or -1 when it has none (a copied, clipped or
   * strip-expanded buffer, a bag the cache refused, or the feature compiled
   * out). See StaPipRetainedCommands in stapip_qbuffer_renderer.hpp.
   *
   * StaPipQBufferRenderer::getBuffer() resets it - the one gate every buffer
   * passes through before any fill - so a slot recycled out of a retained bag
   * cannot carry a stale index into a copied or clipped one. StaPipCore sets
   * it back after the fill, for the routes that may be retained.
   */
  int retainIndex;

#if TYRA_STAPIP_PROBE_UNCACHED_CHAIN
  /**
   * Probe B only (stapip_probes.hpp): this buffer's streams live in the
   * qbuffer COPY POOL, written by the EE between sends, rather than in the
   * bag's own long-lived arrays. The DMA REF tags then point at memory whose
   * dirty cache lines only `FlushCache` writes back, so a send carrying one of
   * these must keep the flush. Cleared by getBuffer(), set by the copy fills
   * and by StaPipClipper::writeChunk.
   */
  bool probeCopyFilled;
#endif

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;

 private:
  u32 maxVertCount;
  void deallocateDynamicData();
  void allocateDynamicData(u16 size, StaPipBag* bag);
  u8 _isDynamicallyAllocated, _stAllocated, _colorAllocated, _normalAllocated;
};

}  // namespace Tyra
