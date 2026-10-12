"""A31 cold SDK body execution gates, deliberately no performance verdict."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-coarse-strip-analyze.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("plan['experiment']=='A30' and plan['coarseStrip']",
        "plan['experiment']=='A31' and plan['coarseStrip'] and plan['worldBody'] and plan['worldBodyColdOnly']")
replace("assert core.count('buffer->partialStripOrigin=sourceEligible;')==3", """assert core.count('buffer->partialStripOrigin=sourceEligible;')==3
assert sha(root/'tyra/engine/inc/debug/world_strip_plan.hpp')==plan['worldSourceSha256']
assert sha(root/'tyra/engine/inc/debug/world_body.hpp')==plan['worldBodySha256']
assert sha(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp')==plan['worldRendererSha256']
assert 'if (ClipConsumer::observe && buffers[i]->coarseClipStrip &&' in renderer
assert 'compiled==reference' in renderer and 'emitRef(currentPacket,body.data(),' in renderer
assert 'CoarseStrip::active=ClipTrial::mode==7;WorldBody::reset();' in physics
""")
replace("report['reverse']=reverse",r'''def worldEvidence(log):
 records=re.findall(r'WORLDCHAIN phase=(\d+) arm=(\d+) checks=(\d+) words=(\d+) liveHeads=1 coldOnly=1',log)
 assert len(records)==count*5
 for phase in range(count):
  group=[tuple(map(int,r)) for r in records if int(r[0])==phase]
  assert [r[1] for r in group]==list(range(5))
  assert group[1][2]>0 and group[1][3]>group[1][2]*4
  assert all(group[i][2:]==(0,0) for i in (0,2,3,4))
 return records
report['worldBodies']=worldEvidence(text)
for name,bad in (
 ('missing-world',text.replace('WORLDCHAIN phase=0 arm=1','MISSINGWORLD phase=0 arm=1')),
 ('inactive-world',re.sub(r'(WORLDCHAIN phase=0 arm=1 checks=)\d+',r'\g<1>0',text)),
 ('empty-world',re.sub(r'(WORLDCHAIN phase=0 arm=1 checks=\d+ words=)\d+',r'\g<1>0',text)),
 ('control-world',text.replace('WORLDCHAIN phase=0 arm=0 checks=0','WORLDCHAIN phase=0 arm=0 checks=1')),
 ('stale-heads',text.replace('liveHeads=1 coldOnly=1','liveHeads=0 coldOnly=1')),
 ('hot-compile',text.replace('liveHeads=1 coldOnly=1','liveHeads=1 coldOnly=0'))):
 try:worldEvidence(bad)
 except AssertionError:report['negativeControls'].append(name)
 else:raise AssertionError('A31 world negative escaped: '+name)
report['worldCompiledExecution']=True
report['performanceCandidate']=False
report['scope']='A31 cold actual SDK body compilation/execution with live heads; no priced-frame route change or speedup claim'
report['reverse']=reverse''')
replace('coarse-strip-{platform}.json','world-body-{platform}.json')
replace("output=a.output or root/f'world-body-{platform}.json'", """report.pop('contrasts',None)
report.pop('firstOrderGate',None)
output=a.output or root/f'world-body-{platform}.json'""")
exec(compile(source,str(__file__),'exec'))
