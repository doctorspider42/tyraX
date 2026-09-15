/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: GS VRAM residency. Upstream deallocated EVERY resident
# texture the moment one did not fit; this evicts coldest-first until the
# newcomer fits (see pickVictim) and reports what it did through
# RendererCoreVRamStats / traceFrame(). Backed by the free-list heap in
# renderer_core_gs_vram - see docs/gs-vram.md.
*/

#include <string>
#include <vector>
#include "renderer/core/texture/renderer_core_texture.hpp"
#include "debug/debug.hpp"

namespace Tyra {

// Modified by TyraX: the NAMED half of the VRAM instrument. VRAMSTAT counts
// re-uploads and evictions; it never says WHICH texture is cycling or what
// is holding the heap, and "12.17 re-uploads per frame" cannot be acted on
// until it does (docs/gs-vram.md, "The residency census"). This block keeps
// an id -> name map and the current frame's eviction victims, and
// traceFrame() prints both with the periodic summary.
//
// It lives entirely in this .cpp and adds NO field to any header, so the
// struct layouts are identical in both build profiles and the whole thing
// compiles to nothing with TYRA_VRAM_CENSUS at 0 - the devkit zero-cost
// rule. It is deliberately NOT keyed to NDEBUG: this engine never defines it
// (docs/devkit.md) applies to the instrument as much as to the devkit.
#if TYRA_VRAM_CENSUS
namespace {

struct VRamCensusName {
  u32 id;
  std::string name;
};

struct VRamCensusEvict {
  std::string victim;
  std::string incoming;
  int words;
};

std::vector<VRamCensusName> censusNames;
std::vector<VRamCensusEvict> censusEvicts;

const char* censusNameOf(const u32& id) {
  for (u32 i = 0; i < censusNames.size(); i++)
    if (censusNames[i].id == id) return censusNames[i].name.c_str();
  return "?";
}

void censusRemember(const Texture* tex) {
  for (u32 i = 0; i < censusNames.size(); i++)
    if (censusNames[i].id == tex->id) return;
  censusNames.push_back({tex->id, tex->name});
}

}  // namespace
#endif

RendererCoreTexture::RendererCoreTexture() {}

RendererCoreTexture::~RendererCoreTexture() {}

void RendererCoreTexture::beforeMutation() {
  if (mutationBarrier) mutationBarrier(mutationBarrierContext);
}

void RendererCoreTexture::init(RendererCoreGS* t_gs, Path3* t_path3) {
  gs = t_gs;
  sender.init(t_path3, t_gs);
  repository.init(&currentAllocations, this);
  path3 = t_path3;
  initClut();
}

// Modified by TyraX - see the header comment.
void RendererCoreTexture::freeTextureBuffers(const u32& texId) {
  auto allocated = getAllocatedBuffersByTextureId(texId);
  if (allocated.id == 0) return;  // never uploaded - nothing on the GS
  beforeMutation();
  // Modified by TyraX: residency counters. Freeing anything but the newest
  // allocation used to rewind the bump pointer under still-live textures;
  // the heap handles it now, but the counter stays as proof the path is
  // exercised (streaming layers hit it every unload).
  if (!currentAllocations.empty() &&
      currentAllocations.back().id != allocated.id)
    stats.unorderedFrees++;
  sender.deallocate(allocated);
  unregisterAllocation(texId);
}

// Modified by TyraX - see the header comment.
void RendererCoreTexture::evictAll() {
  if (!currentAllocations.empty()) beforeMutation();
  for (int i = currentAllocations.size() - 1; i >= 0; i--)
    sender.deallocate(currentAllocations[i]);
  currentAllocations.clear();
  updateClutBuffer(nullptr);
}

void RendererCoreTexture::updateClutBuffer(texbuffer_t* clutBuffer) {
  if (clutBuffer == nullptr || clutBuffer->width == 0) {
    clut.psm = 0;
    clut.load_method = CLUT_NO_LOAD;
    clut.address = 0;
  } else {
    clut.psm = clutBuffer->psm;
    clut.load_method = CLUT_LOAD;
    clut.address = clutBuffer->address;
  }
}

RendererCoreTextureBuffers RendererCoreTexture::useTexture(
    const Texture* t_tex) {
  TYRA_ASSERT(t_tex != nullptr, "Provided nullptr texture!");

  // Modified by TyraX: VRAM-resident textures (dynamic env map) bind their
  // own texbuffer - the pixels are rendered into GS memory, there is nothing
  // to upload and the allocation never enters the eviction lists.
  if (t_tex->vramResident != nullptr)
    return RendererCoreTextureBuffers{t_tex->id, t_tex->vramResident, nullptr,
                                      0};

  stats.binds++;  // Modified by TyraX: residency counters

  // Modified by TyraX: touch the residency entry in place - the returned
  // copy is a snapshot, the list entry is what eviction sorts on. (useSeq
  // wraps after ~9 hours of continuous play at a few thousand binds a frame;
  // the worst that costs is a couple of mis-chosen victims in one frame.)
  useSeq++;
  for (u32 i = 0; i < currentAllocations.size(); i++) {
    if (currentAllocations[i].id != t_tex->id) continue;
    currentAllocations[i].lastUsedSeq = useSeq;
    stats.hits++;
    return currentAllocations[i];
  }

  // Modified by TyraX: evict the coldest allocations until this one fits,
  // instead of dumping the entire resident set. The heap frees in any order
  // and coalesces, so the survivors keep their VRAM and their pixels.
  beforeMutation();
  makeRoomFor(t_tex);

  auto newTexBuffer = sender.allocate(t_tex);
  newTexBuffer.lastUsedSeq = useSeq;
#if TYRA_VRAM_CENSUS
  censusRemember(t_tex);  // Modified by TyraX: see the census block above.
#endif
  path3->sendTexture(t_tex, newTexBuffer);
  registerAllocation(newTexBuffer);

  stats.uploads++;
  bool seen = false;
  for (u32 i = 0; i < everUploaded.size(); i++)
    if (everUploaded[i] == t_tex->id) {
      seen = true;
      break;
    }
  if (seen)
    stats.reuploads++;
  else
    everUploaded.push_back(t_tex->id);

  return newTexBuffer;
}

// Modified by TyraX: pick the allocation to give up, in two tiers.
//
// Tier 1 - allocations that are genuinely STALE: not bound in this frame nor
// in the one before it. Coldest first (lowest bind sequence), ties broken by
// the bigger allocation since it frees more per re-upload we will owe. These
// are the textures that went off-screen or belong to a layer that was
// streamed out, and they are always the cheapest thing to give up.
//
// Tier 2 - every resident allocation is part of the live working set, i.e.
// what the scene draws genuinely does not fit. Here LRU is the *worst*
// possible policy: the scene re-binds its textures in the same order every
// frame, so "coldest" is precisely the one that will be requested first next
// frame - the whole set cycles and every texture re-uploads every frame.
// Evict the MOST recently bound one instead (scan-resistant MRU): the head
// of the scan stays resident and only the overflow tail keeps re-uploading.
// Measured on a scene just over budget (3x256 + 3x128 32bpp textures,
// ~1.08 MB of heap): 9-10 re-uploads/frame with the flush-everything policy,
// 3 with this one. On a scene MASSIVELY over budget (6x256, less than half
// of it resident at a time) nothing helps much - 10 before, 8 after.
//
// Note the tier-1 test spans TWO frames on purpose. "Not bound yet in this
// frame" is not evidence of coldness - the tail of last frame's scan has not
// come round again yet, and evicting it is a guaranteed miss a few binds
// later in the very same frame.
//
// Nothing outside this list can ever be picked: VRAM-resident textures (the
// env map / camera feed) never enter it, and the init-time buffers live
// below the heap floor where the allocator cannot hand them out.
int RendererCoreTexture::pickVictim() const {
  int victim = -1;
  u32 victimSeq = 0;
  int victimSize = 0;

  for (u32 i = 0; i < currentAllocations.size(); i++) {
    const u32 seq = currentAllocations[i].lastUsedSeq;
    if (seq >= prevFrameStartSeq) continue;  // still in the working set
    int size = gs->vram.getAllocationWords(currentAllocations[i].core->address);
    if (currentAllocations[i].clut != nullptr)
      size += gs->vram.getAllocationWords(currentAllocations[i].clut->address);
    if (victim < 0 || seq < victimSeq ||
        (seq == victimSeq && size > victimSize)) {
      victim = static_cast<int>(i);
      victimSeq = seq;
      victimSize = size;
    }
  }

  if (victim >= 0) return victim;

  for (u32 i = 0; i < currentAllocations.size(); i++) {
    const u32 seq = currentAllocations[i].lastUsedSeq;
    if (victim < 0 || seq > victimSeq) {
      victim = static_cast<int>(i);
      victimSeq = seq;
    }
  }

  return victim;
}

// Modified by TyraX: evict until one incoming texture fits.
void RendererCoreTexture::makeRoomFor(const Texture* t_tex) {
  const int coreWords = gs->vram.getSizeWords(*t_tex->core);
  const int clutWords = (t_tex->clut != nullptr && t_tex->clut->width > 0)
                            ? gs->vram.getSizeWords(*t_tex->clut)
                            : 0;

  if (gs->vram.canAllocatePair(coreWords, clutWords)) return;

  bool evictedAny = false;

  while (!currentAllocations.empty() &&
         !gs->vram.canAllocatePair(coreWords, clutWords)) {
    const int victim = pickVictim();
    if (victim < 0) break;
#if TYRA_VRAM_CENSUS
    // Modified by TyraX: name the victim and what it was given up for.
    if (censusEvicts.size() < 32) {
      int vw = gs->vram.getAllocationWords(
          currentAllocations[victim].core->address);
      if (currentAllocations[victim].clut != nullptr)
        vw += gs->vram.getAllocationWords(
            currentAllocations[victim].clut->address);
      censusEvicts.push_back(
          {censusNameOf(currentAllocations[victim].id), t_tex->name, vw});
    }
#endif
    sender.deallocate(currentAllocations[victim]);
    currentAllocations.erase(currentAllocations.begin() + victim);
    stats.evictions++;
    evictedAny = true;
  }

  if (evictedAny) stats.evictionEvents++;

  TYRA_ASSERT(gs->vram.canAllocatePair(coreWords, clutWords),
              "Texture does not fit in GS VRAM even with an empty heap!",
              "Heap words:", gs->vram.getHeapWords(),
              "Needed words:", coreWords + clutWords);
}

// Modified by TyraX: per-frame VRAM residency report. Compiled to almost
// nothing in release (TYRA_LOG is a no-op under NDEBUG); in a debug build it
// prints a VRAMSTAT line to the game's log.txt on every frame that evicted
// something, plus a summary every 120 frames so a quiet scene still shows its
// working set and free-VRAM low-water mark.
void RendererCoreTexture::traceFrame() {
  frameCounter++;
  // Binds from here on belong to the next frame.
  prevFrameStartSeq = frameStartSeq;
  frameStartSeq = useSeq + 1;

  stats.resident = currentAllocations.size();
  if (stats.resident > stats.peakResident) stats.peakResident = stats.resident;

  const float freeMB = gs->vram.getFreeSpaceInMB();
  if (freeMB < stats.minFreeMB) stats.minFreeMB = freeMB;

  const bool evicted = stats.evictions != lastLoggedEvictions;
  const bool summary = (frameCounter % 120) == 0;
#if TYRA_VRAM_CENSUS
  // Modified by TyraX: OPT-IN, and it has to be. `#if TYRA_VRAM_CENSUS` looked like
  // the devkit rule and is not: the engine's own Makefile defines NDEBUG for
  // one target only, so the native build never defines it and the census
  // shipped live in a RELEASE-profile game. Measured on a physical console it
  // wrote 1,016 host: lines in a 1,440-frame run and cost about 1 ms a frame -
  // and it was briefly mis-attributed to the vehicle quantization measured in
  // the same window, because both arms carried it. A debug channel that a
  // release build still pays for is the exact thing docs/devkit.md forbids.
  //
  // The census, with the periodic summary only. One line
  // per resident allocation answers "what is holding the heap", and the
  // victims answer "what is cycling, and for whom" - the two questions
  // VRAMSTAT's counters raise and cannot settle. A thrashing scene evicts
  // every frame, so printing the victims on every eviction frame would be
  // four lines a frame forever; the summary is enough to read a steady
  // state, and the counters still carry the rate.
  //
  // The victim list is cleared when it is PRINTED, never per frame. Clearing
  // it per frame made the instrument miss the one event it was built for:
  // a pause menu opened between two summaries evicted eight allocations and
  // the census reported none of them, because the eviction landed on a frame
  // whose list was thrown away eight frames later. A rare eviction is
  // exactly the interesting one.
  if (summary) {
    for (u32 i = 0; i < currentAllocations.size(); i++) {
      int w = gs->vram.getAllocationWords(currentAllocations[i].core->address);
      if (currentAllocations[i].clut != nullptr)
        w += gs->vram.getAllocationWords(currentAllocations[i].clut->address);
      TYRA_LOG("VRAMRES f=", frameCounter, " i=", (int)i, " words=", w,
               " kb=", w / 256, " name=", censusNameOf(currentAllocations[i].id));
    }
    for (u32 i = 0; i < censusEvicts.size(); i++)
      TYRA_LOG("VRAMEVICT f=", frameCounter, " words=", censusEvicts[i].words,
               " kb=", censusEvicts[i].words / 256,
               " victim=", censusEvicts[i].victim.c_str(),
               " for=", censusEvicts[i].incoming.c_str());
    censusEvicts.clear();
  }
#endif
  if (!evicted && !summary) return;
  lastLoggedEvictions = stats.evictions;

  TYRA_LOG("VRAMSTAT f=", frameCounter, " bind=", stats.binds, " (+",
           stats.binds - lastLogged.binds, ")", " hit=", stats.hits, " (+",
           stats.hits - lastLogged.hits, ")", " up=", stats.uploads, " (+",
           stats.uploads - lastLogged.uploads, ")", " reup=", stats.reuploads,
           " (+", stats.reuploads - lastLogged.reuploads, ")",
           " evict=", stats.evictions, " (+",
           stats.evictions - lastLogged.evictions, ")",
           " runs=", stats.evictionEvents, " res=", stats.resident,
           " peak=", stats.peakResident, " oofree=", stats.unorderedFrees,
           " freeMB=", freeMB, " minFreeMB=", stats.minFreeMB, " largestKB=",
           gs->vram.getLargestFreeWords() / 256);

  lastLogged = stats;
}

RendererCoreTextureBuffers RendererCoreTexture::updateTextureInfo(
    const Texture* t_tex) {
  TYRA_ASSERT(t_tex != nullptr, "Provided nullptr texture!");

  auto allocated = getAllocatedBuffersByTextureId(t_tex->id);
  TYRA_ASSERT(allocated.id != 0, "Can't update an unallocated texture!");

  beforeMutation();
  path3->sendTexture(t_tex, allocated);
  return allocated;
}

RendererCoreTextureBuffers RendererCoreTexture::getAllocatedBuffersByTextureId(
    const u32& t_id) {
  for (u32 i = 0; i < currentAllocations.size(); i++)
    if (currentAllocations[i].id == t_id) return currentAllocations[i];
  return {0, nullptr, nullptr, 0};
}

void RendererCoreTexture::registerAllocation(
    const RendererCoreTextureBuffers& t_buffers) {
  currentAllocations.push_back(t_buffers);
}

void RendererCoreTexture::unregisterAllocation(const u32& textureId) {
  // An id that is not registered used to leave foundIndex UNINITIALISED and
  // erase at whatever offset the stack happened to hold - a corrupted vector
  // or a crash, from a caller that merely asked twice. GCC 11 did not see it;
  // GCC 15 does, and with -Werror that is what stops the build.
  for (u32 i = 0; i < currentAllocations.size(); i++) {
    if (currentAllocations[i].id == textureId) {
      currentAllocations.erase(currentAllocations.begin() + i);
      return;
    }
  }

  TYRA_WARN("Texture allocation ", textureId, " was not registered.");
}

void RendererCoreTexture::initClut() {
  clut.storage_mode = CLUT_STORAGE_MODE1;
  clut.start = 0;
  clut.psm = 0;
  clut.load_method = CLUT_NO_LOAD;
  clut.address = 0;
  TYRA_LOG("Clut set!");
}

}  // namespace Tyra
