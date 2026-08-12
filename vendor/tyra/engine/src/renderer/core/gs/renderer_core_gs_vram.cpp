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

int RendererCoreGSVRam::getSize(int width, const int& height, const int& psm,
                                const int& alignment) {
  int size = 0;

  // First correct the buffer width to be a multiple of 64 or 128
  // If the width is less than or equal to 16, then it's a palette
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

  // Texture storage size is in pixels/word
  switch (psm) {
    case GS_PSM_4:
      size = width * (height >> 3);
      break;
    case GS_PSM_8:
      size = width * (height >> 2);
      break;
    case GS_PSM_24:
    case GS_PSM_32:
    case GS_PSM_8H:
    case GS_PSM_4HL:
    case GS_PSM_4HH:
    case GS_PSMZ_24:
    case GS_PSMZ_32:
      size = width * height;
      break;
    case GS_PSM_16:
    case GS_PSM_16S:
    case GS_PSMZ_16:
    case GS_PSMZ_16S:
      size = width * (height >> 1);
      break;
    default:
      return 0;
  }

  if (alignment == GS_VRAM_TEXTURE_ALIGNMENT) {
    // Upstream's fixed pad, kept: it is what every allocation on this engine has
    // been sized with, and shrinking allocations is not this function's job.
    // What it is NOT is a bound - see the page footprint below, which is.
    size += 1024 * 2;
  }

  // Modified by TyraX: NEVER LESS THAN THE PAGE FOOTPRINT THE GS ACTUALLY
  // SPANS. The size above counts a texture's PIXELS; the GS stores it in whole
  // 8 KB pages, one row of pages at a time, so a texture that does not fill its
  // last page row still OWNS those pages - and the next allocation must start
  // beyond them. For a wide, short texture the two answers differ by a factor:
  //
  //   the debug HUD font, 512x16 PSMCT32 - pixels 8192 words (+2048 pad),
  //   footprint ceil(512/64) x ceil(16/32) = 8 x 1 pages = 16384 words.
  //
  // So the next texture was placed 10240 words in - page 5 of the font's own 8 -
  // and overwrote pages 5..7 of it, which is every glyph from x=320 rightwards.
  // Reported as "opening the menu makes letters disappear": the HUD read V AM
  // because R lives at x=480, and R was the only glyph past x=320 on screen (T,
  // at 496, was equally gone and simply not being drawn). The menu's own font
  // atlas is the allocation that lands on it, which is why a menu opening is
  // what triggers it, and why it survived every scene that never opens one.
  //
  // upstream's "TODO: Without this hack, textures are overlapping ourselves"
  // sat on the pad above. The pad is not the fix: it is 2048 words, and it
  // covers this for width <= 128 and nothing wider.
  //
  // A page is 8 KB = 2048 words whatever the format; only its texel geometry
  // changes. PSM_8H/4HL/4HH are the high bits of a 32-bit buffer and keep
  // 64x32. Palettes (width <= 16, the branch above leaves their width alone)
  // are stored by their own rules and are not page-mapped like this.
  if (width > 16) {
    int pageW = 64, pageH = 32;  // PSMCT32/24, PSMZ32/24, and the H formats
    switch (psm) {
      case GS_PSM_8: pageW = 128; pageH = 64; break;
      case GS_PSM_4: pageW = 128; pageH = 128; break;
      case GS_PSM_16:
      case GS_PSM_16S:
      case GS_PSMZ_16:
      case GS_PSMZ_16S: pageW = 64; pageH = 64; break;
      default: break;
    }
    const int pagesWide = (width + pageW - 1) / pageW;
    const int pagesHigh = (height + pageH - 1) / pageH;
    const int footprint = pagesWide * pagesHigh * 2048;
    if (size < footprint) size = footprint;
  }

  // The buffer size is dependent on alignment
  size = -alignment & (size + (alignment - 1));

  return size;
}

}  // namespace Tyra
