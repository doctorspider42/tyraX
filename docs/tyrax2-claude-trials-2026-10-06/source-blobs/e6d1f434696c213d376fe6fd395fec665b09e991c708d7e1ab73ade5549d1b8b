/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: free-list texture heap - see the header.
*/

#include <dma.h>
#include <graph.h>
#include <gs_psm.h>
#include "debug/debug.hpp"
#include "renderer/core/gs/renderer_core_gs_vram.hpp"

#define GS_VRAM_TEXTURE_ALIGNMENT GRAPH_ALIGN_BLOCK
#define GS_VRAM_BUFFER_ALIGNMENT GRAPH_ALIGN_PAGE

namespace Tyra {

RendererCoreGSVRam::RendererCoreGSVRam() {
  maxWords = GRAPH_VRAM_MAX_WORDS;
  permanentEnd = 0;
  touched = false;
  freeList.reserve(32);
  liveList.reserve(64);
  heapReset();
  cachedFreeSpace = getFreeSpaceInMB();
}

RendererCoreGSVRam::~RendererCoreGSVRam() {}

void RendererCoreGSVRam::heapReset() {
  freeList.clear();
  liveList.clear();
  if (permanentEnd < maxWords)
    freeList.push_back({permanentEnd, maxWords - permanentEnd});
  touched = true;
}

void RendererCoreGSVRam::reset() {
  permanentEnd = 0;
  heapReset();
}

const float& RendererCoreGSVRam::getFreeSpaceInMB() {
  if (touched) {
    int total = 0;
    for (u32 i = 0; i < freeList.size(); i++) total += freeList[i].size;
    cachedFreeSpace = total / ptr2MB;
    touched = false;
  }

  return cachedFreeSpace;
}

int RendererCoreGSVRam::getLargestFreeWords() const {
  int best = 0;
  for (u32 i = 0; i < freeList.size(); i++)
    if (freeList[i].size > best) best = freeList[i].size;
  return best;
}

int RendererCoreGSVRam::getAllocationWords(const int& address) const {
  for (u32 i = 0; i < liveList.size(); i++)
    if (liveList[i].start == address) return liveList[i].size;
  return 0;
}

bool RendererCoreGSVRam::canAllocate(const int& words) const {
  if (words <= 0) return true;
  for (u32 i = 0; i < freeList.size(); i++)
    if (freeList[i].size >= words) return true;
  return false;
}

// Best-fit twice, without touching the free list. The BIGGER request is
// placed first, matching the real allocation order (a texture's pixel data
// goes down before its palette) and making the answer conservative: a "yes"
// here is a guarantee, because whatever block the big one takes, the small
// one still has somewhere to go.
bool RendererCoreGSVRam::canAllocatePair(const int& wordsA,
                                         const int& wordsB) const {
  if (wordsA <= 0) return canAllocate(wordsB);
  if (wordsB <= 0) return canAllocate(wordsA);

  const int big = wordsA > wordsB ? wordsA : wordsB;
  const int small = wordsA > wordsB ? wordsB : wordsA;

  // Take the tightest block that fits `big`, then check `small` against what
  // is left (including that block's remainder).
  int bestIdx = -1;
  int bestSize = 0;
  for (u32 i = 0; i < freeList.size(); i++) {
    if (freeList[i].size < big) continue;
    if (bestIdx < 0 || freeList[i].size < bestSize) {
      bestIdx = static_cast<int>(i);
      bestSize = freeList[i].size;
    }
  }
  if (bestIdx < 0) return false;

  for (u32 i = 0; i < freeList.size(); i++) {
    const int avail = static_cast<int>(i) == bestIdx ? freeList[i].size - big
                                                     : freeList[i].size;
    if (avail >= small) return true;
  }
  return false;
}

float RendererCoreGSVRam::getSizeInMB(const Texture& texture) {
  float result = getSizeInMB(*texture.core);

  if (texture.clut != nullptr) {
    result += getSizeInMB(*texture.clut);
  }

  return result;
}

float RendererCoreGSVRam::getSizeInMB(const TextureData& texData) {
  return getSizeInMB(texData.width, texData.height, texData.psm,
                     GS_VRAM_TEXTURE_ALIGNMENT);
}

float RendererCoreGSVRam::getSizeInMB(int width, const int& height,
                                      const int& psm, const int& alignment) {
  return getSize(width, height, psm, alignment) / ptr2MB;
}

// Modified by TyraX
int RendererCoreGSVRam::getSizeWords(const TextureData& texData) {
  return getSize(texData.width, texData.height, texData.psm,
                 GS_VRAM_TEXTURE_ALIGNMENT);
}

int RendererCoreGSVRam::allocate(const TextureData& texData) {
  return allocate(texData.width, texData.height, texData.psm,
                  GS_VRAM_TEXTURE_ALIGNMENT);
}

int RendererCoreGSVRam::allocateBuffer(const int& width, const int& height,
                                       const int& psm) {
  return allocate(width, height, psm, GS_VRAM_BUFFER_ALIGNMENT);
}

int RendererCoreGSVRam::allocate(const int& width, const int& height,
                                 const int& psm, const int& alignment) {
  const auto size = getSize(width, height, psm, alignment);
  if (size <= 0) return -1;

  // Page-aligned allocations are the renderer's own permanent buffers; block
  // aligned ones are textures and live on the managed heap.
  if (alignment == GS_VRAM_BUFFER_ALIGNMENT) return allocatePermanent(size);
  return allocateFromHeap(size);
}

// Modified by TyraX: the init-time buffer region. Grows from address 0 and is
// never released, so the heap floor rises with it. Only legal while the heap
// is empty - every caller runs during renderer init (or right after reset()
// during a display-mode switch, which evicts all textures first).
int RendererCoreGSVRam::allocatePermanent(const int& size) {
  if (!liveList.empty()) {
    TYRA_WARN("VRAM: permanent buffer requested with ", liveList.size(),
              " textures resident - dropping them.");
  }

  if (permanentEnd + size > maxWords) return -1;

  const int address = permanentEnd;
  permanentEnd += size;
  heapReset();
  return address;
}

// Modified by TyraX: best fit over the coalesced free list. Best fit (rather
// than first fit) keeps the big blocks big, which matters a lot here - the
// heap is ~1 MB and a single 256x256 32bpp texture is a quarter of it.
int RendererCoreGSVRam::allocateFromHeap(const int& size) {
  int bestIdx = -1;
  int bestSize = 0;

  for (u32 i = 0; i < freeList.size(); i++) {
    if (freeList[i].size < size) continue;
    if (bestIdx < 0 || freeList[i].size < bestSize) {
      bestIdx = static_cast<int>(i);
      bestSize = freeList[i].size;
    }
  }

  if (bestIdx < 0) return -1;

  const int address = freeList[bestIdx].start;
  freeList[bestIdx].start += size;
  freeList[bestIdx].size -= size;
  if (freeList[bestIdx].size == 0)
    freeList.erase(freeList.begin() + bestIdx);

  liveList.push_back({address, size});
  touched = true;

  return address;
}

// Modified by TyraX: order-independent release + coalescing.
void RendererCoreGSVRam::free(const int& address) {
  int size = 0;
  bool found = false;

  for (u32 i = 0; i < liveList.size(); i++) {
    if (liveList[i].start != address) continue;
    size = liveList[i].size;
    liveList.erase(liveList.begin() + i);
    found = true;
    break;
  }

  // Not ours: the permanent buffers (never handed out by the heap) or a
  // double free. Upstream would have rewound the bump pointer here.
  if (!found) return;

  u32 at = 0;
  while (at < freeList.size() && freeList[at].start < address) at++;
  freeList.insert(freeList.begin() + at, {address, size});

  // Coalesce with the next block, then with the previous one.
  if (at + 1 < freeList.size() &&
      freeList[at].start + freeList[at].size == freeList[at + 1].start) {
    freeList[at].size += freeList[at + 1].size;
    freeList.erase(freeList.begin() + at + 1);
  }
  if (at > 0 &&
      freeList[at - 1].start + freeList[at - 1].size == freeList[at].start) {
    freeList[at - 1].size += freeList[at].size;
    freeList.erase(freeList.begin() + at);
  }

  touched = true;
}

// Modified by TyraX: the GS storage geometry of a pixel storage mode - the
// size of one page in texels, of one block in texels, and which of the two
// block-order patterns the page uses. Every page is 2048 words (8 KB) and
// holds 32 blocks; the modes differ in how many texels that is. 8H/4HL/4HH
// live in the high bits of a 32-bit page, so they share its layout.
struct GsPsmLayout {
  int pageW, pageH;    // texels per page
  int blockW, blockH;  // texels per block
  int cols, rows;      // blocks per page
  const unsigned char* order;  // block index at [row * cols + col], or null
};

// The two block orders that matter here. Both are Morton (bit-interleaved)
// curves, which is what makes the corner rule below valid: the largest index
// over any top-left sub-rectangle sits in its bottom-right corner.
// The Z-buffer orders are permuted variants for which that does NOT hold -
// they get no sub-page path (a z buffer is never sub-page anyway).
static const unsigned char kGsOrder8x4[32] = {
    0,  1,  4,  5,  16, 17, 20, 21,  //
    2,  3,  6,  7,  18, 19, 22, 23,  //
    8,  9,  12, 13, 24, 25, 28, 29,  //
    10, 11, 14, 15, 26, 27, 30, 31};
static const unsigned char kGsOrder4x8[32] = {
    0,  2,  8,  10,  //
    1,  3,  9,  11,  //
    4,  6,  12, 14,  //
    5,  7,  13, 15,  //
    16, 18, 24, 26,  //
    17, 19, 25, 27,  //
    20, 22, 28, 30,  //
    21, 23, 29, 31};

static bool gsPsmLayout(const int& psm, GsPsmLayout* out) {
  switch (psm) {
    case GS_PSM_4:
      *out = {128, 128, 32, 16, 4, 8, kGsOrder4x8};
      return true;
    case GS_PSM_8:
      *out = {128, 64, 16, 16, 8, 4, kGsOrder8x4};
      return true;
    case GS_PSM_16:
      *out = {64, 64, 16, 8, 4, 8, kGsOrder4x8};
      return true;
    case GS_PSM_16S:
      // Same page/block geometry, a permuted order - page path only.
      *out = {64, 64, 16, 8, 4, 8, nullptr};
      return true;
    case GS_PSM_24:
    case GS_PSM_32:
    case GS_PSM_8H:
    case GS_PSM_4HL:
    case GS_PSM_4HH:
      *out = {64, 32, 8, 8, 8, 4, kGsOrder8x4};
      return true;
    case GS_PSMZ_16:
    case GS_PSMZ_16S:
      *out = {64, 64, 16, 8, 4, 8, nullptr};
      return true;
    case GS_PSMZ_24:
    case GS_PSMZ_32:
      *out = {64, 32, 8, 8, 8, 4, nullptr};
      return true;
    default:
      return false;
  }
}

// Modified by TyraX: the real GS footprint instead of a flat +8 KB pad.
//
// GS memory is paged and SWIZZLED: the texels of one page are spread over
// its 32 blocks in a scrambled order, so "width * height words" is NOT what
// an image occupies. A 64x8 PSMCT32 texture has 8 rows of texels but reaches
// block 21 of its page - allocate it its 512 words and the next texture
// lands inside it. Upstream papered over that with `size += 1024 * 2` and the
// comment "without this hack, textures are overlapping ourselves". That pad
// is not derived from anything: too little to be a guarantee, far too much
// for a big texture, and brutal on palettes - a 16-entry CLUT carries 64
// BYTES of data and was charged 8.25 KB, so twenty palettized textures spent
// 160 KB of a ~1 MB heap on their palettes' padding alone.
//
// What a region actually occupies is the HIGHEST BLOCK it touches: a texel
// sits at page index (y / pageH) * pagesW + (x / pageW), 32 blocks per page,
// plus the block the swizzle puts it at inside that page. The last page has
// the highest index, and 32 * n always beats 32 * (n - 1) + 31, so the whole
// region's maximum is the last page's bottom-right block - which, the orders
// being Morton curves, is the entry at the corner of the blocks that page
// actually uses. Z-buffer orders are permuted and get whole pages instead;
// a z buffer is never sub-page, so nothing is lost.
//
// TBP0 (and CBP) count blocks, and the swizzle is computed relative to that
// base, so a block-aligned allocation that is not page-aligned is fine.
//
// Frame/z/post-fx buffers are unchanged by this (they were always whole
// pages); a 4-bit 128x128 texture halves, and a 16-entry CLUT drops 33x.
// Extreme aspect ratios grow, because the old formula UNDER-allocated them:
// a 512x32 PSMCT16 strip spans 8 pages and reaches word 15360, and was being
// handed 10240 - the +8 KB pad was never a fix for the overlap it named,
// only a way of making it less likely.
int RendererCoreGSVRam::getSize(int width, const int& height, const int& psm,
                                const int& alignment) {
  GsPsmLayout l;
  if (!gsPsmLayout(psm, &l)) return 0;
  if (width <= 0 || height <= 0) return 0;

  // Correct the buffer width to a multiple of 64 or 128 - that is the width
  // the GS is handed (TBW), so it is the width that is occupied. A width of
  // 16 or less is a CLUT, which is addressed by CBP and gets no such rounding
  // (this is what keeps a 16-entry palette at one block).
  if (width > 16) {
    switch (psm) {
      case GS_PSM_8:
      case GS_PSM_4:
      case GS_PSM_8H:
      case GS_PSM_4HL:
      case GS_PSM_4HH:
        width = -128 & (width + 127);
        break;
      default:
        width = -64 & (width + 63);
        break;
    }
  }

  const int pagesW = (width + l.pageW - 1) / l.pageW;
  const int pagesH = (height + l.pageH - 1) / l.pageH;

  // The GS stores an image in whole pages, one page row at a time, and
  // scrambles its texels across a page's 32 blocks - so what an allocation
  // must cover is the HIGHEST BLOCK its texels reach: the last page's index
  // times 32, plus the block the swizzle puts that page's bottom-right corner
  // at. (Both orders are Morton curves, which is what makes the corner the
  // maximum. The Z orders are permuted variants for which that is false, so
  // they round to whole pages instead; a z buffer is never sub-page.)
  //
  // This is a BOUND, which the arithmetic it replaced was not. Upstream sized
  // an allocation by its pixel count plus a flat 2048-word pad carrying the
  // comment "TODO: Without this hack, textures are overlapping ourselves" -
  // and the pad covers a wide, short texture for width <= 128 and nothing
  // wider. The debug HUD font, 512x16 PSMCT32, counts 8192 words of pixels
  // but spans 8 pages, so the next texture was placed 10240 words in - page 5
  // of the font's own 8 - and overwrote every glyph from x=320 rightwards.
  // Reported as "opening the menu makes letters disappear": the HUD read
  // "V AM" because R lives at x=480, and the menu's own font atlas is the
  // allocation that lands on it, which is why only a scene that opens a menu
  // ever showed it.
  //
  // Dropping the pad is the other half. It was never derived from anything,
  // and it is brutal on palettes: a 16-entry CLUT holds 64 BYTES and was
  // charged 8.25 KB, so twenty palettized textures spent 160 KB of a ~1 MB
  // heap on padding alone. A CLUT is addressed by CBP in blocks and the width
  // <= 16 branch above leaves its width alone, so the block path sizes it
  // exactly: one block for 16 entries, four for 256.
  int size;
  if (l.order != nullptr) {
    // Blocks used inside the LAST page (the partial remainder of each axis).
    int blocksW = (width - (pagesW - 1) * l.pageW + l.blockW - 1) / l.blockW;
    int blocksH = (height - (pagesH - 1) * l.pageH + l.blockH - 1) / l.blockH;
    if (blocksW < 1) blocksW = 1;
    if (blocksH < 1) blocksH = 1;
    if (blocksW > l.cols) blocksW = l.cols;
    if (blocksH > l.rows) blocksH = l.rows;
    const int lastPage = pagesW * pagesH - 1;
    const int lastBlock =
        lastPage * 32 + l.order[(blocksH - 1) * l.cols + (blocksW - 1)];
    size = (lastBlock + 1) * GRAPH_ALIGN_BLOCK;
  } else {
    size = pagesW * pagesH * GRAPH_ALIGN_PAGE;
  }

  // The buffer size is dependent on alignment
  size = -alignment & (size + (alignment - 1));

  return size;
}

}  // namespace Tyra
