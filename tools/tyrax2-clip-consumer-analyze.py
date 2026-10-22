"""A25 reader: reuse A24 travel/binding gates, replacing only intervention gates."""
import sys
from pathlib import Path
source=Path(__file__).with_name('tyrax2-clip-capacity-analyze.py').read_text()
def replace(old,new):
 global source
 assert source.count(old)==1,(old,source.count(old));source=source.replace(old,new)
start=source.index("assert 'fullClipChecks=");end=source.index('qbuffer=root/',start)
source=source[:start]+'''assert plan['consumerOnly'] and plan['experiment']=='A25'
assert sha(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp')==plan['preparationSha256']
assert 'ClipTrial::' not in core and 'ClipConsumer::' not in core
renderer=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
assert '? getCullProgramByBag(buffer->bag) : getClipProgramByBag(buffer->bag)' in renderer
assert 'stapip.core.setVU1Clipping(CLIP_VU1)' in (root/'game/src/terrain_game.cpp').read_text()
assert 'inline constexpr bool CLIP_VU1S[SCENE_COUNT] = {true, true, true};' in (root/'game/inc/scene_data.hpp').read_text()
'''+source[end:]
replace("elif merged:assert removed>=merged and 12<max_input<=39 and max_input%3==0", "else:assert eligible==clips and merged==removed==max_input==0")
replace("assert all(differences[i]==0 for i in (0,1,2,4)),(phase,view,differences)","assert all(differences[i]==0 for i in (0,2,3,4)),(phase,view,differences)")
replace('phases=validate(rows,text)', '''phases=validate(rows,text)
def validate_inputs(log):
 inputs=[tuple(map(int,r)) for r in re.findall(r'CONSUMERINPUT phase=(\\d+) arm=(\\d+) calls=(\\d+) verts=(\\d+) hash=(\\d+)',log)]
 assert len(inputs)==count*5
 for phase in range(count):
  group=[r for r in inputs if r[0]==phase]
  assert [r[1] for r in group]==list(range(5))
  assert group[0][2]>0 and group[0][3]>0
  assert all(r[2:]==group[0][2:] for r in group),('EE input mismatch',phase,group)
 return inputs
inputs=validate_inputs(text)
for corrupted in (re.sub(r'CONSUMERINPUT[^\\r\\n]+','',text),re.sub(r'(CONSUMERINPUT phase=0 arm=1 calls=)\\d+',r'\\g<1>0',text)):
 try:validate_inputs(corrupted)
 except AssertionError:pass
 else:raise AssertionError('Cold input rejection escaped')
''')
replace('negatives=[]',"negatives=['missing-consumer-input','changed-consumer-input']")
replace("output=a.output or root/f'clip-capacity-{platform}.json'", "report['consumerInputEqual']=True;report['inputs']=inputs\nreport['controlRasterEqual']=True\nreport['consumerActive']=all(r['eligible']==r['clipCalls']>0 for r in phases if r['mode']==7)\nreport['scope']='EE preparation retained; consumer/output/program emission contrast, not pure VU cycles'\noutput=a.output or root/f'clip-consumer-{platform}.json'")
exec(compile(source,str(Path(__file__)), 'exec'))
