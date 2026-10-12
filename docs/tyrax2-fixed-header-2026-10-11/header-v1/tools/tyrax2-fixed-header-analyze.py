"""A26 same-ELF travel/byte/raster gates, without additional hot timers."""
from pathlib import Path
source=Path(__file__).with_name('tyrax2-clip-capacity-analyze.py').read_text()
def replace(old,new):
 global source
 assert source.count(old)==1,(old,source.count(old));source=source.replace(old,new)
start=source.index("assert 'fullClipChecks=");end=source.index('qbuffer=root/',start)
source=source[:start]+'''assert plan['fixedHeader'] and plan['experiment']=='A26'
assert sha(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp')==plan['preparationSha256']
assert 'ClipTrial::' not in core and 'ClipConsumer::' not in core
renderer=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
assert 'ClipTrial::mode' not in renderer and 'ClipConsumer::input(buffer)' in renderer
program=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp').read_text()
assert 'packet2_vif_close_unpack_manual(packet,2);' in program
assert 'A26 fixed header differs from SDK auto close' in program
'''+source[end:]
replace('elif merged:assert removed>=merged and 12<max_input<=39 and max_input%3==0',
 'else:assert eligible==clips and merged==removed==max_input==0')
replace('assert all(differences[i]==0 for i in (0,1,2,4)),(phase,view,differences)',
 'assert all(differences[i]==0 for i in (0,1,2,3,4)),(phase,view,differences)')
replace('phases=validate(rows,text)', '''phases=validate(rows,text)
def cold(log):
 inputs=[tuple(map(int,r)) for r in re.findall(r'CONSUMERINPUT phase=(\\d+) arm=(\\d+) calls=(\\d+) verts=(\\d+) hash=(\\d+)',log)]
 checks=[tuple(map(int,r)) for r in re.findall(r'HEADERORACLE phase=(\\d+) arm=(\\d+) checks=(\\d+)',log)]
 assert len(inputs)==len(checks)==count*5
 for phase in range(count):
  group=[r for r in inputs if r[0]==phase];oracle=[r for r in checks if r[0]==phase]
  assert [r[1] for r in group]==[r[1] for r in oracle]==list(range(5))
  assert group[0][2]>0 and group[0][3]>0 and all(r[2:]==group[0][2:] for r in group)
  assert oracle[0][2]>0 and all(r[2]==oracle[0][2] for r in oracle)
 return inputs,checks
inputs,checks=cold(text)
for corrupted in (re.sub(r'HEADERORACLE[^\\r\\n]+','',text),re.sub(r'(HEADERORACLE phase=0 arm=1 checks=)\\d+',r'\\g<1>0',text)):
 try:cold(corrupted)
 except AssertionError:pass
 else:raise AssertionError('Cold header rejection escaped')
''')
replace('negatives=[]',"negatives=['missing-header-oracle','changed-header-oracle']")
replace("output=a.output or root/f'clip-capacity-{platform}.json'",'''report['consumerInputEqual']=True;report['headerOracle']=checks;report['inputs']=inputs
report['controlRasterEqual']=True
report['scope']='Same output; fixed header close; elapsed net work, not pure EE cycles'
if a.physical:report['firstOrderGate']=all(c['savedMeanMs']>0 and c['savedSpatialMs']>0 and c['candidateP95Ms']<=c['maxControlP95Ms']+.15 for c in report['contrasts'])
output=a.output or root/f'fixed-header-{platform}.json' ''')
exec(compile(source,str(Path(__file__)),'exec'))
