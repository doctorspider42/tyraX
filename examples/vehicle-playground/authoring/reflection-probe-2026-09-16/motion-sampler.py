"""Replace a benchmark fixture's parked sampler with a MOTION one.

`benchmark-district.py` parks the camera and the traffic, which is what makes
it repeatable — and it is exactly the wrong fixture for a change that skips
work when an input did not change. The reflection reuse budget is that kind of
change, so a number from the parked fixture alone is not evidence for it (the
same rule `--keep-routes` exists for; see docs/wheel-rebake-skip.md).

This rewrites the sampler so the four 360-frame phases are four CAMERA MOTION
regimes in the garage, in daylight, with everything else held constant:

    0  idle        the parked pose, unchanged - the best case
    1  straight    6 units/s forward, no turn - driving down a street
    2  slow turn   20 degrees/s - a junction
    3  hard turn   90 degrees/s - the case a cadence change is rejected for

Read it with `summarize_inventory.py`: the `hits` column of `env_probe_objs` is
the capture rate, and that is the whole measurement. Multiply the change in
captures per frame by the hardware-measured 4.14 ms per capture (2.07 ms of
garage-day average at one capture every second frame) to get milliseconds.
"""
import argparse
from pathlib import Path

SAMPLER = r'''#include "scripts/script.hpp"
#include "scripts/district_data.hpp"
#include "file/file_utils.hpp"
#include <cmath>
#include <cstdio>
namespace Vehicle_playground {
// Motion regimes for the reflection-reuse measurement (authoring/
// reflection-probe-2026-09-16/motion-sampler.py). One regime per 360-frame
// phase, all in the garage in daylight, so the ONLY variable is how the
// camera moves. Frame-indexed, never wall-clock: the emulator does not run at
// a constant rate and a time-driven route would put two runs in different
// places (docs/profiling.md, the frame-timing rig's own rule).
class DistrictMotion : public Script {
  unsigned int frame = 0;
  struct Sample { unsigned int frame, phase; float fps; } samples[32];
  int count = 0;
  unsigned int heldPhase = 3;
  bool written = false;
 public:
  void update(ScriptContext& ctx) override {
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
    const unsigned int within = frame < 1440 ? frame % 360 : (frame % 360);
    if (ctx.saveValues && DISTRICT_NIGHT_VALUE < ctx.saveValueCount)
      ctx.saveValues[DISTRICT_NIGHT_VALUE] = 0.0F;  // daylight in every regime
    // The parked pose every other round used, as regime 0 and as the origin
    // of the other three.
    float ex = 0.0F, ey = 4.0F, ez = -32.0F;
    float ax = 0.0F, ay = 1.0F, az = -12.0F;
    const float t = (float)within;
    if (phase == 1) {
      // Straight: 6 units/s forward at 50 Hz. Eye and target move together,
      // so the AIM is constant and only parallax on the reflected buildings
      // can invalidate the capture.
      ez += t * 0.12F;
      az += t * 0.12F;
    } else if (phase == 2 || phase == 3) {
      // Turn in place: the eye is fixed and the target sweeps about it, which
      // is what moves the probe's level-forward aim.
      const float rate = phase == 2 ? 0.4F : 1.8F;  // degrees per frame
      const float yaw = t * rate * 0.01745329252F;
      const float dx = ax - ex, dz = az - ez;
      const float c = cosf(yaw), s = sinf(yaw);
      ax = ex + dx * c - dz * s;
      az = ez + dx * s + dz * c;
    }
    ctx.cameraOverride = true;
    ctx.cameraEye = Tyra::Vec4(ex, ey, ez, 1);
    ctx.cameraAt = Tyra::Vec4(ax, ay, az, 1);
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
TYRA_SCRIPT(Vehicle_playground::DistrictMotion);
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
print('Motion sampler installed:', target)
print('phases: 0 idle, 1 straight 6 u/s, 2 turn 20 deg/s, 3 turn 90 deg/s')
