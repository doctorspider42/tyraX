/* Modified by TyraX: frame-owned snapshots of complete DMA chains and REFs.
 * Licensed under Apache 2.0. See docs/tyrax2.md. */
#pragma once
#include <string.h>
#include "debug/hardware_trace.hpp"
#include "vif1_chain_check.hpp"

#ifndef TYRA_FRAME_PIPELINE_SUPPORT
#define TYRA_FRAME_PIPELINE_SUPPORT 1
#endif

#ifndef TYRA_FRAME_CHAIN_ARENA
#define TYRA_FRAME_CHAIN_ARENA 0
#endif
#ifndef TYRA_FRAME_CHAIN_ARENA_BYTES
#define TYRA_FRAME_CHAIN_ARENA_BYTES 1048576
#endif
#ifndef TYRA_FRAME_ARENA_QUAD_COPY
#define TYRA_FRAME_ARENA_QUAD_COPY 1
#endif

namespace Tyra {
// Disjoint engine-owned ranges, sorted for bounded logarithmic lookup on the
// per-REF path. Owners must fence readers before remove().
class ImmutableSpanTable {
 public:
  bool add(uint32_t base, uint32_t qwords) {
    if (!base || (base & 15) || !qwords || qwords > (UINT32_MAX - base) / 16 ||
        count == 512) return false;
    uint32_t at = lowerBound(base);
    if ((at && spans[at-1].base + spans[at-1].qwords * 16 > base) ||
        (at < count && base + qwords * 16 > spans[at].base)) return false;
    for (uint32_t i = count; i > at; --i) spans[i] = spans[i-1];
    spans[at] = {base,qwords,0,false}; ++count; return true;
  }
  bool contains(uint32_t address, uint32_t qwords) const {
    const uint32_t at = lowerBound(address);
    const Span* span = at < count && spans[at].base == address ? &spans[at] :
                       at ? &spans[at-1] : nullptr;
    return span && !span->retired && address >= span->base &&
           address - span->base <= span->qwords * 16 &&
           qwords <= (span->qwords * 16 - (address - span->base)) / 16;
  }
  bool borrow(uint32_t address, uint32_t qwords, uint32_t bankMask) {
    if (!contains(address, qwords) || !bankMask || (bankMask & ~3u)) return false;
    const uint32_t at = lowerBound(address);
    Span& span = at < count && spans[at].base == address ? spans[at] : spans[at-1];
    span.readers |= bankMask;
    return true;
  }
  void retire(uint32_t base) {
    const uint32_t at = lowerBound(base);
    if (at < count && spans[at].base == base) spans[at].retired = true;
  }
  bool reclaimable(uint32_t base) const {
    const uint32_t at = lowerBound(base);
    return at == count || spans[at].base != base || !spans[at].readers;
  }
  void releaseBanks(uint32_t bankMask) {
    for (uint32_t i = 0; i < count; ++i) spans[i].readers &= ~bankMask;
  }
  bool collectRetired(uint32_t base) {
    const uint32_t at = lowerBound(base);
    if (at == count || spans[at].base != base) return true;
    if (!spans[at].retired || spans[at].readers) return false;
    remove(base);
    return true;
  }
  bool hasBase(uint32_t address) const {
    const uint32_t at = lowerBound(address);
    return at < count && spans[at].base == address;
  }
  void remove(uint32_t address) {
    const uint32_t at = lowerBound(address);
    if (at == count || spans[at].base != address) return;
    for (uint32_t i = at; i + 1 < count; ++i) spans[i] = spans[i+1];
    --count;
  }
 private:
  struct Span {
    uint32_t base = 0, qwords = 0, readers = 0;
    bool retired = false;
  };
  Span spans[512]; uint32_t count = 0;
  uint32_t lowerBound(uint32_t address) const {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      if (spans[mid].base < address) lo = mid + 1; else hi = mid;
    }
    return lo;
  }
};
// Allocation and completion belong to the caller. No allocation occurs here;
// reset is legal only after every submitted reader of this storage completes.
class FrameChainArena {
 public:
  struct Snapshot { void* chain = nullptr; uint32_t bytes = 0, borrowedBytes = 0; };
  // Only an owner that fences before mutation/destruction may offer a span.
  // This callback is stable throughout a copy, like the readable resolver.
  using ImmutableResolver = bool (*)(uint32_t address, uint32_t qwords);
  FrameChainArena(void* memory, uint32_t bytes)
      : memory(static_cast<uint8_t*>(memory)), capacity(bytes) {}
  uint32_t used() const { return cursor; }
  void reset() { cursor = 0; }

