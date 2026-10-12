#include "tyrax2-world-cell-sdk.hpp"

// Force all actual SDK/compiler calls into the real EE object. No mock emitter.
WorldCellSdk::Cell compileWorldCellSdk(
    const WorldCellPlan::Cell& cell,
    const std::vector<WorldCellSdk::Binding>& bindings,
    std::uint64_t budget) {
  return WorldCellSdk::compile(cell,bindings,budget);
}
