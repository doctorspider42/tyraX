"""Private road submission omission; retain road storage and physical receivers."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out, source = [Path(p).resolve() for p in sys.argv[1:3]]
context = '--night-roads' if '--night-roads' in sys.argv else '--roads-day'
subprocess.run([sys.executable, str(ROOT/'tools/tyrax2-vu-paint.py'),
                str(out), str(source), context], check=True)

def edit(path, old, new):
    text = path.read_text()
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_bytes(text.replace(old, new).encode())

edit(out/'game/src/gen/game_physics.gen.cpp',
     'part.envTexBag->paintVu = MotionProbe::variant() == 6;',
     'part.envTexBag->paintVu = true;')
edit(out/'tyra/engine/inc/debug/motion_probe.hpp',
     'inline bool observing(){return false;}',
     'inline U roadAccepted=0, roadVertices=0, roadSkipped=0;\ninline bool observing(){return false;}')
edit(out/'game/src/gen/game_vehicles.gen.cpp',
     '    submitHeavy(c.bag.get());', '''    const bool omit=MotionProbe::variant()==6;
    if(MotionProbe::offset>=1000 && MotionProbe::offset<1100) {
      ++MotionProbe::roadAccepted;
      MotionProbe::roadVertices+=c.bag->count;
      if(omit) ++MotionProbe::roadSkipped;
    }
    if(!omit) submitHeavy(c.bag.get());''')
edit(out/'game/src/terrain_game.cpp',
     '  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {',
     '''  if(MotionProbe::offset==1000)
    MotionProbe::roadAccepted=MotionProbe::roadVertices=MotionProbe::roadSkipped=0;
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    const bool omit=MotionProbe::variant()==6;
    if(!MotionProbe::roadAccepted || !MotionProbe::roadVertices ||
       (omit ? MotionProbe::roadSkipped!=MotionProbe::roadAccepted : MotionProbe::roadSkipped!=0))
      MotionProbe::valid=false;
    printf("LOG: ROADSUBMIT phase=%u accepted=%u vertices=%u skipped=%u\\n",
      MotionProbe::phase,MotionProbe::roadAccepted,MotionProbe::roadVertices,MotionProbe::roadSkipped);''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-submit-omission',
    context=context,modes=[0,6,0],constantVuPaint=True,sourceProject=str(source),
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
