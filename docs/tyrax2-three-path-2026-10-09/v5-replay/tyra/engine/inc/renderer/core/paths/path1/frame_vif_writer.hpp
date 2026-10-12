/* Modified by TyraX: native bounded recording of one complete TTE chain.
 * Licensed under Apache 2.0. See docs/tyrax2.md. */
#pragma once
#include <string.h>
#include "vif1_chain_check.hpp"
#include "frame_chain_arena.hpp"

namespace Tyra {
// Records VIF operations from the beginning; never splices END/NEXT links
// between already-built DMA buffers. The owner retains every REF lease until
// the final DMA fence and only resets this storage after that fence completes.
class FrameVifWriter {
 public:
  FrameVifWriter(void* memory, uint32_t capacityQwords)
      : words(static_cast<uint32_t*>(memory)), capacity(capacityQwords) {}
  uint32_t checkpoint() const { return cursor; }
  uint32_t availableQwords() const {
    return valid() && !closed && cursor < capacity ? capacity - cursor - 1 : 0;
  }
  bool rewind(uint32_t checkpoint) {
    if (closed || checkpoint > cursor) return false;
    cursor = checkpoint; return true;
  }
  void reset() { cursor = 0; closed = false; }
  bool inlineVif(uint32_t first, uint32_t second, const void* data,
                 uint32_t qwords) {
    if (qwords > 65535 || (qwords && (!data ||
        (reinterpret_cast<uintptr_t>(data) & 15))) || !room(1 + qwords)) return false;
    uint32_t* tag = words + cursor * 4;
    writeTag(tag, 1, qwords, 0, first, second);
    if (qwords && !FrameChainArena::copyQwords(tag + 4, data, qwords)) return false;
    cursor += 1 + qwords;
    return true;
  }
  bool referencedVif(uint32_t first, uint32_t second, uint32_t address,
                     uint32_t qwords, Vif1ChainCheck::Resolver readable,
                     void* context = nullptr) {
    if (qwords > 65535 || !room(1) || (qwords && ((address & 0x8000000f) ||
        !readable || !readable(address, qwords, context)))) return false;
    writeTag(words + cursor * 4, 3, qwords, address, first, second);
    ++cursor; return true;
  }
  // Exactly one END is generated here, including its clean TTE NOP words.
  uint32_t finish() {
    if (closed) return cursor;
    if (!valid() || cursor >= capacity) return 0;
    writeTag(words + cursor * 4, 7, 0, 0, 0, 0);
    ++cursor; closed = true; return cursor;
  }
  const void* data() const { return words; }

 private:
  bool valid() const {
    return words && !(reinterpret_cast<uintptr_t>(words) & 15) && capacity;
  }
  bool room(uint32_t qwords) const {
    // Always reserve the eventual END. Failed recording leaves prior records
    // unchanged, permitting a caller to rewind the whole operation and flush.
    return valid() && !closed && cursor < capacity && qwords < capacity - cursor;
  }
  static void writeTag(uint32_t* tag, uint32_t id, uint32_t qwords,
                       uint32_t address, uint32_t first, uint32_t second) {
    tag[0] = (id << 28) | qwords; tag[1] = address;
    tag[2] = first; tag[3] = second;
  }
  uint32_t* words;
  uint32_t capacity, cursor = 0;
  bool closed = false;
};
} // namespace Tyra
