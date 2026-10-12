"""Replace A44 whole-set LOD rebuilding with owned local cell publication."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--mode',choices=('OFF','ON'),required=True)
p.add_argument('--reuse-built',action='store_true')
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
assert 'DRIVEDONE valid=1' in (a.source/'emulator/emulator.log').read_text(errors='replace')
if a.reuse_built:
    manifest=json.loads((a.source/'world-cell-moving-manifest.json').read_text())
    assert manifest.get('incremental') and manifest['mode']=='OFF' and a.mode=='ON'
else:
    assert a.mode=='OFF'
    report=json.loads((a.source/'world-cell-moving-emulator.json').read_text())
    assert not report['valid'] and not report['deliveryValid']
    assert report['elfSha256']==binding['elfSha256']
    assert report['onLogSha256']==sha(a.source/'emulator/emulator.log')
    assert len(report['images'])==36 and all(r['differentBytes']==0 for r in report['images'])
    assert all(r[6]==25 and r[7]==50 and r[9]==49 for r in report['onSummary'])

def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-terrain-manifest.json',
            'world-cell-view-manifest.json','world-source-manifest.json','world-source-switch-manifest.json',
            'world-command-source-manifest.json','world-command-order-manifest.json',
            'world-cell-conditioned-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)

if not a.reuse_built:
    repo=Path(__file__).resolve().parent
    game=a.out/'game/src/terrain_game.cpp';t=game.read_text()
    old=(repo/'tyrax2-world-cell-terrain-methods.inc').read_text().strip()
    assert t.count(old)==1
    t=t.replace(old,(repo/'tyrax2-world-cell-incremental-methods.inc').read_text().strip())
    game.write_text(t)
    header=a.out/'game/inc/terrain_game.hpp';t=header.read_text()
    old='  bool completeTerrainDirty=true,completeTerrainSlots=false;'
    assert t.count(old)==1
    t=t.replace(old,old+'\n  u64 completeTerrainTextureEpoch=0;\n  u32 completeTerrainProgramEpoch=0,completeTerrainDepth=0;\n  bool completeTerrainFog=false;')
    old='  std::vector<TerrainChunk> terrainChunks;'
    assert t.count(old)==1
    t=t.replace(old,'  bool compileCompleteTerrainChunk(TerrainChunk& ch);\n'+old)
    header.write_text(t)
    header=a.out/'game/inc/world_moving.hpp';t=header.read_text()
    old='inline unsigned prepares=0,prepareTicks=0,dirtyFrames=0,captures=0;'
    assert t.count(old)==1
    t=t.replace(old,old+'\ninline unsigned localRebuilds=0,localTicks=0,heldOldReaders=0;')
    t=t.replace('deferred=1','deferred=0 incremental=1')
    old='  phase=p;frames=gasFrames=requested=submitted=fallback=prepares=prepareTicks=dirtyFrames=captures=0;'
    assert t.count(old)==1;t=t.replace(old,old+'\n  localRebuilds=localTicks=heldOldReaders=0;')
    old='  fflush(stdout);\n}\n}'
    assert t.count(old)==1
    t=t.replace(old,'''  printf("A44LOCALPHASE phase=%u rebuilds=%u ticks=%u heldReaders=%u\\n",phase,localRebuilds,localTicks,heldOldReaders);
  fflush(stdout);
}
}''')
    header.write_text(t)
    physics=a.out/'game/src/gen/game_physics.gen.cpp';t=physics.read_text()
    old='  ch.completeCell.owner.reset();completeTerrainDirty=true;'
    assert t.count(old)==1
    t=t.replace(old,'''  if(ch.completeCell.owner){
    const auto* old=ch.completeCell.owner->admit(ch.completeCell.key);
    if(old && !Tyra::Vif1Queue::immutableSpanReady(old->data()))++WorldMoving::heldOldReaders;
  }
'''+old)
    old='  for(auto& layer:ch.layerPasses)stapip.core.prepareWorldGeometry(layer.bag.get());\n}'
    assert t.count(old)==1
    t=t.replace(old,'''  for(auto& layer:ch.layerPasses)stapip.core.prepareWorldGeometry(layer.bag.get());
  if(completeTerrainSlots){
    const unsigned start=WorldMoving::ticks();
    TYRA_ASSERT(compileCompleteTerrainChunk(ch),"A44 local cell publication failed");
    ++WorldMoving::localRebuilds;WorldMoving::localTicks+=WorldMoving::ticks()-start;
    completeTerrainDirty=false;
  }
}''')
    old='#include "world_held_order.hpp"'
    assert t.count(old)==1;t=t.replace(old,old+'\n#include "world_moving.hpp"')
    physics.write_text(t)
else:
    for rel in ('native-binding.json','symbols.txt','build.log'):shutil.copy2(a.source/rel,a.out/rel)
    assert sha(a.out/'game/bin/vehicle-playground.elf')==binding['elfSha256']

(a.out/'game/bin/world-moving-control.txt').write_bytes(b'1' if a.mode=='ON' else b'0')
manifest=json.loads((a.source/'world-cell-moving-manifest.json').read_text())
manifest.update(baseElfSha256=binding['elfSha256'],mode=a.mode,reuseBuilt=a.reuse_built,
                incremental=True,deferredPreparation=False,generatorSha256=sha(Path(__file__)),
                controlSha256=sha(a.out/'game/bin/world-moving-control.txt'),status='UNQUALIFIED',
                limits=['Same-ELF fixed-step/order2 correctness observer, not production FPS.',
                        'Local bodies rebuilt during original producer streaming; texture set stays reserved.',
                        'Material-set/epoch changes require pre-record whole-set reservation; unsupported changes fail closed.',
                        'Night ordinary; moving raster/lifetime and physical net gates still required.'])
(a.out/'world-cell-moving-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.out/'world-cell-incremental-manifest.json').write_text(json.dumps({
    'baseElfSha256':binding['elfSha256'],'reuseBuilt':a.reuse_built,
    'methodsSha256':sha(Path(__file__).with_name('tyrax2-world-cell-incremental-methods.inc')),
    'status':'UNQUALIFIED'},indent=2)+'\n')
print(a.out,a.mode,'same-ELF reuse' if a.reuse_built else 'requires native build')
