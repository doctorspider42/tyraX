"""A11: current production paint block on the preserved A08 game driver."""
import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('out',type=Path)
parser.add_argument('lifecycle_fixture',type=Path)
args=parser.parse_args()
out=args.out.resolve();source=args.lifecycle_fixture.resolve()
assert not out.exists(), 'Refuse to overwrite evidence'
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
shutil.copytree(source/'game',out/'game',ignore=quiet.ignore)
shutil.copytree(repo/'vendor/tyra',out/'tyra',ignore=quiet.ignore)
shutil.copy2(source/'authored-manifest.json',out/'authored-manifest.json')

def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())

# Regenerate separate copies with the rebuilt editor, never the authored map.
# Only the generated production paint region enters the old deterministic driver.
generated=[]
for kind in ('fpp','orbit'):
    game=out/('generated-'+kind)
    shutil.copytree(source/'game',game,ignore=quiet.ignore)
    manifest=game/'vehicle-playground.tyra'
    model=json.loads(manifest.read_text(encoding='utf-8-sig'))
    model['template']=kind
    manifest.write_text(json.dumps(model,indent=2)+'\n')
    run=subprocess.run([str(repo/'build-dev/tyrax-editor.exe'),'--refresh-gen',str(game)],capture_output=True,text=True)
    (out/('generation-'+kind+'.log')).write_text(run.stdout+run.stderr)
    assert run.returncode==0,run.stderr
    text=(game/'src/gen/game_physics.gen.cpp').read_text()
    begin=text.index('    const int paint = ')
    end=text.index('    renderObjectBag(objectIndex, part.envBag.get());',begin)
    generated.append(text[begin:end])
assert generated[0]==generated[1], 'Both templates must emit the same paint block'
production=generated[0]
(out/'production-paint-block.txt').write_bytes(production.encode())
fields=re.findall(r'    short envPaintKey\[6\].*?(?=    // The additive twin)',(out/'generated-fpp/inc/terrain_game.hpp').read_text(),re.S)
assert len(fields)==1
hdr=out/'game/inc/terrain_game.hpp'
old=re.findall(r'    short envPaintKey\[6\].*?(?=    // The additive twin)',hdr.read_text(),re.S)
assert len(old)==1
edit(hdr,old[0],fields[0]+'    PaintLife::State paintLife; // Independent diagnostic basis.\n')

engine=out/'tyra/engine'
for name in ('motion_probe.hpp','paint_lifecycle.hpp'):
    shutil.copy2(source/'tyra/engine/inc/debug'/name,engine/'inc/debug'/name)
edit(engine/'inc/renderer/3d/pipeline/static/core/stapip_vu1_experiments.hpp',
     '#define TYRA_VU1_EXP_PAINT 0','#define TYRA_VU1_EXP_PAINT 1')
# Only frame/phase accounting hooks, not old renderer/capability implementations.
path=engine/'src/engine.cpp'
text=quiet.engine_hooks(path.read_text()).replace('debug/quiet_runtime.hpp','debug/motion_probe.hpp').replace('QuietRuntime::begin','MotionProbe::begin').replace('QuietRuntime::afterLoop','MotionProbe::end')
path.write_text(text)
path=engine/'src/renderer/core/renderer_core.cpp'
text=quiet.present_hooks(path.read_text()).replace('debug/quiet_cadence.hpp','debug/motion_probe.hpp').replace('QuietCadence::onPresent','MotionProbe::onPresent')
for name,value in [('Pipeline',1),('Synchronous',0),('Synthetic',2)]:
    text=text.replace('QuietCadence::PresentKind::'+name,str(value))
path.write_text(text)

instrumented=production.replace('stapip.core.supportsPaintVu(part.envBag.get());','PaintLife::mode != 0 && stapip.core.supportsPaintVu(part.envBag.get());')
instrumented=instrumented.replace('      if (paintChanged) {','''      bool refreshed = false;
      if (paintChanged) {
        part.paintLife.accept(part.envTexBag->coordinates,part.envBag->count,normalStamp,&fwdL.x,&hL.x);''')
instrumented=instrumented.replace('      if (!part.envTexBag->paintVu && !part.envPaintEeReady) {','''      if (!part.envTexBag->paintVu && !part.envPaintEeReady) {
        refreshed = true;''')
anchor='''        part.envPaintEeReady = true;
#endif
      }
    }
'''
assert instrumented.count(anchor)==1
instrumented=instrumented.replace(anchor,'''        part.envPaintEeReady = true;
#endif
      }
      part.paintLife.eeReady = part.envPaintEeReady;
      TYRA_ASSERT(!memcmp(&part.envTexBag->paintForward.x,part.paintLife.forward,12) &&
                  !memcmp(&part.envTexBag->paintHalf.x,part.paintLife.half,12),
                  "A11 accepted basis mismatch");
      PaintLife::check(part.paintLife,part.envTexBag->paintVu,paintChanged,refreshed,
          part.shownLod,part.envTexBag->coordinates,part.envColorBag->many,part.envBag->count);
      TYRA_ASSERT(!PaintLife::errors,"A11 paint colour/state mismatch");
    }
''')
physics=out/'game/src/gen/game_physics.gen.cpp'
text=physics.read_text();begin=text.index('    const int paint = ')
end=text.index('    if(vehiclePaintFor(objectIndex)) MotionProbe::add(MotionProbe::EnvDrawn);',begin)
edit(physics,text[begin:end],instrumented)
# Use the actual authored mood script; roads/cars/reflections remain present.
edit(out/'game/inc/scene_data.hpp',
     'SAVE_VALUE_DEFAULTS[SAVE_VALUE_COUNT > 0 ? SAVE_VALUE_COUNT : 1] = {0.0F};',
     'SAVE_VALUE_DEFAULTS[SAVE_VALUE_COUNT > 0 ? SAVE_VALUE_COUNT : 1] = {1.0F};')
edit(out/'game/src/terrain_game.cpp',
     '!roads || false || daynight::g_hour!=12.0F',
     '!roads || !lights || daynight::g_hour!=0.0F')
(out/'paint-game-manifest.json').write_text(json.dumps(dict(
    experiment='A11 production game paint block, night roads, intrusive correctness only',
    baselineDriver=str(source),productionBlockSha256=hashlib.sha256(production.encode()).hexdigest(),
    templates=['fpp','orbit'],modes=['EE','VU','legacy fallback','VU','override fallback','VU'],
    hour=0,phaseCount=3,limitations=['no physical qualification','not a whole-scene raster oracle','A08 orchestration retained around regenerated production block']),indent=2)+'\n')
print(out)
