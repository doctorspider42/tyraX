"""Qualify ordinary OFF versus fully costed cell ON before hardware net pricing."""
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
manifest=json.loads((a.source/'world-cell-moving-manifest.json').read_text())
assert manifest.get('incremental') and manifest['bufferCount']==2
if a.reuse_built:
    assert manifest.get('productionRoles') and manifest['mode']=='OFF' and a.mode=='ON'
else:
    assert a.mode=='OFF'
    report=json.loads((a.source/'world-cell-moving-emulator.json').read_text())
    assert report['valid'] and report['deliveryValid'] and report['incremental']
    assert report['elfSha256']==binding['elfSha256']
    assert report['onLogSha256']==sha(a.source/'emulator/emulator.log')
    assert len(report['images'])==36 and all(r['differentBytes']==0 for r in report['images'])
    assert all(r[3]>0 for r in report['onLocalRebuilds'])

def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-terrain-manifest.json',
            'world-cell-view-manifest.json','world-source-manifest.json','world-source-switch-manifest.json',
            'world-command-source-manifest.json','world-command-order-manifest.json',
            'world-cell-conditioned-manifest.json','world-cell-incremental-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
if not a.reuse_built:
    game=a.out/'game/src/terrain_game.cpp';t=game.read_text()
    old='''  if(MotionProbe::phase<3 && MotionProbe::offset>=1100 && !MotionProbe::finishPending){
    const bool dirty=completeTerrainDirty || !completeTerrainSlots;'''
    assert t.count(old)==1
    t=t.replace(old,'''  if(WorldMoving::enabled && MotionProbe::phase<3 && MotionProbe::offset>=1100 && !MotionProbe::finishPending){
    const bool dirty=completeTerrainDirty || !completeTerrainSlots;''')
    game.write_text(t)
    header=a.out/'game/inc/world_moving.hpp';t=header.read_text()
    old='deferred=0 incremental=1'
    assert t.count(old)==1;t=t.replace(old,old+' roles=production')
    header.write_text(t)
else:
    for rel in ('native-binding.json','symbols.txt','build.log'):shutil.copy2(a.source/rel,a.out/rel)
    assert sha(a.out/'game/bin/vehicle-playground.elf')==binding['elfSha256']
(a.out/'game/bin/world-moving-control.txt').write_bytes(b'1' if a.mode=='ON' else b'0')
manifest.update(baseElfSha256=binding['elfSha256'],mode=a.mode,reuseBuilt=a.reuse_built,
                productionRoles=True,commonReservation=False,generatorSha256=sha(Path(__file__)),
                controlSha256=sha(a.out/'game/bin/world-moving-control.txt'),status='UNQUALIFIED',
                limits=['Correctness-only fixed-step/order2 observer; no production FPS claim.',
                        'OFF retains ordinary A31 producer/residency, no cell preparation during tracked driving.',
                        'ON pays bounded reservation/local compilation/retirement itself.',
                        'Post-drive held gates remain diagnostic; night stays ordinary.'])
(a.out/'world-cell-moving-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.out/'world-cell-production-role-manifest.json').write_text(json.dumps({
    'baseElfSha256':binding['elfSha256'],'generatorSha256':sha(Path(__file__)),
    'mode':a.mode,'reuseBuilt':a.reuse_built,'status':'UNQUALIFIED'},indent=2)+'\n')
print(a.out,a.mode,'same-ELF reuse' if a.reuse_built else 'requires native build')
