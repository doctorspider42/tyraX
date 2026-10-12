"""Compare old/new whole-cell producer: skip A31 bodies immediately discarded by cells."""
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
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
report=json.loads((a.physical_attempt/'world-cell-price-ps2.json').read_text())
assert report['valid'] and report['physicalPrice'] and report['elfSha256']==binding['elfSha256']
assert report['logSha256']==sha(a.physical_attempt/'console.log')
assert report['forwardSavedMs']>1 and report['reverseSavedMs']>1
assert not report['controlledStable60']
emulator=json.loads((a.source/'world-cell-price-emulator.json').read_text())
assert emulator['valid'] and emulator['elfSha256']==binding['elfSha256']
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
header=a.out/'game/inc/world_moving.hpp';t=header.read_text()
old='inline bool configured=false,enabled=false;'
assert t.count(old)==1;t=t.replace(old,old+'\ninline bool skipA31=false;\ninline unsigned legacyCalls=0,legacyPrepared=0;')
old='  enabled=p<6 && modes[p];'
assert t.count(old)==1;t=t.replace(old,'  enabled=p<6;skipA31=p<6 && modes[p];')
old='  localRebuilds=localTicks=heldOldReaders=0;'
assert t.count(old)==1;t=t.replace(old,old+'\n  legacyCalls=legacyPrepared=0;')
old='shakeReset=1\\n",p,unsigned(enabled));'
assert t.count(old)==1;t=t.replace(old,'shakeReset=1 variant=skip-a31\\n",p,unsigned(skipA31));')
old='  fflush(stdout);\n}\n}'
assert t.count(old)==1
t=t.replace(old,'''  printf("A44A31PHASE phase=%u skip=%u calls=%u prepared=%u\\n",phase,unsigned(skipA31),legacyCalls,legacyPrepared);
  fflush(stdout);
}
}''')
header.write_text(t)
physics=a.out/'game/src/gen/game_physics.gen.cpp';t=physics.read_text()
old='''  stapip.core.prepareWorldGeometry(ch.bag.get());
  stapip.core.prepareWorldGeometry(ch.aoBag.get());
  stapip.core.prepareWorldGeometry(ch.emisBag.get());
  stapip.core.prepareWorldGeometry(ch.gsBag.get());
  for(auto& layer:ch.layerPasses)stapip.core.prepareWorldGeometry(layer.bag.get());
  if(completeTerrainSlots){'''
assert t.count(old)==1
t=t.replace(old,'''  if(!completeTerrainSlots || !WorldMoving::skipA31){
    auto prepare=[&](Tyra::StaPipBag* b){
      const bool tracked=completeTerrainSlots && MotionProbe::offset>=1100 && !MotionProbe::finishPending;
      if(tracked)++WorldMoving::legacyCalls;
      const bool prepared=stapip.core.prepareWorldGeometry(b);
      if(tracked && prepared)++WorldMoving::legacyPrepared;
    };
    prepare(ch.bag.get());prepare(ch.aoBag.get());prepare(ch.emisBag.get());prepare(ch.gsBag.get());
    for(auto& layer:ch.layerPasses)prepare(layer.bag.get());
  }
  if(completeTerrainSlots){''')
physics.write_text(t)
manifest=json.loads((a.source/'world-cell-price-manifest.json').read_text())
manifest.update(baseElfSha256=binding['elfSha256'],generatorSha256=sha(Path(__file__)),
                variant='skip-a31',ordinaryControlUnchanged=False,
                comparison='old whole-cell producer versus whole-cell without discarded A31 preparation',
                status='UNQUALIFIED',limits=[
                    'Both price roles use the accepted cell route; mode0 retains A31 work, mode1 skips it.',
                    'Inactive slots still retain ordinary A31 producer; geometry/LOD/leases unchanged.',
                    'No predicted gain, production default, nighttime or60fps claim.'
                ])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED same-ELF old/new cell producer, no geometry change')
