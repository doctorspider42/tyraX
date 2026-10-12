"""Qualify A31 all-plane cold execution without changing performance claims."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-body-analyze.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("assert 'compiled==reference' in renderer", """assert plan['worldAllPlanes']
assert sha(root/'tyra/engine/inc/debug/world_mask.hpp')==plan['worldMaskSha256']
assert 'copy.clipPlaneMask=63;' in renderer
assert 'WorldMask::sameBody(reference,compiled)' in renderer""")
replace("CoarseStrip::active=ClipTrial::mode==7;WorldBody::reset();' in physics",
        "CoarseStrip::active=ClipTrial::mode==7;WorldBody::reset();WorldMask::changed=0;' in physics")
replace("report['worldCompiledExecution']=True",r'''def maskEvidence(log):
 rows=re.findall(r'WORLDMASK phase=(\d+) arm=(\d+) changed=(\d+) planes=63 coldOnly=1',log)
 assert len(rows)==count*5
 for phase in range(count):
  group=[tuple(map(int,r)) for r in rows if int(r[0])==phase]
  assert [r[1] for r in group]==list(range(5))
  assert group[1][2]>0 and all(group[i][2]==0 for i in (0,2,3,4))
 return rows
report['worldMasks']=maskEvidence(text)
for name,bad in (
 ('missing-mask',text.replace('WORLDMASK phase=0 arm=1','MISSINGMASK phase=0 arm=1')),
 ('unchanged-mask',re.sub(r'(WORLDMASK phase=0 arm=1 changed=)\d+',r'\g<1>0',text)),
 ('control-mask',text.replace('WORLDMASK phase=0 arm=0 changed=0','WORLDMASK phase=0 arm=0 changed=1')),
 ('partial-mask',text.replace('planes=63 coldOnly=1','planes=1 coldOnly=1'))):
 try:maskEvidence(bad)
 except AssertionError:report['negativeControls'].append(name)
 else:raise AssertionError('A31 mask control escaped: '+name)
report['worldAllPlanes']=True
report['allPlaneScope']='selected native C/TC cold bodies only; no whole-owner or live-route qualification'
report['worldCompiledExecution']=True''')
replace('world-body-{platform}.json','world-mask-{platform}.json')
exec(compile(source,str(__file__),'exec'))
