"""Prepare six daytime net-price trips, preserving ordinary OFF and normal Auto order."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
report=json.loads((a.source/'world-cell-moving-emulator.json').read_text())
assert report['valid'] and report['deliveryValid'] and report['productionRoles']
assert report['elfSha256']==binding['elfSha256']
assert report['onLogSha256']==sha(a.source/'emulator/emulator.log')
assert len(report['images'])==36 and all(r['differentBytes']==0 for r in report['images'])
assert all(r[3]>0 for r in report['onLocalRebuilds'])
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
game=a.out/'game/src/terrain_game.cpp';t=game.read_text()
old='''  if(MotionProbe::finishPending){
    if(MotionProbe::phase<3)WorldMoving::finish();
    renderJoinCheckpoint();renderCompleteTerrainCheckpoint();
  }'''
assert t.count(old)==1
t=t.replace(old,'''  if(MotionProbe::finishPending){
    WorldMoving::finish();captureCompleteWorldPriceCheckpoint();
  }''')
old='  WorldHeldOrder::active=true;WorldHeldOrder::interleaved=true;'
assert t.count(old)==1;t=t.replace(old,'  WorldHeldOrder::active=false;')
t=t.replace('WorldMoving::enabled && MotionProbe::phase<3 &&','WorldMoving::enabled && MotionProbe::phase<6 &&')
old='  WorldMoving::begin(MotionProbe::phase);'
assert t.count(old)==1
t=t.replace(old,old+'\n  if(MotionProbe::offset==MotionProbe::gasStart){g_vehShakeClock=0;g_camShakeClock=0;}')
old='  saveValues[0] = MotionProbe::phase >= 3 ? 1.0F : 0.0F;'
assert t.count(old)==1;t=t.replace(old,'  saveValues[0] = 0.0F;')
old='''    if(!roads || (MotionProbe::phase>=3 ? lights==0 : lights!=0) || daynight::g_hour!=(MotionProbe::phase>=3?0.0F:12.0F) || !daynight::g_paused) MotionProbe::valid=false;'''
assert t.count(old)==1
t=t.replace(old,'''    if(!roads || lights!=0 || daynight::g_hour!=12.0F || !daynight::g_paused) MotionProbe::valid=false;''')
start=t.index('  if(MotionProbe::phase<3 && MotionProbe::offset>=1100 && !MotionProbe::finishPending){\n    ++WorldMoving::frames;')
end=t.index('  // Frame extrapolation (docs/frame-extrapolation.md)',start)
t=t[:start]+'''  if(MotionProbe::phase<6 && MotionProbe::offset>=1100 && !MotionProbe::finishPending){
    ++WorldMoving::frames;
    if(MotionProbe::gas())++WorldMoving::gasFrames;
    WorldMoving::requested+=WorldCellComplete::requested;
    WorldMoving::submitted+=WorldCellComplete::submitted;
    WorldMoving::fallback+=WorldCellComplete::fallback;
    if(completeTerrainDirty)++WorldMoving::dirtyFrames;
  }
'''+t[end:]
t+='\n'+(repo/'tyrax2-world-cell-price-checkpoint.inc').read_text()
game.write_text(t)
header=a.out/'game/inc/terrain_game.hpp';t=header.read_text()
old='  void renderCompleteTerrainCheckpoint();'
assert t.count(old)==1;t=t.replace(old,old+'\n  void captureCompleteWorldPriceCheckpoint();')
header.write_text(t)
header=a.out/'game/inc/world_moving.hpp';t=header.read_text()
start=t.index('inline void configure(){');end=t.index('inline void finish(){',start)
t=t[:start]+'''inline void begin(unsigned p){
  constexpr unsigned modes[]={0,1,0,1,0,1};
  enabled=p<6 && modes[p];
  if(phase==p)return;
  phase=p;frames=gasFrames=requested=submitted=fallback=prepares=prepareTicks=dirtyFrames=captures=0;
  localRebuilds=localTicks=heldOldReaders=0;
  printf("A44PRICECONFIG phase=%u candidate=%u autoOrder=1 fixedStep=1 clipObserver=0 shakeReset=1\\n",p,unsigned(enabled));
  fflush(stdout);
}
'''+t[end:]
header.write_text(t)
header=a.out/'tyra/engine/inc/debug/motion_probe.hpp';t=header.read_text()
old='  ClipTrial::observe=!finishPending && offset>=gasStart && offset%128==64;'
assert t.count(old)==1;t=t.replace(old,'  ClipTrial::observe=false;')
header.write_text(t)
(a.out/'world-cell-price-manifest.json').write_text(json.dumps({
    'baseElfSha256':binding['elfSha256'],'generatorSha256':sha(Path(__file__)),
    'modes':[0,1,0,1,0,1],'daytimeOnly':True,'bufferCount':2,'movingAutoOrder':True,
    'fixedSceneStep':True,'inPriceReadbacks':False,'ordinaryControlUnchanged':True,
    'version':2,'clipObserver':False,'shakePhaseReset':True,'samePoseEndpointArms':3,
    'status':'UNQUALIFIED','limits':[
        'Emulator correctness only; physical same-ELF net and reverse pricing required.',
        'Normal Auto moving order; fixed order only in post-price endpoint image checkpoint.',
        'Fixed simulation step is controlled workload, not final ordinary-player FPS qualification.',
        'Cold work at offset1100 is outside samples; local replacement cost during driving remains included.',
        'No nighttime cell or60fps claim.'
    ]},indent=2)+'\n')
print(a.out,'UNQUALIFIED six-day production net/reverse, Auto order, no in-price readback')
