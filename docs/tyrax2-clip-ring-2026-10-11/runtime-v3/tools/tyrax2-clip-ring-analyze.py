"""A29 ordered input, native activation, SDK header and exact raster gates."""
from pathlib import Path
base = Path(__file__).with_name('tyrax2-terrain-focus-analyze.py').read_text()
finish = "exec(compile(source, str(Path(__file__)), 'exec'))"
assert base.count(finish) == 1
exec(compile(base.replace(finish, ''), str(Path(__file__)), 'exec'))
replace("plan['terrainFocus'] and not plan['fixedHeader'] and plan['experiment']=='A28'",
        "plan['clipRing'] and plan['terrainFocus'] and not plan['fixedHeader'] and plan['experiment']=='A29'")
replace("assert 'ClipTrial::' not in core and 'ClipConsumer::' not in core", """assert 'ClipTrial::' not in core
assert ('ClipConsumer::noteFallback(n);' in core)==plan.get('nativeFallback',False)""")
replace("assert sha(qbuffer)=='ba059bd60f30728a7ebb0e96ca2e370676a9f26f0c278b379b0a02f130298eab'", """assert sha(qbuffer)==plan['qbufferSha256']
assert sha(root/'tyra/engine/inc/debug/clip_consumer.hpp')==plan['consumerSha256']
assert 'fillNativeClipStrip' in qbuffer.read_text()
assert 'if(native)buffer->fillNativeClipStrip(pkg,first,n);' in core
assert 'setDiagnosticClipRing' in renderer and 'A29 displaced resident billboards' in renderer
assert 'reinterpret_cast<u32*>(packet->next)[-1]=2U|(buffer->nativeClipParity&1U);' in program
for rel,h in json.loads((root/'ring-shader-pins.json').read_text()).items():
 assert sha(root/'tyra/engine/src/renderer/3d/pipeline/static/core'/rel)==h,rel
symbols=(root/'symbols.txt').read_text()
def words(stem):
 values=[]
 for suffix in ('Start','End'):
  matches=re.findall(r'^([0-9a-f]+) T '+re.escape(stem+'Code'+suffix)+r'$',symbols,re.M)
  assert len(matches)==1,stem
  values.append(int(matches[0],16))
 assert values[1]>values[0] and (values[1]-values[0])%8==0
 return (values[1]-values[0])//8
assert words('StaPipVU1Clip_C_ring_hybrid_fold_')==392
assert words('StaPipVU1Clip_TC_ring_hybrid_fold_paint_')==464
resident=sum(words('StaPipVU1'+stem+'_') for stem in
 ('Cull_C','Cull_D_FOLD','Cull_TC','Cull_TD_FOLD','Cull_TCE_ENVN_PAINT',
  'Billboard_C','Billboard_T','Clip_C_ring_hybrid_fold','Clip_TC_ring_hybrid_fold_paint'))
assert resident==2034 and resident<=2042
""")
replace("assert 'stableTerrainFocus=ClipTrial::mode'+('!=7' if reverse else '==7')+' || TerrainFocus::checkpoint' in physics", """assert not reverse and 'stableTerrainFocus=true;' in physics
assert 'stapip.core.setDiagnosticClipRing(ClipTrial::mode==7);' in game
assert 'stapip.core.setDiagnosticClipRing(ClipTrial::mode==7);' in physics
assert 'stapip.core.setDiagnosticClipRing(active==7);' in physics""")
replace("assert oracle[0][2]>0 and all(r[2]==oracle[0][2] for r in oracle)", """assert oracle[0][2]>0 and all(oracle[i][2]==oracle[0][2] for i in (0,2,3,4))
  assert 0<oracle[1][2]<oracle[0][2]""")
replace('return inputs,checks', r'''rings=[tuple(map(int,r)) for r in re.findall(r'RINGCONTRACT phase=(\d+) arm=(\d+) calls=(\d+) triangles=(\d+)',log)]
 assert len(rings)==count*5
 for phase in range(count):
  group=[r for r in rings if r[0]==phase]
  assert [r[1] for r in group]==list(range(5))
  assert group[1][2]>0 and group[1][3]>=group[1][2]
  assert group[1][3]<=next(r[2] for r in inputs if r[:2]==(phase,1))
  assert all(group[i][2:]==(0,0) for i in (0,2,3,4))
 if plan.get('nativeFallback',False):
  fallback=[tuple(map(int,r)) for r in re.findall(r'RINGFALLBACK phase=(\d+) arm=(\d+) calls=(\d+) triangles=(\d+)',log)]
  assert len(fallback)==count*5
  for phase in range(count):
   group=[r for r in fallback if r[0]==phase]
   assert [r[1] for r in group]==list(range(5))
   total=next(r for r in rings if r[:2]==(phase,1))
   assert 0<group[1][2]<=total[2] and group[1][2]<=group[1][3]<=total[3]
   assert all(group[i][2:]==(0,0) for i in (0,2,3,4))
 return inputs,checks''')
replace("report['heldTerrainContracts']=focusEvidence(text)", r'''report['heldTerrainContracts']=focusEvidence(text)
for bad in (text.replace('RINGCONTRACT phase=0 arm=1','MISSINGRING phase=0 arm=1'),
 re.sub(r'(RINGCONTRACT phase=0 arm=1 calls=)\d+',r'\g<1>0',text),
 re.sub(r'(RINGCONTRACT phase=0 arm=0 calls=)\d+',r'\g<1>1',text),
 re.sub(r'(RINGCONTRACT phase=0 arm=1 calls=\d+ triangles=)\d+',r'\g<1>0',text),
 re.sub(r'(CONSUMERINPUT phase=0 arm=1 calls=\d+ verts=\d+ hash=)\d+',r'\g<1>0',text)):
 try:cold(bad)
 except AssertionError:pass
 else:raise AssertionError('A29 cold negative escaped')
report['negativeControls']+=['missing-native-contract','inactive-native-candidate','native-control',
 'zero-native-triangles','changed-canonical-input']
report['ringContracts']=re.findall(r'RINGCONTRACT[^\r\n]+',text)
report['nativeFallback']=plan.get('nativeFallback',False)
if report['nativeFallback']:
 for bad in (text.replace('RINGFALLBACK phase=0 arm=1','MISSINGFALLBACK phase=0 arm=1'),
  re.sub(r'(RINGFALLBACK phase=0 arm=1 calls=)\d+',r'\g<1>0',text),
  re.sub(r'(RINGFALLBACK phase=0 arm=0 calls=)\d+',r'\g<1>1',text),
  re.sub(r'(RINGFALLBACK phase=0 arm=1 calls=\d+ triangles=)\d+',r'\g<1>0',text)):
  try:cold(bad)
  except AssertionError:pass
  else:raise AssertionError('A29 fallback negative escaped')
 report['negativeControls']+=['missing-fallback','inactive-fallback','fallback-control','empty-fallback']
 report['fallbackContracts']=re.findall(r'RINGFALLBACK[^\r\n]+',text)
report['residentWords']=resident
report['nativeActive']=True''')
replace('terrain-focus-{platform}.json','clip-ring-{platform}.json')
replace('Stable pre-shake terrain focus; held-focus exact output only; moving LOD transitions may differ; elapsed net work, not pure EE cycles',
        'Native clip strips with bounded VU output rings; stable terrain focus and quiet transport in both arms; net elapsed, not pure EE cycles')
exec(compile(source, str(Path(__file__)), 'exec'))
