/* Modified by TyraX: private direct-SPR transactional prefix storage. */
#pragma once
#include <stdint.h>
#include <string.h>

namespace Tyra {
// No DMA consumer may observe the destination until finish. Rollback changes
// the logical committed extent; abandoned bytes beyond it remain unreachable.
class TransactionVifStorage {
 public:
  virtual ~TransactionVifStorage() = default;
  virtual void reset() = 0;
  virtual bool append(const void*, uint32_t) = 0;
  virtual bool rewind(uint32_t) = 0;
  virtual bool finish() = 0;
};
template<class Backend>
class SprTransactionStorage : public TransactionVifStorage {
 public:
  SprTransactionStorage(Backend& backend, void* window, void* destination,
                        uint32_t capacity)
      : io(backend), scratch(static_cast<uint8_t*>(window)),
        output(static_cast<uint8_t*>(destination)), capacity(capacity) {}
  void reset() override { cursor = page = used = 0; closed = false; }
  bool append(const void* source, uint32_t qwords) override {
    if (closed || !scratch || !output || !source || qwords > capacity - cursor)
      return false;
    auto* src = static_cast<const uint8_t*>(source);
    while (qwords) {
      const uint32_t count = qwords < 1024 - used ? qwords : 1024 - used;
      io.copy(scratch + used * 16, src, count);
      used += count; cursor += count; src += count * 16; qwords -= count;
      if (used == 1024) commit();
    }
    return true;
  }
  bool rewind(uint32_t checkpoint) override {
    if (closed || checkpoint > cursor) return false;
    if (checkpoint >= page) {
      used = checkpoint - page;
    } else {
      page = checkpoint & ~uint32_t(1023);
      used = checkpoint - page;
      // The abandoned earlier page was fully committed synchronously. Reload
      // its live prefix through the backend's uncached destination read path.
      if (used) io.load(scratch, output + page * 16, used);
    }
    cursor = checkpoint; return true;
  }
  bool finish() override {
    if (closed) return true;
    if (used) commit();
    closed = true; return true;
  }
 private:
  void commit() {
    // transfer blocks to completion; the backend may diagnose and stop on
    // timeout, but cannot return with a live transfer and permit scratch reuse.
    io.transfer(scratch, output + page * 16, used);
    page += used; used = 0;
  }
  Backend& io;
  uint8_t* scratch;
  uint8_t* output;
  uint32_t capacity, cursor = 0, page = 0, used = 0;
  bool closed = false;
};
} // namespace Tyra
