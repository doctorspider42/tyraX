"""Build a same-ELF OFF/ON moving correctness experiment from qualified A44 cells."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path)
p.add_argument('--mode', choices=('OFF', 'ON'), required=True)
p.add_argument('--reuse-built', action='store_true')
p.add_argument('--recover-hybrid-control', action='store_true')
a = p.parse_args()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert not a.out.exists()
binding = json.loads((a.source / 'native-binding.json').read_text())
for rel, digest in binding['files'].items():
    assert sha(a.source / rel) == digest, rel
assert sha(a.source / 'game/bin/vehicle-playground.elf') == binding['elfSha256']
log = (a.source / 'emulator/emulator.log').read_text(errors='replace')
assert not (a.reuse_built and a.recover_hybrid_control)
if a.recover_hybrid_control:
    assert 'A44 moving requires Hybrid2 display' in (a.source / 'game/bin/log.txt').read_text(errors='replace')
else:
    assert 'DRIVEDONE valid=1' in log
if a.reuse_built or a.recover_hybrid_control:
    parent = json.loads((a.source / 'world-cell-moving-manifest.json').read_text())
    assert parent['mode'] == 'OFF' and a.mode == ('OFF' if a.recover_hybrid_control else 'ON')
else:
    conditioned = json.loads((a.source / 'world-cell-conditioned-manifest.json').read_text())
    assert conditioned['fixedSceneStep'] and conditioned['heldMode'] == 2
    for phase in range(3):
        report = json.loads((a.source / f'world-cell-view-emulator-phase{phase}.json').read_text())
        assert report['valid'] and not report['ordinaryOnly']
        assert report['elfSha256'] == binding['elfSha256']
        assert report['logSha256'] == sha(a.source / 'emulator/emulator.log')
        for row in report['images']:
            for rel, digest in zip(row['files'], row['hashes']):
                assert sha(a.source / rel) == digest, rel

def ignore(path, names):
    return [n for n in names if n in ('obj', '.git') or
            (Path(path).name == 'bin' and (n in ('log.txt', 'drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-', 'command-', 'cell-', 'source-', 'moving-')) and n.endswith('.bin'))]

for part in ('game', 'tyra/engine'):
    shutil.copytree(a.source / part, a.out / part, ignore=ignore)
for rel in ('tyra/Makefile.base', 'authored-manifest.json',
            'world-cell-terrain-manifest.json', 'world-cell-view-manifest.json',
            'world-source-manifest.json', 'world-source-switch-manifest.json',
            'world-command-source-manifest.json', 'world-command-order-manifest.json',
            'world-cell-conditioned-manifest.json'):
    shutil.copy2(a.source / rel, a.out / rel)

if not a.reuse_built and not a.recover_hybrid_control:
    header = r'''#pragma once
#include "debug/debug.hpp"
#include "world_held_order.hpp"
#include <cstdio>
namespace WorldMoving {
inline bool configured=false,enabled=false;
inline unsigned phase=~0U,frames=0,gasFrames=0,requested=0,submitted=0,fallback=0;
inline unsigned prepares=0,prepareTicks=0,dirtyFrames=0,captures=0;
inline unsigned ticks(){unsigned t;asm volatile("mfc0 %0,$9":"=r"(t));return t;}
inline void configure(){
  if(configured)return;
  FILE* f=fopen("host:world-moving-control.txt","rb");
  const int mode=f?fgetc(f):-1,extra=f?fgetc(f):-1;
  const bool closed=f && fclose(f)==0;
  TYRA_ASSERT(closed && (mode=='0' || mode=='1') && extra==EOF,"A44 moving control invalid");
  enabled=mode=='1';configured=true;
  printf("A44MOVINGCONFIG enabled=%u fixedStep=1 order=2 deferred=1\n",unsigned(enabled));
  fflush(stdout);
}
inline void begin(unsigned p){
  configure();
  if(phase==p)return;
  phase=p;frames=gasFrames=requested=submitted=fallback=prepares=prepareTicks=dirtyFrames=captures=0;
}
inline void finish(){
  printf("A44MOVING phase=%u enabled=%u frames=%u gasFrames=%u requested=%u submitted=%u fallback=%u prepares=%u prepareTicks=%u dirtyFrames=%u captures=%u\n",
    phase,unsigned(enabled),frames,gasFrames,requested,submitted,fallback,prepares,prepareTicks,dirtyFrames,captures);
  fflush(stdout);
}
}
'''
    (a.out / 'game/inc/world_moving.hpp').write_text(header)
    game = a.out / 'game/src/terrain_game.cpp'
    t = game.read_text()
    anchor = '#include "debug/triple_trial.hpp"'
    assert t.count(anchor) == 1
    t = t.replace(anchor, anchor + '\n#include "world_moving.hpp"\n#include <screenshot.h>')
    anchor = '  if(MotionProbe::finishPending){renderJoinCheckpoint();renderCompleteTerrainCheckpoint();}'
    assert t.count(anchor) == 1
    t = t.replace(anchor, '''  if(MotionProbe::finishPending){
    if(MotionProbe::phase<3)WorldMoving::finish();
    renderJoinCheckpoint();renderCompleteTerrainCheckpoint();
  }
  WorldMoving::begin(MotionProbe::phase);
  WorldHeldOrder::active=true;WorldHeldOrder::interleaved=true;
  WorldCellComplete::active=false;
  if(MotionProbe::phase<3 && MotionProbe::offset>=1100 && !MotionProbe::finishPending){
    const bool dirty=completeTerrainDirty || !completeTerrainSlots;
    const unsigned start=WorldMoving::ticks();
    TYRA_ASSERT(prepareCompleteTerrain(),"A44 moving pre-record preparation failed");
    if(dirty){++WorldMoving::prepares;WorldMoving::prepareTicks+=WorldMoving::ticks()-start;}
    WorldCellComplete::active=WorldMoving::enabled;
  }
  WorldCellComplete::requested=WorldCellComplete::submitted=WorldCellComplete::fallback=0;''')
    anchor = '  engine->renderer.endFrame();'
    assert t.count(anchor) == 1
    capture = r'''
  if(MotionProbe::phase<3 && MotionProbe::offset>=1100 && !MotionProbe::finishPending){
    ++WorldMoving::frames;
    if(MotionProbe::gas())++WorldMoving::gasFrames;
    WorldMoving::requested+=WorldCellComplete::requested;
    WorldMoving::submitted+=WorldCellComplete::submitted;
    WorldMoving::fallback+=WorldCellComplete::fallback;
    if(completeTerrainDirty)++WorldMoving::dirtyFrames;
    if(MotionProbe::gas() && MotionProbe::offset%128==0){
      auto& core=engine->renderer.core;core.synchronizeFrame();core.sync.align2D();
      const unsigned stage=MotionProbe::phase*4096+MotionProbe::offset;
      printf("A44MOVINGFRAME stage=%u enabled=%u requested=%u submitted=%u fallback=%u dirty=%u\n",
        stage,unsigned(WorldMoving::enabled),WorldCellComplete::requested,WorldCellComplete::submitted,
        WorldCellComplete::fallback,unsigned(completeTerrainDirty));
      for(unsigned kind=0;kind<3;++kind){
        auto* fb=kind==2?core.gs.getPreviousFrameBuffer():core.gs.getPreviousRealFrameBuffer();
        unsigned address=fb->address,width=fb->width,height=fb->height,psm=fb->psm;
        if(kind==1){auto* texture=core.envMap.getTexture()->vramResident;
          TYRA_ASSERT(texture,"A44 moving missing env");address=texture->address;width=height=128;psm=texture->psm;}
        TYRA_ASSERT(width<=1024 && (psm==0 || psm==2),"A44 moving readback format");
        if(kind==2)TYRA_ASSERT(core.gs.getFrameBufferCount()==2 && psm==2,"A44 moving requires Hybrid2 display");
        char path[80];snprintf(path,sizeof(path),"host:moving-%u-%s.bin",stage,kind==2?"display":kind==1?"env":"main");
        FILE* f=fopen(path,"wb");TYRA_ASSERT(f,"A44 moving readback open");
        unsigned h[4]={width,height,psm,stage};TYRA_ASSERT(fwrite(h,sizeof(h),1,f)==1,"A44 moving header");
        alignas(16) static unsigned line[1024];
        for(unsigned y=0;y<height;++y){
          TYRA_ASSERT(ps2_screenshot(line,address/64,0,y,width,1,psm),"A44 moving GS readback");
          FlushCache(0);TYRA_ASSERT(fwrite(line,psm==2?2:4,width,f)==width,"A44 moving pixels");
        }
        TYRA_ASSERT(fclose(f)==0,"A44 moving close");
      }
      ++WorldMoving::captures;fflush(stdout);
    }
  }
'''
    t = t.replace(anchor, anchor + capture)
    game.write_text(t)
elif a.reuse_built:
    for rel in ('native-binding.json', 'symbols.txt', 'build.log'):
        shutil.copy2(a.source / rel, a.out / rel)
    assert sha(a.out / 'game/bin/vehicle-playground.elf') == binding['elfSha256']

if not a.reuse_built:
    engine = a.out / 'tyra/engine/src/engine.cpp'
    t = engine.read_text()
    old = '''    renderer.core.setDiagnosticTriple(MotionProbe::phase%3!=1);
    printf("TRIPLEMODE phase=%u requested=%u actual=%u\\n",MotionProbe::phase,unsigned(MotionProbe::phase%3!=1),renderer.core.gs.getFrameBufferCount());fflush(stdout);'''
    assert t.count(old) == 1, 'Expected inherited A38 buffer-mode experiment'
    t = t.replace(old, '''    renderer.core.setDiagnosticTriple(false);
    TYRA_ASSERT(renderer.core.gs.getFrameBufferCount()==2,"A44 moving buffer count");
    printf("A44BUFFER phase=%u requested=2 actual=%u\\n",MotionProbe::phase,renderer.core.gs.getFrameBufferCount());fflush(stdout);''')
    engine.write_text(t)

(a.out / 'game/bin/world-moving-control.txt').write_bytes(b'1' if a.mode == 'ON' else b'0')
(a.out / 'world-cell-moving-manifest.json').write_text(json.dumps({
    'baseElfSha256': binding['elfSha256'], 'generatorSha256': sha(Path(__file__)),
    'mode': a.mode, 'reuseBuilt': a.reuse_built, 'fixedSceneStep': True,
    'bufferCount': 2, 'recoveredHybridControl': a.recover_hybrid_control,
    'movingOrder': 2, 'commonReservation': True, 'deferredPreparation': True,
    'controlSha256': sha(a.out / 'game/bin/world-moving-control.txt'),
    'status': 'UNQUALIFIED', 'limits': [
        'Correctness only: forced moving order and synchronous readback are not production FPS.',
        'Original in-record terrain streaming retained; fallback/dirty frames must be evaluated.',
        'Whole-set cold preparation counted separately, not hidden as dispatch cost.',
        'Night uses ordinary rendering; no nighttime cell admission claim.'
    ]}, indent=2) + '\n')
print(a.out, a.mode, 'same-ELF reuse' if a.reuse_built else 'requires native build')