  Snapshot copy(const void* base, uint32_t qwords,
                Vif1ChainCheck::Resolver resolver, void* context,
                uint32_t dmaBase, ImmutableResolver immutable = nullptr) {
    HardwareTrace::Scope traceSnapshot("Snapshot", HardwareTrace::Kind::Span, true);
    if (!memory || !base || !qwords || qwords > 65536 ||
        (reinterpret_cast<uintptr_t>(base) & 15) ||
        (reinterpret_cast<uintptr_t>(memory) & 15) || (dmaBase & 15)) return {};
    const auto* words = static_cast<const uint32_t*>(base);
    uint32_t at = 0, needed = qwords * 16;
    uint32_t payloadQwords = 0;
    bool ended = false;
    // Preflight all ranges and space before writing anything. The original
    // source stays owned by the synchronous caller throughout both passes.
    { HardwareTrace::Scope trace("SnapshotPreflight", HardwareTrace::Kind::Span, true);
    while (at < qwords) {
      const auto* tag = words + at * 4;
      const uint32_t size = tag[0] & 65535, id = (tag[0] >> 28) & 7;
      if (tag[0] & 0x8c000000u) return {};
      if (size > 1048576 - payloadQwords) return {};
      payloadQwords += size;
      if (id == 1 || id == 7) {
        if (size > qwords - at - 1) return {};
        at += size + 1;
      } else if (id == 0 || id == 3) {
        if (size && ((tag[1] & 0x8000000f) || !resolver ||
                     !resolver(tag[1], size, context))) return {};
        if (size && !(immutable && immutable(tag[1], size))) {
          if (size * 16 > capacity || needed > capacity - size * 16) return {};
          needed += size * 16;
        }
        ++at;
      } else return {};
      if (id == 0 || id == 7) { ended = true; break; }
    }
    if (!ended || at != qwords || cursor > capacity ||
        needed > capacity - cursor || dmaBase > UINT32_MAX - cursor ||
        needed > UINT32_MAX - (dmaBase + cursor)) return {};
    }
    uint8_t* target = memory + cursor;
    { HardwareTrace::Scope trace("SnapshotChainCopy", HardwareTrace::Kind::Span, true);
      if (!copyQwords(target, base, qwords)) return {}; }
    uint32_t payload = qwords * 16;
    uint32_t borrowed = 0;
    at = 0;
    { HardwareTrace::Scope trace("SnapshotFixup", HardwareTrace::Kind::Span, true);
    while (at < qwords) {
      const auto* source = words + at * 4;
      auto* tag = reinterpret_cast<uint32_t*>(target) + at * 4;
      const uint32_t size = source[0] & 65535, id = (source[0] >> 28) & 7;
      if (id == 1 || id == 7) at += size + 1;
      else {
        if (size && immutable && immutable(source[1], size)) {
          borrowed += size * 16;
        } else if (size) {
          const auto* ref = resolver(source[1], size, context);
          if (!ref) return {}; // resolver contract must be stable
          { HardwareTrace::Scope trace("SnapshotMutableCopy", HardwareTrace::Kind::Span, true);
            if (!copyQwords(target + payload, ref, size)) return {}; }
          tag[1] = dmaBase + cursor + payload;
          payload += size * 16;
        }
        ++at;
      }
    }
    }
    cursor += needed;
    return {target, needed, borrowed};
  }

 public:
  static bool copyQwords(void* destination, const void* source, uint32_t count) {
#if TYRA_VIF1_CHAIN_CHECK
    const uint32_t bytes = count * 16;
#endif
#if defined(_EE) && TYRA_FRAME_ARENA_QUAD_COPY
    // EE libc memcpy is a byte/word copy. All arena spans are 16-byte aligned;
    // use the R5900's full quadword load/store width, without claiming SPR or
    // any DMA channel owned by a custom program. Cache write-back remains the
    // queue's existing obligation before submission starts.
    auto* dst = static_cast<uint8_t*>(destination);
    const auto* src = static_cast<const uint8_t*>(source);
    while (count >= 4) {
      asm volatile("lq $8, 0(%0)\n\tlq $9, 16(%0)\n\t"
                   "lq $10, 32(%0)\n\tlq $11, 48(%0)\n\t"
                   "sq $8, 0(%1)\n\tsq $9, 16(%1)\n\t"
                   "sq $10, 32(%1)\n\tsq $11, 48(%1)"
                   : : "r"(src), "r"(dst) : "$8", "$9", "$10", "$11", "memory");
      src += 64; dst += 64; count -= 4;
    }
    while (count--) {
      asm volatile("lq $8, 0(%0)\n\tsq $8, 0(%1)"
                   : : "r"(src), "r"(dst) : "$8", "memory");
      src += 16; dst += 16;
    }
#else
    memcpy(destination, source, count * 16);
#endif
#if TYRA_VIF1_CHAIN_CHECK
    // The host uses memcpy; this also verifies the actual R5900 assembly arm
    // byte-for-byte on the console. A mismatch retains ordinary submission.
    return memcmp(destination, source, bytes) == 0;
#else
    return true;
#endif
  }
 private:
  uint8_t* memory;
  uint32_t capacity;
  uint32_t cursor = 0;
};
} // namespace Tyra
