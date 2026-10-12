"""A40 complete Hybrid3 paint comparison, original SDK emitter and exact rasters."""
import hashlib,json,re
from pathlib import Path
parent=Path(__file__).with_name('tyrax2-triple-check.py');a38Path=parent
source=parent.read_text();source=source[:source.index('\noutput=a.output or root/')]
def once(old,new):
 global source
 assert source.count(old)==1,old
 source=source.replace(old,new)
once("manifest['phases'] in ([2,3,2,2,3,2],[3,2,3,3,2,3]) and manifest['held']==[2,3,2]",
     "manifest['phases']==[3]*6 and manifest['held']==[3]*3")
once("reverse=manifest['phases'][0]==3","reverse=False")
once("condition='MotionProbe::phase%3!=1' if reverse else 'MotionProbe::phase%3==1'","condition='true'")
once("assert sha(root/headWriter)==parentBinding['files'][headWriter], 'Original SDK head writer changed'",'''
assert sha(root/'parent-head-writer.cpp')==parentBinding['files'][headWriter]
actual=(root/headWriter).read_text()
assert actual.count('#include "debug/paint_triple.hpp"\\n')==1
assert actual.count('    if(paintVu)++PaintTriple::coldHeads;\\n')==1
actual=actual.replace('#include "debug/paint_triple.hpp"\\n','').replace('    if(paintVu)++PaintTriple::coldHeads;\\n','')
assert actual==(root/'parent-head-writer.cpp').read_text(), 'SDK emitter changed beyond cold witness'
''')
# A paint head adds one UNPACK header and two Vec4 payloads. Validate this
# exact delta below, not an impossible same-length or same-mode-bit hash.
once('# Reuse the complete A37 ordinary baseline travel/owner/header/36-raster gate,',
 '''source=source.replace('r[2:-1]','r[2:-2]')
source=source.replace('group[0][2:-1]','group[0][2:-2]')
source=source.replace("'paintBlocked','paintVu','coreTicks'","'paintBlocked','coreTicks'")
# Reuse the complete A37 ordinary baseline travel/owner/header/36-raster gate,''')
exec(compile(source,str(parent),'exec'))
paint=json.loads((root/'paint-triple-manifest.json').read_text())
assert paint['experiment']=='A40' and paint['modes']==[0,1,0,0,1,0] and paint['held']==[0,1,0]
binding=json.loads((root/'a38-native-binding.json').read_text());assert paint['parentElf']==binding['elfSha256']
assert '#define TYRA_VU1_EXP_PAINT 1' in (root/'tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_vu1_experiments.hpp').read_text()
assert 'PaintTriple::enabled && stapip.core.supportsPaintVu(part.envBag.get());' in physics
def paintGate(text,data):
 modes=[tuple(map(int,r)) for r in re.findall(r'\bPAINTMODE phase=(\d+) mode=(\d+)',text)]
 assert modes==list(enumerate(paint['modes']))
 held=[tuple(map(int,r)) for r in re.findall(r'\bPAINTTRIPLE phase=(\d+) arm=(\d+) mode=(\d+) heads=(\d+)',text)]
 assert [(p,arm,m) for p,arm,m,n in held]==[(p,arm,paint['held'][arm]) for p in range(6) for arm in range(3)]
 assert all(n>0 if m else n==0 for p,arm,m,n in held)
 for phase in range(6):
  values=[r['paintVu'] for r in data if r['phase']==phase and r['gas']]
  assert sum(values)>0 if paint['modes'][phase] else sum(values)==0
  heads=contracts['FRONTORACLE'][phase*3:phase*3+3]
  before,on,after=[int(r[-2]) for r in heads]
  assert before==after and on==before+3*held[phase*3+1][3], 'Paint UNPACK head delta'
 return held
held=paintGate(log,rows)
for label,bad in [('missing-paint-mode',log.replace('PAINTMODE phase=1','MISSINGPAINTMODE phase=1')),
 ('inactive-paint-head',re.sub(r'(PAINTTRIPLE phase=1 arm=1 mode=1 heads=)\d+',r'\g<1>0',log))]:
 try:paintGate(bad,rows)
 except AssertionError:negatives.append(label)
 else:raise AssertionError(label)
bad=[dict(r,paintVu=0) for r in rows]
try:paintGate(log,bad)
except AssertionError:negatives.append('inactive-moving-paint')
else:raise AssertionError('inactive-moving-paint')
report.update(experiment='A40',paintHeads=held,paintManifestSha256=sha(root/'paint-triple-manifest.json'),
 a38ReaderSha256=sha(a38Path),sdkEmitterNormalizedSha256=sha(root/'parent-head-writer.cpp'),negativeControls=negatives,
 scope='Known integrated paint EE/VU/EE under Hybrid3; exact rasters/source and prescribed +3qw per paint head, not pure EE cycles')
output=a.output or root/f'paint-triple-{platform}.json';output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(valid=True,platform=platform,images=len(images),controls=len(negatives),heads=held,contrasts=report.get('contrasts')),indent=2))
