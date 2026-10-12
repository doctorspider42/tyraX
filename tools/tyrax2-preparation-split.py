"""Derive an all-ordinary, observer-controlled preparation split from bound A18 V5."""
import argparse, hashlib, json, shutil, subprocess, sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('out',type=Path)
p.add_argument('--geometry',action='store_true',help='A21: separate object setup from geometry dispatch')
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(repo/'tools/tyrax2-bag-tape-clone.py'),str(a.source),str(a.out)],check=True)
assert (a.out/'tyra/engine/inc/debug/command_size.hpp').exists()
def edit(rel,old,new):
    path=a.out/rel;t=path.read_text(encoding='utf-8-sig')
    assert t.count(old)==1,(rel,old,t.count(old))
    path.write_text(t.replace(old,new),encoding='utf-8')

engine='tyra/engine/'
shutil.copy2(repo/'tools/tyrax2-preparation-split.hpp',a.out/engine/'inc/debug/preparation_split.hpp')
edit(engine+'inc/debug/command_pricing.hpp',
     'constexpr U modes[stages]={0,0,1,0,2,0,2,0,1,0};',
     'constexpr U modes[stages]={0,0,0,0,0,0,0,0,0,0};')
game='game/src/gen/game_physics.gen.cpp'
edit(game,'#include "debug/command_pricing.hpp"',
     '#include "debug/command_pricing.hpp"\n#include "debug/preparation_split.hpp"')
edit(game,'  const unsigned stall=core.getStallTotal(),start=TapePricing::tick();',
     '  PreparationSplit::begin(PreparationSplit::mode(stage));\n  const unsigned stall=core.getStallTotal(),start=TapePricing::tick();')
edit(game,'    prepare=TapePricing::tick()-start;',
     '    prepare=TapePricing::tick()-start;\n    PreparationSplit::boundary();')
edit(game,'  const unsigned whole=TapePricing::tick()-start;',
     '  const unsigned whole=TapePricing::tick()-start;\n  PreparationSplit::end();')
edit(game,'    TapePricing::samples[TapePricing::used++]={stage,local,whole,pacing,startup,prepare};',
     '''    TapePricing::samples[TapePricing::used++]={stage,local,whole,pacing,startup,prepare};
    TYRA_ASSERT(PreparationSplit::used<1152,"Split overflow");
    PreparationSplit::samples[PreparationSplit::used++]={stage,local,PreparationSplit::policy,
      PreparationSplit::errors,PreparationSplit::parts[0],PreparationSplit::parts[1]};''')
edit(game,'  ++iteration;if(iteration==TapePricing::length*TapePricing::stages)TapePricing::dump();',
     '''  ++iteration;if(iteration==TapePricing::length*TapePricing::stages){
    TYRA_ASSERT(PreparationSplit::used==1152,"Missing split samples");
    CommandTape::binary("host:command-split.bin",PreparationSplit::samples,sizeof(PreparationSplit::samples));
    printf("SPLITDONE samples=%u bytes=%u\\n",PreparationSplit::used,unsigned(sizeof(PreparationSplit::samples)));fflush(stdout);
    TapePricing::dump();
  }''')
hooks=[]
def hook(rel,anchor,kind):
    edit(engine+rel,anchor,anchor+'\n  PreparationSplit::Scope preparationSplit(PreparationSplit::'+kind+');')
    hooks.append(dict(file=engine+rel,anchor=anchor,kind=kind))
core='src/renderer/3d/pipeline/static/core/stapip_core.cpp'
qb='src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
queue='src/renderer/core/paths/path1/vif1_queue.cpp'
for rel in (core,qb,queue):
    path=a.out/engine/rel
    path.write_text('#include "debug/preparation_split.hpp"\n'+path.read_text(encoding='utf-8-sig'),encoding='utf-8')
for rel,anchor in ((core,'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {'),
                   (core,'void StaPipCore::onFrameEnd() {'),
                   (qb,'void StaPipQBufferRenderer::flushPendingPacket() {')):
    hook(rel,anchor,'StaPip')
# Entry callbacks are outside submission scope; nested producer callbacks retain
# their own category even if invoked later from a queue drain or barrier.
hook(queue,'  closeOpenChain();\n#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT\n  u32 snapshotRecords = 0;','Queue')
hook(queue,'  const u32 qwords = packet2_get_qw_count(const_cast<packet2_t*>(packet));','Queue')
for anchor in ('void Vif1Queue::beginRecordingFrame(bool pipeline) {',
               'void Vif1Queue::endRecordingFrame() {',
               'void Vif1Queue::recordBarrier() {',
               'void Vif1Queue::drain() {',
               'void Vif1Queue::waitFor(u32 sequence) {',
               'void Vif1Queue::enqueue(const void* chain, u32 sequence) {',
               '  if (!nativePendingSequence) return;'):
    hook(queue,anchor,'Queue')
if a.geometry:
    edit(engine+'inc/debug/preparation_split.hpp',
         '#define PREPARATION_SPLIT_GEOMETRY 0','#define PREPARATION_SPLIT_GEOMETRY 1')
    edit(engine+core,'  HardwareTrace::Scope traceDispatch("Dispatch");',
         '  PreparationSplit::Scope geometrySplit(PreparationSplit::Geometry);\n  HardwareTrace::Scope traceDispatch("Dispatch");')
    edit(engine+qb,'PreparationSplit::Scope preparationSplit(PreparationSplit::StaPip);',
         'PreparationSplit::Scope preparationSplit(PreparationSplit::Geometry);')
    for h in hooks:
        if h['file']==engine+qb:h['kind']='Geometry'
    hooks.append(dict(file=engine+core,anchor='HardwareTrace::Scope traceDispatch("Dispatch");',kind='Geometry'))
manifest=dict(parent=str(a.source),parentElf=json.loads((a.source/'native-binding.json').read_text())['elfSha256'],
    policies=[0,0,1,2,1,0,1,2,1,0],routes=[0]*10,hooks=hooks,geometry=a.geometry,
    scope='Exclusive elapsed StaPip/queue plus residual, split before endFrame; includes waits/preemption and observer cost; not pure EE or live FPS')
(a.out/'preparation-split-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
# The old binding belongs to the unmodified parent, never to this new build.
(a.out/'native-binding.json').unlink();(a.out/'symbols.txt').unlink()
print(a.out)
