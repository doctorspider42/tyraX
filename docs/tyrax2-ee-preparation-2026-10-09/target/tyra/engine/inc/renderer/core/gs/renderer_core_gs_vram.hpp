/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: real GS VRAM residency management. Upstream was a
# bump pointer whose free() was "pointer = address" - order dependent, so
# freeing anything but the newest allocation handed the memory of still-live
# textures back out (layer streaming reproduced it: three freed textures,
# three survivors rendering from overwritten VRAM). See docs/gs-vram.md.
*/

#pragma once

#include <vector>
#include "renderer/renderer_settings.hpp"
#include "renderer/core/texture/models/texture.hpp"

namespace Tyra {

/**
 * GS VRAM allocator.
 *
 * Two regions, both in GS words (1 word = 4 bytes, 1 MB = 262144 words):
 *
 * - The **permanent region** at the bottom, filled by allocateBuffer() during
 *   renderer init (both frame buffers, the z buffer, the post-fx scratch
 *   buffers, the film-grain noise, the env-map / camera-feed render targets).
 *   It is a plain bump region that is never freed - free() simply does not
 *   know those addresses, so a stray free can't reclaim them.
 * - The **texture heap** above it, managed by a coalescing free list.
 *   allocate() takes a best-fit block, free() gives it back in any order.
 */
class RendererCoreGSVRam {
 public:
  RendererCoreGSVRam();
  ~RendererCoreGSVRam();

  /** Total free space in the texture heap (sum of all free blocks). */
  const float& getFreeSpaceInMB();

  float getSizeInMB(const Texture& texture);
  float getSizeInMB(const TextureData& texData);
  float getSizeInMB(int width, const int& height, const int& psm,
                    const int& alignment);

  int allocate(const TextureData& texData);
  int allocateBuffer(const int& width, const int& height, const int& psm);
  int allocate(const int& width, const int& height, const int& psm,
               const int& alignment);

  /**
   * Release a texture allocation. Order independent (TyraX): the block
   * returns to the free list and coalesces with its neighbours, so freeing
   * the oldest texture while newer ones are live is safe. Addresses the
   * allocator does not own (the permanent buffers, a double free) are
   * ignored.
   */
  void free(const int& address);

  /** Modified by TyraX: forget every allocation - used by the runtime
   * display-mode switch, which rebuilds the whole VRAM layout from the frame
   * buffers up. Callers must have released every texture first. */
  void reset();

  // --- Modified by TyraX: residency queries used by the texture manager ---

  /** Words a texture's pixel data needs: its real swizzled GS footprint
   * (see getSize) rounded up to block alignment. */
  int getSizeWords(const TextureData& texData);

  /** True when the heap can serve one block of `words`. */
  bool canAllocate(const int& words) const;

  /**
   * True when the heap can serve `wordsA` and then `wordsB` as two separate
   * blocks - a texture with a palette needs both, and the pair must be
   * decided before either is taken (a half-allocated texture has nowhere
   * to go).
   */
  bool canAllocatePair(const int& wordsA, const int& wordsB) const;

  /** Largest single block the heap can serve right now, in words. */
  int getLargestFreeWords() const;

  /** Words held by the live allocation at `address` (0 when unknown). */
  int getAllocationWords(const int& address) const;

  /** Total words the texture heap spans (free + allocated). */
  int getHeapWords() const { return maxWords - permanentEnd; }

 private:
  struct Block {
    int start;
    int size;
  };

  int getSize(int width, const int& height, const int& psm,
              const int& alignment);

  int allocatePermanent(const int& size);
  int allocateFromHeap(const int& size);
  void heapReset();

  static constexpr float ptr2MB = 262144.0F;
  int maxWords;

  /** End of the never-freed init-time buffer region = heap floor. */
  int permanentEnd;

  /** Free blocks, sorted by address, disjoint and always coalesced. */
  std::vector<Block> freeList;

  /** Live texture allocations, so free(address) can recover the size. */
  std::vector<Block> liveList;

  bool touched;
  float cachedFreeSpace;
};

}  // namespace Tyra
