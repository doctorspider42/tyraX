"""A33 actual-writer cold head execution only; no moving performance claim."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();assert not a.out.exists();repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('q',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert q.sha(a.source/rel)==h,rel
plan=json.loads((a.source/'clip-capacity-manifest.json').read_text())
assert plan['experiment']=='A31-owner' and plan['worldNetProducerCost']
for target in ('emulator','physical'):
    r=json.loads((a.source/f'world-runtime-{target}.json').read_text())
    assert r['valid'] and r['performanceCandidate'] and r['groupRasterEqual']
def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','ring-shader-pins.json'):shutil.copy2(a.source/rel,a.out/rel)
def edit(path,old,new):
    text=path.read_text();assert text.count(old)==1,(path,old);path.write_text(text.replace(old,new))
engine=a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-object-head.hpp',engine/'inc/debug/object_head.hpp')
trial=engine/'inc/debug/head_trial.hpp'
trial.write_text('''#pragma once
namespace HeadTrial {
inline unsigned checks=0,draws=0,words=0,hash=2166136261U;
inline void reset(){checks=draws=words=0;hash=2166136261U;}
}
''')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(renderer,'#include "debug/clip_consumer.hpp"',
     '#include "debug/clip_consumer.hpp"\n#include "debug/object_head.hpp"\n#include "debug/head_trial.hpp"')
anchor='  if (submissionBatchCandidate || kInlineUniforms) {\n    emitUnpack(objectDataPacket, VU1_MVP_MATRIX_ADDR, mvp->data, 4);'
edit(renderer,anchor,'  const u32 headStart=ClipConsumer::observe?packet2_get_qw_count(objectDataPacket):0;\n'+anchor)
anchor='  // Do not terminate the DMA chain here: addBuffersDataToPacket appends the'
edit(renderer,anchor,'  if(ClipConsumer::observe)testObjectHead(bag,mvp,objectDataPacket,headStart,spotActive,mayClip,singleColorEnabled);\n'+anchor)
renderer.write_text(renderer.read_text()+'\n'+(repo/'tools/tyrax2-object-head-runtime.inc').read_text())
header=engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
edit(header,'  void addClipChain(packet2_t* objectDataPacket) const;',
     '  void testObjectHead(StaPipBag*,M4x4*,packet2_t*,u32,bool,bool,bool);\n  void addClipChain(packet2_t* objectDataPacket) const;')
physics=a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'#include "debug/clip_consumer.hpp"',
     '#include "debug/clip_consumer.hpp"\n#include "debug/head_trial.hpp"')
edit(physics,'ClipConsumer::reset();CoarseStrip::reset();ClipConsumer::observe=frame==31;',
     'ClipConsumer::reset();CoarseStrip::reset();HeadTrial::reset();ClipConsumer::observe=frame==31;')
anchor='    printf("WORLDOWNER phase=%u arm=%u sources=%u draws=%u triangles=%u hash=%08x\\n",'
edit(physics,anchor,'    printf("OBJECTHEAD phase=%u arm=%u checks=%u draws=%u words=%u hash=%08x\\n",\n      MotionProbe::phase,arm,HeadTrial::checks,HeadTrial::draws,HeadTrial::words,HeadTrial::hash);fflush(stdout);\n'+anchor)
plan.update(experiment='A33-head',objectHead=True,objectHeadStage='cold-actual-writer-only',
    performanceCandidate=False,baseElfSha256=binding['elfSha256'],
    objectHeadRendererSha256=q.sha(renderer))
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
(a.out/'README.txt').write_text('A33 cold object-head SDK execution. Original hot head generation remains; NOT a speed candidate.\n')
print('Created actual-writer cold head fixture',a.out)
