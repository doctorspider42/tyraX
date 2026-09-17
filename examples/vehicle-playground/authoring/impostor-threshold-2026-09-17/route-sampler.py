"""Replace a benchmark fixture's sampler with a DRIVE-IN ROUTE of parked stations.

A threshold validated only at the parked pose is rejected, and the reason is
specific: an impostor swap is invisible in a still frame by construction - the
arm that is showing a card is showing it in both captures. What has to be shown
is that **crossing the threshold does not pop**, and that needs the camera at a
series of distances either side of it.

Why STATIONS rather than a moving camera. The capture path freezes the game for
a fixed spell, so two arms' `--capture-frame` calls never land on the same frame
of a moving route and a pixel diff between them reports the sampler's phase
rather than the change (the reflection round hit exactly this and had to leave
its moving evidence numeric). Parking at station k makes the comparison exact:
both arms are at the identical camera position, so a difference is the
representation and nothing else.

The route drives the garage approach, +Z along the road centre at the fixture's
own eye height, keeping the garage pose's heading. Distances to the two objects
the threshold acts on - Tower block 06 and Loft block 07 - fall monotonically
from ~117 to ~45, so the stations straddle both the 100 switch and the 90
return-to-full hysteresis edge, and they are dense where the swap happens.

TWO NUMBERS COME OUT OF IT, and the second is the gate:

  * candidate vs control AT a station = what the player loses to the card at
    that distance, in pixels. This is the quality cost, measured, per distance.
  * the STEP in that number between adjacent stations, compared with the step
    between stations on the same side of the threshold. A swap that changes the
    picture by about as much as moving the camera two units does is not a pop;
    one that changes it by far more is.
"""
import argparse
from pathlib import Path

SAMPLER = r'''#include "scripts/script.hpp"
#include "scripts/district_data.hpp"
#include "file/file_utils.hpp"
#include <cstdio>
#include <cmath>
namespace Vehicle_playground {
// Drive-in route of parked stations for the impostor-threshold gate
// (authoring/impostor-threshold-2026-09-17/route-sampler.py). The camera is
// PARKED at station k; the command file selects k. Same heading as the
// fixture's garage-day pose, so only the distance changes.
class DistrictRoute : public Script {
  // 0..11  APPROACH: the eye drives +Z down the road centre. Distance to Tower
  //        block 06 at (-33,0,80) runs 117 -> 45, so these cross the 100
  //        switch and the 90 hysteresis edge. Dense where the swap happens.
  // 12..19 ORBIT: the eye circles Tower block 06 at a radius that keeps it an
  //        impostor in the candidate (110 units > the 100 threshold), looking
  //        at it. This is the OTHER gate - an 8-view card changes view sector
  //        every 45 degrees, and a sector change is a different kind of pop
  //        from the near/far swap. Rotating the camera in place would NOT test
  //        it: the view is chosen from the object-to-CAMERA-POSITION vector,
  //        so only moving around the object changes the sector. The arc is
  //        centred on the garage approach and spans 91 degrees at 13-degree
  //        steps, which crosses at least two sector boundaries.
  static const int kApproach = 12;
  static const int kOrbit = 8;
  static const int kStations = kApproach + kOrbit;
  const float stationZ[kApproach] = {-32, -20, -12, -8, -6, -4,
                                     -2, 0, 4, 12, 24, 40};
  unsigned int frame = 0;
  int station = 0;
  bool written = false;
 public:
  void update(ScriptContext& ctx) override {
    if (frame >= 1440 && frame % 30 == 0) {
      FILE* f = std::fopen(Tyra::FileUtils::fromCwd("district-benchmark-pose.txt").c_str(), "r");
      if (f) {
        int v = 0;
        // A torn or empty read holds the last station rather than silently
        // falling back to station 0, which would duplicate a capture and read
        // as "the swap changed nothing".
        if (std::fscanf(f, "%d", &v) == 1 && v >= 0 && v < kStations) station = v;
        std::fclose(f);
      }
    }
    if (ctx.saveValues && DISTRICT_NIGHT_VALUE < ctx.saveValueCount)
      ctx.saveValues[DISTRICT_NIGHT_VALUE] = 0.0F;
    ctx.cameraOverride = true;
    if (station < kApproach) {
      const float z = stationZ[station];
      ctx.cameraEye = Tyra::Vec4(0, 4, z, 1);
      ctx.cameraAt = Tyra::Vec4(0, 1, z + 20.0F, 1);
    } else {
      // Tower block 06, the object the threshold acts on in the garage poses.
      const float tx = -33.0F, tz = 80.0F;
      const float radius = 110.0F;          // > 100, so the candidate shows a card
      const float deg = 120.0F + 13.0F * (float)(station - kApproach);
      const float rad = deg * 3.14159265F / 180.0F;
      ctx.cameraEye = Tyra::Vec4(tx + radius * sinf(rad), 4.0F,
                                 tz + radius * cosf(rad), 1);
      ctx.cameraAt = Tyra::Vec4(tx, 8.0F, tz, 1);
    }
    ctx.cameraUp = Tyra::Vec4(0, 1, 0, 0);
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
TYRA_SCRIPT(Vehicle_playground::DistrictRoute);
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
print('Route sampler installed:', target)
print("stations 0-11 approach (eye z): -32 -20 -12 -8 -6 -4 -2 0 4 12 24 40")
print("stations 12-19 orbit  : radius 110 around Tower block 06, 120..211 deg")
