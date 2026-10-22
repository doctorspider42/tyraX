#pragma once
#include "tyrax2-hybrid-copy-adapter.hpp"
#ifdef __mips__
#include <kernel.h>
namespace Tyra {
class HybridCopyEE {
 public:
  using QueueProof = bool (*)(std::uint32_t);
  explicit HybridCopyEE(QueueProof proof) : queueProof(proof) {}
  bool enterCritical() { return DIntr() != 0; }
  void leaveCritical(bool restore) {
    asm volatile("sync.l" ::: "memory");
    if (restore) EIntr();
  }
  bool validPacket(const HybridCopyPacket& p) const {
    if (p.address < 0x100000 || p.address >= 0x02000000 || p.address % 64 ||
        p.qwords < 2 || p.qwords > 128 || p.qwords * 16 > 0x02000000 - p.address)
      return false;
    const auto* tail = reinterpret_cast<const std::uint64_t*>(p.address + (p.qwords - 2) * 16);
    return tail[0] == 0x1000000000008001ULL && tail[1] == 14 &&
           tail[2] == 1 && tail[3] == 0x61;
  }
  void publishPacket(const HybridCopyPacket& p) {
    SyncDCache(reinterpret_cast<void*>(p.address),
               reinterpret_cast<void*>(p.address + p.qwords * 16));
    asm volatile("sync.l" ::: "memory");
  }
  // Must bind the actual old running/completed sequence AND empty queue/no
  // newer submitted prefix. Raw CHCR idle by itself is insufficient.
  bool oldJobReady(std::uint32_t sequence) const {
    return queueProof && queueProof(sequence) && !(reg32(0x10009000) & 0x100);
  }
  bool gifIdle() const { return !(reg32(0x1000a000) & 0x100); }
  bool finishSet() const { return (reg64(0x12001000) & 2) != 0; }
  void clearFinish() {
    reg64(0x12001000) = 2;
    asm volatile("sync.l" ::: "memory");
  }
  void startCopy(const HybridCopyPacket& p) {
    reg32(0x1000a010) = static_cast<std::uint32_t>(p.address);
    reg32(0x1000a020) = p.qwords;
    asm volatile("sync.l" ::: "memory");
    reg32(0x1000a000) = 0x101;
  }
 private:
  static volatile std::uint32_t& reg32(std::uintptr_t address) {
    return *reinterpret_cast<volatile std::uint32_t*>(address);
  }
  static volatile std::uint64_t& reg64(std::uintptr_t address) {
    return *reinterpret_cast<volatile std::uint64_t*>(address);
  }
  QueueProof queueProof;
};
}  // namespace Tyra
#endif
