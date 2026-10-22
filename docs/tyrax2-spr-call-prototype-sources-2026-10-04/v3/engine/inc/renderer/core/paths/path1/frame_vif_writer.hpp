/* Modified by TyraX: native bounded recording of one complete TTE chain.
 * Licensed under Apache 2.0. See docs/tyrax2.md. */
#pragma once
#include <string.h>
#include "spr_transaction_storage.hpp"
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
    if (storage && !storage->rewind(checkpoint)) return false;
    if (mirror && !mirror->rewind(checkpoint)) mirrorOkay = false;
    cursor = checkpoint; return true;
  }
  void reset() { cursor = 0; closed = false; if (storage) storage->reset(); if (mirror) mirror->reset(); mirrorOkay = true; }
  void setMirror(FrameVifWriter* value) { mirror = value; mirrorOkay = true; }
  bool mirrorValid() const { return mirrorOkay; }
  bool setStorage(TransactionVifStorage* value) {
    if (cursor || closed) return false;
    storage = value; if (storage) storage->reset(); return true;
  }
  bool inlineVif(uint32_t first, uint32_t second, const void* data,
                 uint32_t qwords) {
    if (qwords > 65535 || (qwords && (!data ||
        (reinterpret_cast<uintptr_t>(data) & 15))) || !room(1 + qwords)) return false;
    if (storage) {
      alignas(16) uint32_t tag[4]; writeTag(tag, 1, qwords, 0, first, second);
      if (!storage->append(tag, 1) || (qwords && !storage->append(data, qwords))) {
        storage->rewind(cursor); return false;
      }
    } else {
      uint32_t* tag = words + cursor * 4;
      writeTag(tag, 1, qwords, 0, first, second);
      if (qwords && !FrameChainArena::copyQwords(tag + 4, data, qwords)) return false;
    }
    if (mirror && !mirror->inlineVif(first, second, data, qwords)) mirrorOkay = false;
    cursor += 1 + qwords;
    return true;
  }
  bool referencedVif(uint32_t first, uint32_t second, uint32_t address,
                     uint32_t qwords, Vif1ChainCheck::Resolver readable,
                     void* context = nullptr) {
    if (qwords > 65535 || !room(1) || (qwords && ((address & 0x8000000f) ||
        !readable || !readable(address, qwords, context)))) return false;
    if (!emitTag(3, qwords, address, first, second)) return false;
    if (mirror && !mirror->referencedVif(first, second, address, qwords, readable, context)) mirrorOkay = false;
    ++cursor; return true;
  }
  bool calledVif(uint32_t address) {
    if ((address & 0x8000000f) || !address || !room(1)) return false;
    if (!emitTag(5, 0, address, 0, 0)) return false;
    if (mirror && !mirror->calledVif(address)) mirrorOkay = false;
    ++cursor; return true;
  }
  // Exactly one END is generated here, including its clean TTE NOP words.
  uint32_t finish() {
    if (closed) return cursor;
    if (!valid() || cursor >= capacity) return 0;
    if (!emitTag(7, 0, 0, 0, 0)) return 0;
    if (storage && !storage->finish()) { storage->rewind(cursor); return 0; }
    ++cursor; closed = true;
    if (mirror && mirror->finish() != cursor) mirrorOkay = false;
    return cursor;
  }
  const void* data() const { return words; }

 private:
  bool emitTag(uint32_t id, uint32_t qwords, uint32_t address,
               uint32_t first, uint32_t second) {
    if (storage) {
      alignas(16) uint32_t tag[4]; writeTag(tag, id, qwords, address, first, second);
      return storage->append(tag, 1);
    }
    writeTag(words + cursor * 4, id, qwords, address, first, second); return true;
  }
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
  FrameVifWriter* mirror = nullptr;
  bool mirrorOkay = true;
  TransactionVifStorage* storage = nullptr;
  uint32_t* words;
  uint32_t capacity, cursor = 0;
  bool closed = false;
};
} // namespace Tyra
