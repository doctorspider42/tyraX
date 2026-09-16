"""Create an isolated, instrumented Motor District performance fixture.

Build it with the editor being compared. The same project-owned sampler runs
in debug and release, with no host writes during sampling. Four 360-frame
phases cover the garage and outer road in day/night. After 1,440 updates,
bin/district-benchmark.csv contains 32 rolling engine-FPS samples.

REGENERATE BEFORE YOU MEASURE. This script copies the example's *committed*
generated sources (inc/*.gen.hpp, src/gen/, src/terrain_game.cpp) along with
everything else, and those drift behind the editor - examples/ generated files
in this repo are routinely stale. A fixture built from them is measuring a
DIFFERENT GAME from the one the editor under test would produce, and it will
not tell you so. Measured, 2026-09-15: a stale fixture reported 12.17 texture
re-uploads per frame in garage day and 3.6 ms more render submission than the
same scene regenerated with the editor being compared, which records 0.000
re-uploads and 0 evictions in all four poses. A whole VRAM investigation was
launched at that ghost.

So: run `tyrax-editor --build <fixture>` (or at least --refresh-gen) once
before the arm that matters, or copy in a .res-baked/ and bin/ you have just
regenerated - and check that the counters you are about to read agree with the
previous known-good run before concluding anything from a change in them. See
docs/gs-vram.md ("The 12.17 re-uploads were a stale fixture") and
docs/vu1-and-dma-cache-cost.md.

THE TRAFFIC IS PARKED, AND THAT IS A MEASUREMENT HAZARD FOR ONE CLASS OF
CHANGE. Stripping the routes is what makes this fixture repeatable, and for
most work it costs nothing. But any change that SKIPS WORK WHEN AN INPUT DID
NOT CHANGE is flattered enormously here, because every car is still, every
frame, forever - a per-frame skip test that never fires in a real district
scores 100% on this fixture. Pass --keep-routes for a second fixture whose
drivers are driving, measure both, and quote both; a number from the parked
fixture alone is not evidence for such a change. See
docs/wheel-rebake-skip.md, which is the worked example.
"""
import argparse
import csv
import json
from pathlib import Path
import shutil
import statistics

ROOT = Path(__file__).resolve().parents[1]
SAMPLER = r'''#include "scripts/script.hpp"
#include "scripts/district_data.hpp"
#include "file/file_utils.hpp"
#include <cstdio>
namespace Vehicle_playground {
class DistrictBenchmark : public Script {
  unsigned int frame = 0;
  struct Sample { unsigned int frame, phase; float fps; } samples[32];
  int count = 0;
  unsigned int heldPhase = 3;
  bool written = false;
 public:
  void update(ScriptContext& ctx) override {
    // Read pose commands only AFTER measurement; captures must not disturb samples.
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
    if (ctx.saveValues && DISTRICT_NIGHT_VALUE < ctx.saveValueCount)
      ctx.saveValues[DISTRICT_NIGHT_VALUE] = (phase & 1) ? 1.0F : 0.0F;
    ctx.cameraOverride = true;
    ctx.cameraEye = phase < 2 ? Tyra::Vec4(0,4,-32,1) : Tyra::Vec4(4,9,102,1);
    ctx.cameraAt = phase < 2 ? Tyra::Vec4(0,1,-12,1) : Tyra::Vec4(65,3,106,1);
    ctx.cameraUp = Tyra::Vec4(0,1,0,0);
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
TYRA_SCRIPT(Vehicle_playground::DistrictBenchmark);
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('destination', type=Path)
parser.add_argument('--profile', choices=['debug', 'quiet-debug', 'release'], default='debug')
parser.add_argument('--mesh-lod', type=float, default=0)
parser.add_argument('--terrain-lod', type=float, default=0)
parser.add_argument('--report', action='store_true', help='summarize an existing fixture CSV')
parser.add_argument('--keep-routes', action='store_true',
                    help='leave the AI drivers their routes, so traffic MOVES. '
                         'The camera stays frozen and the player stays pinned, '
                         'but the cars do not - which is the only fixture that '
                         'can honestly measure a skip-when-unchanged change '
                         '(see docs/wheel-rebake-skip.md). Costs determinism: '
                         'the pixels are no longer repeatable frame to frame, '
                         'so a --capture-frame A/B needs the parked fixture.')
args = parser.parse_args()
if args.report:
    rows = list(csv.DictReader((args.destination/'bin/district-benchmark.csv').open()))
    if len(rows) != 32 or len({r['frame'] for r in rows}) != 32:
        raise SystemExit('Incomplete or duplicated samples; reject this run')
    for phase, name in enumerate(['garage-day','garage-night','outer-road-day','outer-road-night']):
        values = [float(r['fps']) for r in rows if int(r['phase']) == phase]
        if len(values) != 8 or min(values) <= 0:
            raise SystemExit('Invalid phase samples; reject this run')
        print(f'{name}: median {statistics.median(values):.2f} FPS; '
              f'rolling samples {min(values):.2f}..{max(values):.2f}; n={len(values)}')
    raise SystemExit(0)
if args.destination.exists():
    raise SystemExit(f'Refusing to overwrite {args.destination}')
shutil.copytree(ROOT, args.destination, ignore=shutil.ignore_patterns(
    'bin','obj','build','.res-baked','__pycache__','.git'))
manifest = args.destination/'vehicle-playground.tyra'
p = json.loads(manifest.read_text(encoding='utf-8'))
settings = p['settings']
settings.update(buildProfile='release' if args.profile == 'release' else 'debug',
                keyboardMouse=False, meshLodDistance=args.mesh_lod,
                terrainLodDistance=args.terrain_lod)
if args.profile != 'debug':
    for key in ['liveLink','liveDebug','liveLogic','timeMachine','remotePad','inputRecorder']:
        settings[key] = False
manifest.write_text(json.dumps(p, indent=2)+'\n', encoding='utf-8')
for file in (args.destination/'objects').glob('*.json'):
    obj = json.loads(file.read_text(encoding='utf-8'))
    if obj.get('type') == 'player':
        obj.setdefault('player',{}).update(walkSpeed=0,lookSpeed=0,canJump=False)
    if obj.get('type') == 'vehicle' and not args.keep_routes:
        obj.setdefault('vehicle',{}).pop('route',None)
    file.write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
(args.destination/'src/scripts/zz_district_benchmark.cpp').write_text(SAMPLER,encoding='utf-8')
(args.destination/'BENCHMARK.json').write_text(json.dumps({
    'profile':args.profile,'meshLod':args.mesh_lod,'terrainLod':args.terrain_lod,
    'traffic':('routed; the AI drivers are driving, so the frame is NOT '
               'deterministic and the pixels are not repeatable'
               if args.keep_routes else
               'parked; normal traffic must be tested separately'),
    'keepRoutes':bool(args.keep_routes),
    'sampler':'identical custom script; warmup120frames; samples every30frames; no sample-time host writes'
},indent=2)+'\n',encoding='utf-8')
print(args.destination)
