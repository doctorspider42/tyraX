"""Private moving-wheel local geometry/live matrix A/B, common direct+paint/Hybrid2."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--moving-only',action='store_true')
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
assert sha(a.source/'tyra/engine/bin/libtyra.a')==binding['engineArchiveSha256']
gate=json.loads((a.source/'world-cell-price-emulator.json').read_text())
physical=json.loads((a.source/'physical-v1/world-cell-price-ps2.json').read_text())
assert gate['valid'] and physical['valid'] and physical['physicalPrice']
assert gate['variant']=='paint-cells' and gate['elfSha256']==physical['elfSha256']==binding['elfSha256']

def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
        (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
        (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
shutil.copy2(repo/'tyrax2-wheel-vu.hpp',a.out/'game/inc/wheel_vu.hpp')
def edit(rel,old,new):
    f=a.out/rel;t=f.read_text();assert t.count(old)==1,(rel,old);f.write_text(t.replace(old,new))
vehicle='game/src/gen/game_vehicles.gen.cpp'
edit(vehicle,'#include "game_runtime.gen.hpp"','#include "game_runtime.gen.hpp"\n#include "wheel_vu.hpp"')
edit(vehicle,'  wheelBatches_.clear();','  WheelVu::clear();\n  wheelBatches_.clear();')
edit(vehicle,'    srcRun = run;',(repo/'tyrax2-wheel-vu-render.inc').read_text()+'\n    srcRun = run;')
if a.moving_only:
    edit('game/inc/wheel_vu.hpp','inline bool movingOnly=false;','inline bool movingOnly=true;')
    edit(vehicle,'  stapip.core.render(wheelBag_.get(), receiverClasses == 2 && receiverClass == 0 ?',
         '  if(WheelVu::active)++WheelVu::parkedBatches;\n  stapip.core.render(wheelBag_.get(), receiverClasses == 2 && receiverClass == 0 ?')
moving='game/inc/world_moving.hpp'
edit(moving,'#include "debug/paint_cell_price.hpp"','#include "debug/paint_cell_price.hpp"\n#include "wheel_vu.hpp"')
edit(moving,'PaintCellPrice::enabled=p<6 && modes[p];','PaintCellPrice::enabled=true;WheelVu::active=p<6 && modes[p];')
edit(moving,'  phase=p;frames=gasFrames=','  WheelVu::reset();\n  phase=p;frames=gasFrames=')
edit(moving,'variant=paint-cells\\n",p,unsigned(PaintCellPrice::enabled));','variant=wheels-vu\\n",p,unsigned(WheelVu::active));')
edit(moving,'inline void finish(){','inline void finish(){\n  WheelVu::witness(phase);')
game='game/src/terrain_game.cpp'
edit(game,'  const bool movingPaint=PaintCellPrice::enabled;','  const bool movingPaint=PaintCellPrice::enabled;\n  const bool movingWheels=WheelVu::active;')
edit(game,'  PaintCellPrice::enabled=arm==1?!movingPaint:movingPaint;','  PaintCellPrice::enabled=true;WheelVu::active=arm==1?!movingWheels:movingWheels;')
edit(game,'      PaintCellPrice::reset();PaintCellPrice::held=true;','      PaintCellPrice::reset();PaintCellPrice::held=true;WheelVu::reset();')
edit(game,'  PaintCellPrice::witness(MotionProbe::phase,arm);','  PaintCellPrice::witness(MotionProbe::phase,arm);WheelVu::witness(MotionProbe::phase,int(arm));')
edit(game,'  PaintCellPrice::enabled=movingPaint;','  PaintCellPrice::enabled=movingPaint;WheelVu::active=movingWheels;')
manifest=json.loads((a.source/'world-cell-price-manifest.json').read_text())
manifest.update(baseElfSha256=binding['elfSha256'],generatorSha256=sha(Path(__file__)),
    variant='wheels-vu',wheelMovingOnly=a.moving_only,status='UNQUALIFIED',
    comparison='common direct+paint/Hybrid2/ordinary roads: EE world wheel bake versus immutable local wheel meshes and live matrices',
    limits=['Existing still-rig/trig/strip fixes preserved in ordinary arm.',
            'Same per-wheel padded source geometry, rig visibility, suspension/steer/spin/LOD and material retained.',
            'ON uses four standard draws per admitted car; added heads/consumer work included in net.',
            'Common cold local source preparation both arms; source clear drains queue before release.',
            'Standard VU MVP arithmetic differs from EE bake followed by VP; exact raster gate remains required, no retrospective tolerance.',
            'Per-wheel light selection versus old definition batch must pass actual image qualification; no night/default claim.'])
if a.moving_only:manifest['limits'].append('Only nonzero-speed cars take new route; zero-speed rigs always use existing current-state world batches, never disappear.')
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED local wheel/VU matrix fixture')
