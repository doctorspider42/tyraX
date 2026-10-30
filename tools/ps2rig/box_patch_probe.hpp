// Fixture-only accounting; no per-contact logging or dynamic allocation.
#pragma once
#include <cstdio>
#include <vector>
namespace BoxPatch {
inline bool active = false;
struct Row { unsigned frame, active, tested, patches, points, fallback; };
inline Row current{};
inline std::vector<Row> rows;
inline void begin(bool enabled, unsigned frame) {
  active = enabled; current = {frame, unsigned(active), 0, 0, 0, 0};
}
inline void finish() { rows.push_back(current); }
inline void flush(int phase) {
  char name[80]; std::snprintf(name, sizeof(name), "box-patch-%d.csv", phase);
  FILE* f = std::fopen(name, "w");
  if (f) {
    std::fprintf(f, "frame,active,tested,patches,points,fallback\n");
    for (const auto& r : rows)
      std::fprintf(f, "%u,%u,%u,%u,%u,%u\n", r.frame, r.active,
                   r.tested, r.patches, r.points, r.fallback);
    std::fclose(f);
  }
  std::printf("BOXPATCH phase=%d frames=%u file=%d\n", phase,
              unsigned(rows.size()), f != nullptr);
  std::fflush(stdout); rows.clear();
}
} // namespace BoxPatch
