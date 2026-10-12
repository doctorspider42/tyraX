"""A13 lean same-ELF public-paint qualification, staged without deployment."""
import argparse
import hashlib
import importlib.util
import json
import shutil
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('out',type=Path)
parser.add_argument('source',type=Path,help='Completed A12 V2 fixture')
parser.add_argument('--reverse',action='store_true')
args=parser.parse_args();out=args.out.resolve();source=args.source.resolve()
assert not out.exists()
binding=json.loads((source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():
    assert hashlib.sha256((source/rel).read_bytes()).hexdigest()==digest,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
def ignore(path,names):
    return quiet.ignore(path,names)+[n for n in names if n=='paint_lifecycle.hpp']
for name in ('game','tyra'):
    shutil.copytree(source/name,out/name,ignore=ignore)
for name in ('authored-manifest.json','production-paint-block.txt'):
    shutil.copy2(source/name,out/name)
quiet.switch(out/'tyra/engine/inc/debug/hardware_trace.hpp','TYRA_HARDWARE_TRACE')
quiet.switch(out/'tyra/engine/inc/debug/frame_profile.hpp','TYRA_FRAME_PROFILE')
def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())

header=out/'game/inc/terrain_game.hpp'
edit(header,'#include "debug/paint_lifecycle.hpp"\n','')
edit(header,'    PaintLife::State paintLife; // Independent diagnostic basis.\n','')
game=out/'game/src/terrain_game.cpp'
text=game.read_text();start=text.index('  const unsigned paintMode=');end=text.index('  if(MotionProbe::phase<MotionProbe::phaseCount',start)
edit(game,text[start:end],'  saveValues[0] = MotionProbe::phase >= 3 ? 1.0F : 0.0F;\n')
edit(game,'#include "renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tce_vu1_program.hpp"\n','')
edit(game,'MotionProbe::phase==1 ? lights==0 : lights!=0','MotionProbe::phase>=3 ? lights==0 : lights!=0')
edit(game,'MotionProbe::phase==1?0.0F:12.0F','MotionProbe::phase>=3?0.0F:12.0F')
physics=out/'game/src/gen/game_physics.gen.cpp'
text=physics.read_text();start=text.index('    const int paint = ');end=text.index('    if(vehiclePaintFor(objectIndex)) MotionProbe::add(MotionProbe::EnvDrawn);',start)
paint=(out/'production-paint-block.txt').read_text()
assert 'PaintLife' not in paint and 'MotionProbe' not in paint
paint=paint.replace('part.envTexBag->paintVu = stapip.core.supportsPaintVu(part.envBag.get());',
                    'part.envTexBag->paintVu = MotionProbe::variant()==6 && stapip.core.supportsPaintVu(part.envBag.get());\n      if(part.envTexBag->paintVu) MotionProbe::add(MotionProbe::PaintVu);')
paint=paint.replace('      if (paintChanged) {','      if (paintChanged) {\n        MotionProbe::add(MotionProbe::Paint);')
edit(physics,text[start:end],paint)
edit(physics,'      if(vehiclePaintFor(i)) tier = (MotionProbe::offset / 240) % 3;\n','')
probe=out/'tyra/engine/inc/debug/motion_probe.hpp'
edit(probe,'#include "debug/paint_lifecycle.hpp"\n','')
edit(probe,'inline constexpr U phaseCount=3;','inline constexpr U phaseCount=6;')
modes=([6,0,6] if args.reverse else [0,6,0])*2
edit(probe,'constexpr U modes[]={0,6,0};','constexpr U modes[]={'+','.join(map(str,modes))+'};')
text=probe.read_text();start=text.index('    printf("PAINTDRIVE ');end=text.index('    printf("LOG: DRIVEPHASE',start)
edit(probe,text[start:end],'')
# These interventions must be impossible in qualification, including future mode edits.
start=probe.read_text().index('inline bool freezeCapture() {');end=probe.read_text().index('// Private elapsed scopes:',start)
edit(probe,probe.read_text()[start:end],'inline bool freezeCapture() { return false; }\n')
edit(probe,'inline bool skipBodyEnv(){return gas() && (variant()==2 || variant()==3);}',
     'inline bool skipBodyEnv(){return false;}')
for rel in ('game/src','game/inc','tyra/engine/src','tyra/engine/inc'):
    for path in (out/rel).rglob('*'):
        if path.is_file() and path.suffix in ('.cpp','.hpp'):
            assert 'PaintLife::' not in path.read_text(errors='replace'),path
assert 'paintOverride' not in game.read_text()
assert '(MotionProbe::offset / 240) % 3' not in physics.read_text()
(out/'paint-qualification-manifest.json').write_text(json.dumps(dict(
    experiment='A13 lean same-ELF EE/VU paint; physical qualification pending',
    baseline=str(source),baselineElf=binding['elfSha256'],modes=modes,
    hardwareTrace=0,frameProfile=0,
    hours=[12,12,12,0,0,0],sampleStart=1200,gasStart=1320,maxRows=1200,endZ=108,
    productionPaintSha256=hashlib.sha256((out/'production-paint-block.txt').read_bytes()).hexdigest(),
    limitations=['same compiled paint images in both controls; not flag-OFF whole-image price',
                 'light per-draw counters and deferred CSV remain; common observer cost unpriced',
                 'camera sweep during straight driving; not a standalone parked-turn benchmark',
                 'no physical target available; do not deploy or infer PS2 speed from emulator']),indent=2)+'\n')
print(out)
