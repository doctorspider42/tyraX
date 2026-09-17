"""Replace a benchmark fixture's sampler with the INVALIDATION test.

The motion sampler asks "how much does the reuse budget buy while the camera
moves". This one asks the opposite and much more important question: **does the
gate ever hold an image it should have thrown away?** Task 5 of the Motor
District plan lists what must invalidate a reuse - reflected-object movement,
hide/show, day/night changes - and a gate that skips those is a bug that a
frame-rate table would reward.

The camera is PARKED in all four phases, so pose drift is zero by construction
and the only thing that can force a capture is the content itself:

    0  nothing changes            the gate must reuse EVERY beat (the control
                                  for the other three - if this one captures,
                                  the pose half is not still and the phases
                                  below prove nothing)
    1  a reflected object is hidden and shown every 50 frames
    2  a reflected object slides 0.05 units a frame, continuously
    3  the day/night save value flips every 60 frames

Expected, and it is a sharp prediction rather than a shape: phase 0 reuses
every beat; phase 1 captures on the beat after each of its 4 toggles; phase 2
captures on EVERY beat (the object moves every frame); phase 3 captures on the
beat after each of its 4 flips, plus whatever the ambience ramp between them
does to the sky colour. Read it with

    python compare_probe.py x=<results dir> --labels "static,hide-show,move,day-night"
"""
import argparse
from pathlib import Path

SAMPLER = r'''#include "scripts/script.hpp"
#include "scripts/district_data.hpp"
#include "file/file_utils.hpp"
#include <cstdio>
namespace Vehicle_playground {
// Invalidation regimes for the reflection-reuse measurement (authoring/
// reflection-probe-2026-09-16/content-sampler.py). The camera never moves;
// the SCENE does. The object driven is found by walking the live objects for
// the first one flagged "Show in reflections", so this script carries no
// scene-specific index and keeps working when the district is re-authored.
class DistrictContent : public Script {
  unsigned int frame = 0;
  struct Sample { unsigned int frame, phase; float fps; } samples[32];
  int count = 0;
  unsigned int heldPhase = 3;
  bool written = false;
  int target = -2;        // -2 = not looked for yet, -1 = none in this scene
  float baseX = 0.0F;
 public:
  void update(ScriptContext& ctx) override {
    if (target == -2 && ctx.objects) {
      target = -1;
      for (int i = 0; i < ctx.objectCount; ++i)
        if (ctx.objects[i].data.reflected && ctx.objects[i].active) {
          target = i;
          baseX = ctx.objects[i].data.position[0];
          break;
        }
    }
    if (frame >= 1440 && frame % 30 == 0) {
      FILE* f = std::fopen(Tyra::FileUtils::fromCwd("district-benchmark-pose.txt").c_str(), "r");
      if (f) {
        unsigned int requested = 3;
        if (std::fscanf(f, "%u", &requested) == 1 && requested < 4)
          heldPhase = requested;
        std::fclose(f);
      }
    }
    const unsigned int phase = frame < 1440 ? frame / 360 : heldPhase;
    const unsigned int within = frame % 360;
    // Restore the object before re-deciding, so a phase never inherits the
    // previous one's state.
    if (target >= 0) {
      ctx.objects[target].visible = true;
      if (ctx.objects[target].data.position[0] != baseX) {
        ctx.objects[target].data.position[0] = baseX;
        ctx.objects[target].dirty = true;
      }
    }
    float night = 0.0F;
    if (phase == 1 && target >= 0) {
      ctx.objects[target].visible = ((within / 50u) & 1u) == 0u;
    } else if (phase == 2 && target >= 0) {
      ctx.objects[target].data.position[0] = baseX + (float)within * 0.05F;
      ctx.objects[target].dirty = true;
    } else if (phase == 3) {
      night = ((within / 60u) & 1u) ? 1.0F : 0.0F;
    }
    if (ctx.saveValues && DISTRICT_NIGHT_VALUE < ctx.saveValueCount)
      ctx.saveValues[DISTRICT_NIGHT_VALUE] = night;
    ctx.cameraOverride = true;
    ctx.cameraEye = Tyra::Vec4(0, 4, -32, 1);
    ctx.cameraAt = Tyra::Vec4(0, 1, -12, 1);
    ctx.cameraUp = Tyra::Vec4(0, 1, 0, 0);
    if (frame < 1440 && frame % 360 >= 120 && frame % 30 == 0 && count < 32)
      samples[count++] = {frame, phase, ctx.engine->info.getFps()};
    if (frame >= 1440 && !written) {
      FILE* f = std::fopen(Tyra::FileUtils::fromCwd("district-benchmark.csv").c_str(), "w");
      if (f) {
        std::fprintf(f,"frame,phase,fps\n");
        for (int i=0;i<count;++i)
          std::fprintf(f,"%u,%u,%.3f\n",samples[i].frame,samples[i].phase,samples[i].fps);
        std::fclose(f);
        written = true;
      }
    }
    ++frame;
  }
};
}
TYRA_SCRIPT(Vehicle_playground::DistrictContent);
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
print('Invalidation sampler installed:', target)
print('phases: 0 static, 1 hide/show, 2 moving object, 3 day/night flips')
