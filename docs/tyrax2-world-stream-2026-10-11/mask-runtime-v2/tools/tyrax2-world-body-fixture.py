"""A31 cold SDK body compiler/execution gate; no priced-frame route change."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('source',type=Path,help='Qualified forward A30 fixture')
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert q.sha(a.source/rel)==h,rel
plan=json.loads((a.source/'clip-capacity-manifest.json').read_text())
assert plan['experiment']=='A30' and not plan['reverse'] and not plan['ringStateOnce']
for platform in ('emulator','physical'):
    report=json.loads((a.source/f'coarse-strip-{platform}.json').read_text())
    assert report['valid'] and report['groupRasterEqual'] and report['coarseSourceDeliveryEqual']
    assert len(report['images'])==12
def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):
    shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','ring-shader-pins.json'):
    shutil.copy2(a.source/rel,a.out/rel)
engine=a.out/'tyra/engine'
for name,dest in (('tyrax2-world-strip-plan.hpp','world_strip_plan.hpp'),
                  ('tyrax2-world-body.hpp','world_body.hpp')):
    shutil.copy2(repo/'tools'/name,engine/'inc/debug'/dest)
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
text=renderer.read_text()
text=q.once(text,'#include "debug/clip_consumer.hpp"',
           '#include "debug/clip_consumer.hpp"\n#include "debug/clip_capacity.hpp"\n#include "debug/world_body.hpp"')
anchor='#if TYRA_STAPIP_BAKED_STREAM\n    if (replayPartialBakedBuffer'
text=q.once(text,anchor,(repo/'tools/tyrax2-world-body-fragment.inc').read_text()+'\n'+anchor)
renderer.write_text(text)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
text=physics.read_text()
text=q.once(text,'#include "debug/clip_consumer.hpp"',
           '#include "debug/clip_consumer.hpp"\n#include "debug/world_body.hpp"')
text=q.once(text,'CoarseStrip::active=ClipTrial::mode==7;\n    for(unsigned frame=',
           'CoarseStrip::active=ClipTrial::mode==7;WorldBody::reset();\n    for(unsigned frame=')
anchor='    printf("RINGFALLBACK phase=%u arm=%u calls=%u triangles=%u\\n",'
text=q.once(text,anchor,'''    TYRA_ASSERT(arm==1?(WorldBody::checks>0 && WorldBody::words>0):WorldBody::checks==0,
      "A31 cold compiled execution admission");
    printf("WORLDCHAIN phase=%u arm=%u checks=%u words=%u liveHeads=1 coldOnly=1\\n",
      MotionProbe::phase,arm,WorldBody::checks,WorldBody::words);fflush(stdout);
'''+anchor)
physics.write_text(text)
plan.update(experiment='A31',worldBody=True,worldBodyColdOnly=True,
            worldSourceSha256=q.sha(engine/'inc/debug/world_strip_plan.hpp'),
            worldBodySha256=q.sha(engine/'inc/debug/world_body.hpp'),
            worldRendererSha256=q.sha(renderer),baseElfSha256=binding['elfSha256'],
            status='UNQUALIFIED cold compiler/execution; NOT performance candidate')
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
print(a.out,'UNQUALIFIED A31 cold actual SDK compilation/execution')
