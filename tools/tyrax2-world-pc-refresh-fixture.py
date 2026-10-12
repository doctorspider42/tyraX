"""Reuse qualified A37 raw-PC adapter on accepted direct+paint Hybrid2 workload."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--physical-attempt',type=Path,required=True)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
b=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==b['elfSha256']
for platform,location in [('emulator',a.source),('ps2',a.physical_attempt)]:
    gate=json.loads((location/f'world-cell-price-{platform}.json').read_text())
    assert gate['valid'] and gate['variant']=='paint-cells' and gate['elfSha256']==b['elfSha256']

def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
for original,name in [('tyrax2-pc-moving.hpp','pc_moving.hpp'),('tyrax2-pc-sample-store.hpp','pc_sample_store.hpp')]:
    shutil.copy2(repo/original,a.out/'tyra/engine/inc/debug'/name)

def edit(rel,old,new):
    file=a.out/rel;t=file.read_text();assert t.count(old)==1,(rel,old)
    file.write_text(t.replace(old,new))

edit('game/inc/world_moving.hpp','PaintCellPrice::enabled=p<6 && modes[p];',
     'PaintCellPrice::enabled=true;')
edit('game/inc/world_moving.hpp','variant=paint-cells\\n",p,unsigned(PaintCellPrice::enabled));',
     'variant=pc-refresh\\n",p,unsigned(p<6 && modes[p]));')
engine='tyra/engine/src/engine.cpp'
edit(engine,'#include "debug/motion_probe.hpp"',
     '#include "debug/motion_probe.hpp"\n#include "debug/pc_moving.hpp"')
edit(engine,'  MotionProbe::begin(renderer.core.getStallTotal());',
     '''  MotionProbe::begin(renderer.core.getStallTotal());
  if(MotionProbe::offset==MotionProbe::gasStart){
    constexpr unsigned periods[]={0,997,0,1733,0,1733};
    PcMoving::start(MotionProbe::phase,0,periods[MotionProbe::phase]);
  }
  PcMoving::mark(MotionProbe::offset,1);
  if(MotionProbe::finishPending){PcMoving::stop();PcMoving::exportRows();}''')
edit(engine,'  MotionProbe::end(renderer.core.getStallTotal());',
     '  MotionProbe::end(renderer.core.getStallTotal());\n  PcMoving::mark(MotionProbe::offset,0);')
game='game/src/terrain_game.cpp'
edit(game,'#include "world_moving.hpp"',
     '#include "world_moving.hpp"\n#include "debug/pc_moving.hpp"')
edit(game,'  MotionProbe::renderBegin();',
     '  PcMoving::mark(MotionProbe::offset,2);\n  MotionProbe::renderBegin();')
edit(game,'  PaintCellPrice::enabled=arm==1?!movingPaint:movingPaint;',
     '  PaintCellPrice::enabled=true;')
edit(game,'  WorldCellComplete::active=candidate;\n  for(unsigned i=0;i<32;++i){',
     '''  WorldCellComplete::active=candidate;
  const unsigned stopped=PcMoving::callbacks;
  if(arm==1)PcMoving::start(MotionProbe::phase,1,MotionProbe::phase%2?1733:997);
  for(unsigned i=0;i<32;++i){
    PcMoving::mark(i,8);''')
edit(game,'    PaintCellPrice::held=false;\n  }\n  for(unsigned kind=0;kind<3;++kind){',
     '''    PaintCellPrice::held=false;
  }
  if(arm==1){PcMoving::stop();PcMoving::exportRows();}
  else TYRA_ASSERT(PcMoving::callbacks==stopped,"A44 sampler callback in OFF arm");
  for(unsigned kind=0;kind<3;++kind){''')
sdk=Path('/mnt/c/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2sdk')
sdkPins={rel:sha(sdk/rel) for rel in ('ee/lib/libkernel.a','ee/include/kernel.h','ee/include/timer.h','ee/include/timer_alarm.h')}
cal=json.loads((repo.parent/'docs/tyrax2-pc-sampling-2026-10-11/calibration-v1/pc-calibration-manifest.json').read_text())
assert sdkPins==cal['sdkPins'],'SDK differs from actual PC calibration'
manifest=json.loads((a.source/'world-cell-price-manifest.json').read_text())
manifest.update(baseElfSha256=b['elfSha256'],generatorSha256=sha(Path(__file__)),variant='pc-refresh',
    comparison='common direct+paint Hybrid2: OFF/997/OFF and1733/OFF/1733 sampler',status='UNQUALIFIED',
    limits=['Observer overhead, not an optimization or exclusive-cycle/scene60 ceiling proof.',
            'Same calibrated A37 adapter/store; IRQ masking/preemption/coalescing bias remains.',
            'Fixed-step full held image/activation/physical controls and raw symbolization required.'])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.out/'pc-moving-manifest.json').write_text(json.dumps(dict(parentElf=b['elfSha256'],periods=[0,997,0,1733,0,1733],
    heldPeriods=[997,1733,997,1733,997,1733],sdkPins=sdkPins,
    calibrationElf='a87be09eeee5f91ee90f75876a62be9daca35d3ca368ce07ba739fefc0caf2c3',
    scope='Fresh raw instruction residency on accepted direct+paint baseline, not exclusive EE cycles'),indent=2)+'\n')
print(a.out,'UNQUALIFIED calibrated PC refresh, current workload unchanged')
