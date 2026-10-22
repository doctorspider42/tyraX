#pragma once
#include <cstdint>

namespace Tyra {
struct HybridWindowBounds {
  std::uint32_t epoch;
  std::uint32_t minBlank;
  std::uint32_t maxField;
  std::uint32_t minActiveDelta;
  std::uint32_t maxActiveDelta;
  std::uint32_t copyAndDispatchBudget;
};

struct HybridWindowSample {
  std::uint32_t epoch;
  std::uint32_t startModeBefore, endModeBefore;
  std::uint32_t startAgeBefore, endAge, startAgeAfter;
  std::uint32_t startModeAfter, endModeAfter;
};

// Private component only: bounds/epoch must come from qualified physical
// ownership, video timing and original-copy budget, never handler timestamps.
inline bool hybridCopyWindowReady(const HybridWindowBounds& b,
                                  const HybridWindowSample& s) {
  constexpr std::uint32_t startMode = 0x9e;  // BUSCLK/256, VBlank gate mode1.
  constexpr std::uint32_t endMode = 0xae;    // Same clock, gate mode2.
  constexpr std::uint32_t statusFlags = 0xc00;
  if (!b.epoch || s.epoch != b.epoch || !b.minBlank ||
      b.maxField >= 65536 || b.maxField <= b.minBlank ||
      !b.copyAndDispatchBudget || b.copyAndDispatchBudget >= b.minBlank ||
      !b.minActiveDelta || b.minActiveDelta > b.maxActiveDelta ||
      b.maxActiveDelta >= b.maxField)
    return false;
  if ((s.startModeBefore & ~statusFlags) != startMode ||
      (s.startModeAfter & ~statusFlags) != startMode ||
      (s.endModeBefore & ~statusFlags) != endMode ||
      (s.endModeAfter & ~statusFlags) != endMode)
    return false;
  if (s.startAgeBefore > s.startAgeAfter || s.startAgeAfter >= b.maxField ||
      s.endAge >= b.maxField || s.endAge <= s.startAgeAfter)
    return false;
  const auto activeDelta = s.endAge - s.startAgeAfter;
  if (activeDelta < b.minActiveDelta || activeDelta > b.maxActiveDelta)
    return false;
  // Subtract instead of adding an untrusted budget: no unsigned overflow.
  return s.startAgeAfter < b.minBlank &&
         b.copyAndDispatchBudget < b.minBlank - s.startAgeAfter;
}
}  // namespace Tyra
