"""A12 full-throttle day/night/day qualification of bound A11 paint integration."""
import hashlib
import importlib.util
import json
import shutil
import sys
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
out,source=[Path(p).resolve() for p in sys.argv[1:3]]
assert not out.exists()
binding=json.loads((source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():
    assert hashlib.sha256((source/rel).read_bytes()).hexdigest()==digest,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
for name in ('game','tyra'):
    shutil.copytree(source/name,out/name,ignore=quiet.ignore)
for name in ('authored-manifest.json','paint-game-manifest.json','production-paint-block.txt'):
    shutil.copy2(source/name,out/name)

def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())

vehicle=out/'game/src/gen/game_vehicles.gen.cpp'
edit(vehicle,'inThrottle=0.0F; inBrake=0.0F;','inThrottle=MotionProbe::gas()?1.0F:0.0F; inBrake=0.0F;')
edit(vehicle,'inSteer=0.0F; inHand=1; inNos=0;','inSteer=0.0F; inHand=MotionProbe::gas()?0:1; inNos=0;')
game=out/'game/src/terrain_game.cpp'
edit(game,'    for(const auto& l:engine->renderer.core.dynLights) if(l.enabled) ++lights;',
     '''    for(unsigned i=0;i<engine->renderer.core.dynLightCount;++i)
      if(engine->renderer.core.dynLights[i].enabled) ++lights;''')
edit(game,'  PaintLife::mode=paintMode;','''  PaintLife::mode=paintMode;
  saveValues[0] = MotionProbe::phase == 1 ? 1.0F : 0.0F;''')
edit(game,'!roads || !lights || daynight::g_hour!=0.0F',
     '!roads || (MotionProbe::phase==1 ? lights==0 : lights!=0) || daynight::g_hour!=(MotionProbe::phase==1?0.0F:12.0F)')
probe=out/'tyra/engine/inc/debug/motion_probe.hpp'
edit(probe,'    if(offset==gasStart+719) { finishPending=true; }',
     '    if(current.z>=108.0F || size==1200) { finishPending=true; if(current.z<108.0F)valid=false; }')
oracle=out/'tyra/engine/inc/debug/paint_lifecycle.hpp'
edit(oracle,'inline unsigned reloads=0;',
     'inline unsigned reloads=0;\ninline unsigned movingVisits=0, movingVu=0, movingEe=0;')
edit(probe,'    PaintLife::report(phase);','''    printf("PAINTDRIVE phase=%u visits=%u vu=%u ee=%u\\n",phase,PaintLife::movingVisits,PaintLife::movingVu,PaintLife::movingEe);
    PaintLife::movingVisits=PaintLife::movingVu=PaintLife::movingEe=0;
    PaintLife::report(phase);''')
edit(out/'game/src/gen/game_physics.gen.cpp','      part.paintLife.eeReady = part.envPaintEeReady;','''      if(MotionProbe::gas() && fabsf(MotionProbe::current.speed)>5.0F) {
        ++PaintLife::movingVisits;
        if(part.envTexBag->paintVu) ++PaintLife::movingVu; else ++PaintLife::movingEe;
      }
      part.paintLife.eeReady = part.envPaintEeReady;''')
(out/'paint-drive-manifest.json').write_text(json.dumps(dict(
    experiment='A12 intrusive moving paint correctness; not performance',
    baseline=str(source),baselineElf=binding['elfSha256'],hours=[12,0,12],
    sampleStart=1200,gasStart=1320,maxRows=1200,endZ=108,
    controls='full throttle, zero steer, released handbrake; +/-60 degree camera sweeps',
    limitations=['physical PS2 unavailable','no whole-scene paired raster proof','forced LOD/legacy/override modes are diagnostic']),indent=2)+'\n')
print(out)
