#pragma once
#include <cstddef>
#include <cstdint>
#include "tyrax2-hybrid-copy-protocol.hpp"

namespace Tyra {
struct HybridCopyPacket {
  std::uintptr_t address;
  std::uint32_t qwords, version;
};

// One main producer; main transitions mask IRQs. ISR entry points must be called
// only from their matching interrupt. Core/queue integration is still required.
template <class Hardware> class HybridCopyAdapter {
 public:
  enum class MainAction { Unarmed, Fallback, WaitCopy, CopyDone, WrongJob };
  explicit HybridCopyAdapter(Hardware& hardware) : hw(hardware) {}
  bool armMain(std::uint32_t sequence, HybridCopyPacket source, bool submitted) {
    Critical critical(hw);
    if (!sequence || !submitted || protocol.sourceProtected() ||
        !hw.validPacket(source)) return false;
    hw.publishPacket(source);
    packet = source;
    job = sequence;
    oldConsumed = false;
    return protocol.arm(job, packet.version, true, true);
  }
  bool blankStartISR() {
    if (!protocol.blankStart()) return false;
    return nativeReadyISR();
  }
  // A native consumer may finish after START. Every notification rechecks the
  // hardware budget; protocol ownership still excludes main withdrawal/reuse.
  bool nativeReadyISR() {
    if (protocol.state() != Stage::Armed) return false;
    const bool bound = hw.oldJobReady(job);
    typename HybridCopyProtocol::Ready ready{
        job, bound, hw.finishSet(), hw.gifIdle(), bound, true};
    if (!hw.copyWindowReady()) return false;
    if (!protocol.claimISR(ready, packet.version)) return false;
    hw.clearFinish();
    oldConsumed = true;
    hw.startCopy(packet);
    return true;
  }
  bool blankEndISR() {
    finishIfReady();
    return protocol.blankEnd();
  }
  // GS FINISH IRQ may precede GIF STR clear. Poll once, never wait in ISR;
  // the owner masks that IRQ and main/blank-end may complete it later.
  bool copyFinishISR() {
    finishIfReady();
    return protocol.state() == Stage::Done;
  }
  MainAction takeMain(std::uint32_t sequence) {
    Critical critical(hw);
    if (!protocol.sourceProtected()) return MainAction::Unarmed;
    if (sequence != job) return MainAction::WrongJob;
    if (protocol.withdraw(sequence)) return MainAction::Fallback;
    if (protocol.state() == Stage::Withdrawn) return MainAction::Fallback;
    finishIfReady();
    return protocol.state() == Stage::Done ? MainAction::CopyDone : MainAction::WaitCopy;
  }
  // Poll only, no wait. The caller completes the old VIF sequence normally;
  // this consumes precisely the old FINISH before its fresh-VBlank fallback.
  bool consumeOldMain(std::uint32_t sequence) {
    Critical critical(hw);
    if (sequence != job || protocol.state() != Stage::Withdrawn) return false;
    if (oldConsumed) return true;
    if (!hw.oldJobReady(job) || !hw.gifIdle() || !hw.finishSet()) return false;
    hw.clearFinish();
    oldConsumed = true;
    return true;
  }
  bool fallbackMain(std::uint32_t sequence) {
    Critical critical(hw);
    if (sequence != job || !oldConsumed) return false;
    const bool bound = hw.oldJobReady(job);
    typename HybridCopyProtocol::Ready ready{
        job, bound, true, hw.gifIdle(), bound, true};
    if (!hw.copyWindowReady()) return false;
    if (!protocol.claimFallback(ready, packet.version)) return false;
    // Clear a stray event before assigning FINISH ownership to this copy.
    hw.clearFinish();
    hw.startCopy(packet);
    return true;
  }
  bool finishMain(std::uint32_t sequence) {
    Critical critical(hw);
    if (sequence != job) return false;
    finishIfReady();
    return protocol.state() == Stage::Done;
  }
  bool releaseMain(std::uint32_t sequence) {
    Critical critical(hw);
    return protocol.release(sequence);
  }
  bool sourceProtected() const { return protocol.sourceProtected(); }
  unsigned late() const { return protocol.late(); }

 private:
  using Stage = HybridCopyProtocol::Stage;
  struct Critical {
    Hardware& hw;
    bool restore;
    explicit Critical(Hardware& h) : hw(h), restore(h.enterCritical()) {}
    ~Critical() { hw.leaveCritical(restore); }
    Critical(const Critical&) = delete;
    Critical& operator=(const Critical&) = delete;
  };
  void finishIfReady() {
    if (protocol.state() != Stage::Copying || !oldConsumed ||
        !hw.gifIdle() || !hw.finishSet()) return;
    hw.clearFinish();
    protocol.finish(job, true, true);
  }
  Hardware& hw;
  HybridCopyProtocol protocol;
  HybridCopyPacket packet{};
  volatile std::uint32_t job = 0;
  volatile bool oldConsumed = false;
};
}  // namespace Tyra
