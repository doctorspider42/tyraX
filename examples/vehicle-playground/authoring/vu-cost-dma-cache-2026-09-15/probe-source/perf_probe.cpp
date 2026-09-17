// Modified by TyraX: see the header. One startup read, no allocation, no I/O
// afterwards.
#include "debug/perf_probe.hpp"
#include "file/file_utils.hpp"
#include <cstdio>

namespace Tyra { namespace PerfProbe {

u32 extraFlushes = 0;
bool suppressFlush = false;

void configure() {
  FILE* f = fopen(FileUtils::fromCwd("perf-probe.cfg").c_str(), "r");
  if (!f) return;
  unsigned flushes = 0, suppress = 0;
  const int n = fscanf(f, "%u %u", &flushes, &suppress);
  fclose(f);
  if (n == 1) suppress = 0;
  else if (n != 2 || suppress > 1) return;
  // A malformed or absurd file leaves the control configuration in place; an
  // arm that silently measured something else would be worse than no arm.
  if (flushes > 64) return;
  extraFlushes = flushes;
  suppressFlush = suppress != 0;
}

} }  // namespace Tyra::PerfProbe
