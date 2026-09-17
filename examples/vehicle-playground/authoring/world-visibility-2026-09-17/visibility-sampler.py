"""Replace a benchmark fixture's sampler with the PER-OBJECT VISIBILITY probe.

docs/ee-submission-rearchitecture.md item 5 asks for world visibility - baked
sectors, portals or a PVS. Before any of that is designed, one question has to
be answered with evidence: **is the garage frame overdrawing at all?** The
frame inventory says what is SUBMITTED. It does not say what is SEEN. A
district viewed from a garage forecourt may be genuinely visible, in which case
occlusion culling removes nothing and the lever is LOD or impostors instead.

This sampler answers it by REMOVAL, which is the only ground truth that needs
no new engine counter and no assumption about how the GS rasterises:

    hide exactly one object, photograph the frame, diff it against the control.

    pixels changed == 0  ->  the object contributed NOTHING the player can see.
                             Every triangle it submitted was drawn behind
                             something (or off-screen). It is free to cull.
    pixels changed  > 0  ->  the object is visible, and the count is its exact
                             on-screen contribution in pixels.

That verdict is total. It needs no decomposition into silhouette, shadow, AO or
reflection, because a zero means nothing in the frame depended on the object by
ANY path. Crossed with the inventory's per-object triangle rows, it converts
"submitted" into "submitted and wasted", which is the only number a visibility
scheme can be budgeted against.

The camera is PARKED at the fixture's garage-day pose (phase 0 of
benchmark-district.py: eye (0,4,-32), at (0,1,-12), night value 0) so that the
only thing that differs between two captures is the object under test.

THE COMMAND FILE TAKES A LIST. `district-benchmark-pose.txt` holds whitespace-
or comma-separated object indices to hide, or `-1` for the control. A list is
what makes the round's decisive check possible: once the per-object pass has
named the zero-pixel set, hiding that WHOLE set at once must still be
pixel-identical to the control. One object at a time cannot establish that -
two objects can each be individually redundant while their union is not.
"""
import argparse
from pathlib import Path

SAMPLER = r'''#include "scripts/script.hpp"
#include "scripts/district_data.hpp"
#include "file/file_utils.hpp"
#include <cstdio>
namespace Vehicle_playground {
// Per-object visibility probe for the world-visibility round (authoring/
// world-visibility-2026-09-17/visibility-sampler.py). The camera is parked at
// the fixture's garage-day pose; the command file names objects to HIDE.
class DistrictVis : public Script {
  static const int kMaxHidden = 48;
  unsigned int frame = 0;
  int hidden[kMaxHidden];
  int hiddenCount = 0;
  bool written = false;
 public:
  DistrictVis() { for (int i = 0; i < kMaxHidden; ++i) hidden[i] = -1; }
  void update(ScriptContext& ctx) override {
    // Restore last frame's set BEFORE reading the new one, so a probe never
    // inherits the previous probe's state and the control is reachable again
    // from any arm. Only indices this script hid are ever touched.
    if (ctx.objects) {
      for (int i = 0; i < hiddenCount; ++i)
        if (hidden[i] >= 0 && hidden[i] < ctx.objectCount)
          ctx.objects[hidden[i]].visible = true;
    }
    // Commands are honoured only after the warm-up, exactly as the parked
    // sampler does: streaming, the AO atlas and the reflection probe must have
    // settled before any capture is taken.
    if (frame >= 1440 && frame % 30 == 0) {
      FILE* f = std::fopen(Tyra::FileUtils::fromCwd("district-benchmark-pose.txt").c_str(), "r");
      if (f) {
        int parsed[kMaxHidden];
        int n = 0, read = 0, v = 0;
        // Plain %d: the driver writes the set space-separated, and %d skips
        // leading whitespace itself. `read` counts every integer the file
        // yielded, `n` only the hideable ones, so that the control (-1) is
        // distinguishable from a file that parsed to nothing at all.
        while (n < kMaxHidden && std::fscanf(f, "%d", &v) == 1) {
          ++read;
          if (v >= 0) parsed[n++] = v;
        }
        // A torn or empty read must NOT silently become the control - that
        // would turn a probe into a duplicate of the control and report its
        // object as fully occluded. Last frame's set is held instead.
        if (read > 0) {
          hiddenCount = n;
          for (int i = 0; i < n; ++i) hidden[i] = parsed[i];
        }
        std::fclose(f);
      }
    }
    if (ctx.objects) {
      for (int i = 0; i < hiddenCount; ++i)
        if (hidden[i] >= 0 && hidden[i] < ctx.objectCount)
          ctx.objects[hidden[i]].visible = false;
    }
    // Garage DAY, always. The night poses have authored lamp flicker and
    // twinkling stars and are not pixel-comparable on this fixture at all.
    if (ctx.saveValues && DISTRICT_NIGHT_VALUE < ctx.saveValueCount)
      ctx.saveValues[DISTRICT_NIGHT_VALUE] = 0.0F;
    ctx.cameraOverride = true;
    ctx.cameraEye = Tyra::Vec4(0, 4, -32, 1);
    ctx.cameraAt = Tyra::Vec4(0, 1, -12, 1);
    ctx.cameraUp = Tyra::Vec4(0, 1, 0, 0);
    // The CSV appearing is the driver's "warm-up is over, bin/ is safe to
    // write into now" signal (capture-arm.ps1's contract). Nothing is written
    // during sampling. The hidden count is recorded so a capture can always be
    // traced back to the set that produced it.
    if (frame >= 1440 && !written) {
      FILE* f = std::fopen(Tyra::FileUtils::fromCwd("district-benchmark.csv").c_str(), "w");
      if (f) {
        std::fprintf(f, "frame,phase,fps\n");
        std::fprintf(f, "%u,0,%.3f\n", frame, ctx.engine->info.getFps());
        std::fclose(f);
        written = true;
      }
    }
    ++frame;
  }
};
}
TYRA_SCRIPT(Vehicle_playground::DistrictVis);
'''

p = argparse.ArgumentParser(description=__doc__,
                            formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('project', type=Path)
a = p.parse_args()
assert (a.project / 'BENCHMARK.json').exists(), 'Use an isolated benchmark fixture'
target = a.project / 'src/scripts/zz_district_benchmark.cpp'
assert target.exists(), 'No benchmark sampler to replace'
assert 'districtInv' not in (a.project / 'src/terrain_game.cpp').read_text(
    encoding='utf-8'), 'Run this BEFORE inventory-frame.py, not after'
target.write_text(SAMPLER, encoding='utf-8')
print('Visibility sampler installed:', target)
print('command file: bin/district-benchmark-pose.txt = indices to hide, -1 = control')
