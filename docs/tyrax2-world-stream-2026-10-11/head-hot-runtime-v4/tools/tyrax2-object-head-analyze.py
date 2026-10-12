"""A33 cold whole object-head execution; forbid moving performance attribution."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-runtime-analyze.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("plan['experiment']=='A31-owner'",
        "plan['experiment']=='A33-head' and plan['objectHead'] and not plan['performanceCandidate']")
replace("assert 'CoarseStrip::reset();ClipConsumer::observe=frame==31;' in physics",
        "assert 'CoarseStrip::reset();HeadTrial::reset();ClipConsumer::observe=frame==31;' in physics")
replace("report['performanceCandidate']=True", """assert plan['objectHeadStage']=='cold-actual-writer-only'
assert sha(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp')==plan['objectHeadRendererSha256']
assert 'if(ClipConsumer::observe)testObjectHead(' in renderer
assert 'A33 head differs from actual writer' in renderer and 'memcpy(packet->base+start,output.data(),n*16)' in renderer
assert 'testObjectHead' not in core
def heads(log):
 records=[tuple(int(v,16 if i==5 else 10) for i,v in enumerate(row)) for row in
  re.findall(r'\\bOBJECTHEAD phase=(\\d+) arm=(\\d+) checks=(\\d+) draws=(\\d+) words=(\\d+) hash=([0-9a-f]{8})',log)]
 assert len(records)==count*5
 for phase in range(count):
  group=records[phase*5:phase*5+5]
  assert [(r[0],r[1]) for r in group]==[(phase,arm) for arm in range(5)]
  assert len({(r[2],r[4],r[5]) for r in group})==1
  for _,arm,checks,draws,words,digest in group:
   assert checks>0 and words>=checks*11 and digest
   assert (draws==checks) if arm==1 else draws==0
 return records
headRecords=heads(text)
for label,bad in (
 ('missing-head',text.replace('OBJECTHEAD phase=0 arm=1','MISSINGOBJECTHEAD phase=0 arm=1')),
 ('inactive-head',re.sub(r'(OBJECTHEAD phase=0 arm=1 checks=\\d+ draws=)\\d+',r'\\g<1>0',text)),
 ('head-control',re.sub(r'(OBJECTHEAD phase=0 arm=0 checks=\\d+ draws=)\\d+',r'\\g<1>1',text)),
 ('zero-head-checks',re.sub(r'(OBJECTHEAD phase=0 arm=1 checks=)\\d+',r'\\g<1>0',text)),
 ('zero-head-words',re.sub(r'(OBJECTHEAD phase=0 arm=1[^\\r\\n]* words=)\\d+',r'\\g<1>0',text)),
 ('zero-head-digest',re.sub(r'(OBJECTHEAD phase=0 arm=1[^\\r\\n]* hash=)[0-9a-f]{8}',r'\\g<1>00000000',text))):
 try:heads(bad)
 except AssertionError:report['negativeControls'].append(label)
 else:raise AssertionError('A33 head negative escaped: '+label)
report['objectHeadContracts']=headRecords
report['performanceCandidate']=False
report['objectHeadStage']='cold-actual-writer-only'
report['hotOriginalHead']=True
report.pop('contrasts',None);report.pop('firstOrderGate',None)""")
replace('world-runtime-{platform}.json','object-head-{platform}.json')
replace('Producer-owned compiled partial-world VIF bodies; live heads and same coarse native shader both arms; full-source/residual/moving/raster gates; net elapsed, not pure EE cycles',
        'Cold actual SDK post-wrap object-head template execution only; original hot head generation retained; NOT a moving head speed result')
# Remove inherited late physical/reverse pricing, not just the earlier fields.
start=source.index("if a.physical:report['firstOrderGate']=")
end=source.index("\nreport['reverse']=",start)
source=source[:start]+source[end+1:]
start=source.index('if a.physical and reverse:\n')
end=source.index('output=a.output or root/',start)
source=source[:start]+source[end:]
exec(compile(source,str(__file__),'exec'))
