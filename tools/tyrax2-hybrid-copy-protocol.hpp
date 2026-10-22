#pragma once
#include <cstdint>

// Private protocol model. A target adapter must serialize main/ISR transitions
// and publish immutable DMA data before arm(); this class does neither itself.
namespace Tyra {
class HybridCopyProtocol {
 public:
  enum class Stage { Idle, Armed, Withdrawn, Copying, Done };
  struct Ready {
    std::uint32_t job;
    bool oldDmaDone, exclusiveFinish, gifIdle, prefixUnsubmitted, packetPublished;
  };
  explicit HybridCopyProtocol(std::uint32_t initialField = 0) : field(initialField) {}
  bool blankStart() {
    if (blankOpen) return false;
    ++field;
    blankOpen = true;
    return true;
  }
  bool blankEnd() {
    if (!blankOpen) return false;
    if (auditLive && auditField == field && !auditComplete) ++lateCopies;
    auditLive = false;
    blankOpen = false;
    return true;
  }
  bool arm(std::uint32_t sequence, std::uint32_t packetVersion,
           bool actuallySubmitted, bool immutablePublished) {
    if (stage != Stage::Idle || !actuallySubmitted || !immutablePublished)
      return false;
    job = sequence;
    packet = packetVersion;
    stage = Stage::Armed;
    return true;
  }
  bool claimISR(const Ready& ready, std::uint32_t packetVersion) {
    if (stage != Stage::Armed || !canCopy(ready, packetVersion)) return false;
    startCopy();
    return true;
  }
  bool withdraw(std::uint32_t sequence) {
    if (stage != Stage::Armed || sequence != job) return false;
    withdrawnField = field;
    stage = Stage::Withdrawn;
    return true;
  }
  bool claimFallback(const Ready& ready, std::uint32_t packetVersion) {
    if (stage != Stage::Withdrawn || field == withdrawnField ||
        !canCopy(ready, packetVersion)) return false;
    startCopy();
    return true;
  }
  bool finish(std::uint32_t sequence, bool copyDmaDone, bool copyFinishOwned) {
    if (stage != Stage::Copying || sequence != job || !copyDmaDone ||
        !copyFinishOwned) return false;
    stage = Stage::Done;
    if (auditLive && auditField == field) auditComplete = true;
    return true;
  }
  bool release(std::uint32_t sequence) {
    if (stage != Stage::Done || sequence != job) return false;
    stage = Stage::Idle;
    return true;
  }
  bool sourceProtected() const { return stage != Stage::Idle; }
  Stage state() const { return stage; }
  unsigned late() const { return lateCopies; }

 private:
  bool canCopy(const Ready& r, std::uint32_t version) const {
    return blankOpen && (!claimedAny || claimedField != field) && r.job == job &&
           version == packet && r.oldDmaDone && r.exclusiveFinish && r.gifIdle &&
           r.prefixUnsubmitted && r.packetPublished;
  }
  void startCopy() {
    claimedAny = auditLive = true;
    claimedField = auditField = field;
    auditComplete = false;
    stage = Stage::Copying;
  }
  volatile Stage stage = Stage::Idle;
  volatile std::uint32_t job = 0, packet = 0, field = 0, claimedField = 0, auditField = 0;
  volatile std::uint32_t withdrawnField = 0;
  volatile bool blankOpen = false, claimedAny = false, auditLive = false, auditComplete = false;
  volatile unsigned lateCopies = 0;
};
}  // namespace Tyra
