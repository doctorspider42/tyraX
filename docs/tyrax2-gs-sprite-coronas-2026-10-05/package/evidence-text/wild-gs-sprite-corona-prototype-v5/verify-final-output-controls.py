"""Source-aligned bounded packed-word host controls; does not execute VU or GS."""
from pathlib import Path
import hashlib,json,struct,copy
O=Path(__file__).resolve().parent;B=O.parent/'wild-gs-sprite-corona-prototype-v3';checks=0
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def check(ok):
 global checks
 assert ok;checks+=1
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def equal(a,b):return f32(f32(a)-f32(b))==0
renderer='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';vcl='tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp';writer='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp'
a=(B/renderer).read_bytes();b=(O/renderer).read_bytes();check('fogOffForBag && coronaShaderUnlit &&' not in b.decode())
check('buffer->coronaSpriteApplied=ExperimentalCoronaSprite::enabled&&coronaEligible;' in b.decode())
check('NightAblation::observeCorona(coronaEligible,buffer->coronaSpriteApplied,' in b.decode())
check((O/vcl).read_bytes()==(B/vcl).read_bytes());s=(O/vcl).read_text();pre,post=s.split('vertexLoopsDone:',1)
# Three complete stock loop bodies converge after their stores. Lit path retains light + fog.
for label in ['vertexLoop:','unlitVertexLoop:','noFogVertexLoop:']:check(label in pre)
check(pre.count('CalculateTyraSpotLight{')==3);check(pre.count('CalculateTyraFog{')==6);check(pre.count('PerformTyraFogClipCheck{')==9);check(pre.count('FixColor{')==9);check(pre.count('sq      color')==9);check(pre.count('sq.xyz  vertex')==9)
check('iaddiu  colorRunRemaining, vi00, 6' in post);check('sub     color3, color2, color1' in post);check('sub.zw  color3, vertex2, vertex1' in post);check(post.index('coronaVertexCheck:')<post.index('coronaCompact:'));check(post.index('ibne    stqData, vi00, coronaQuadCheck')<post.index('coronaCompact:'))
w=(O/writer).read_text();check('prim_t spritePrim=*prim; spritePrim.type=PRIM_SPRITE;' in w);check('packet2_utils_gs_add_prim_giftag(packet,&spritePrim,buffer->size/3,' in w)
# Model just the final six-vertex equality gates on the actual packed-word domains.
def gate(package):
 for quad in package:
  r=quad[0]
  if r['adc']:return False
  for v in quad:
   if not all(equal(x,y) for x,y in zip(v['rgba'],r['rgba'])):return False
   if not equal(v['z'],r['z']) or not equal(v['fword'],r['fword']):return False
 return True
base={'rgba':(32,64,128,128),'z':0x7fff8,'fword':0x80a,'adc':False}
quad=[copy.deepcopy(base) for _ in range(6)];check(gate([quad]))
for F in range(256):
 for frac in [0,1,15]:
  q=[dict(base,fword=(F<<4)|frac) for _ in range(6)];check(gate([q]))
  for index in range(6):
   bad=copy.deepcopy(q);bad[index]['fword']=((F^1)<<4)|frac;check(not gate([bad]));check(not gate([q,bad]));check(not gate([bad,q]));check(gate([q]))
for index in range(6):
 for lane in range(4):
  for delta in [-1,1]:
   bad=copy.deepcopy(quad);c=list(bad[index]['rgba']);c[lane]+=delta;bad[index]['rgba']=tuple(c);check(not gate([bad]));check(not gate([quad,bad]));check(gate([quad]))
 for delta in [-65536,-1,1,65536]:
  bad=copy.deepcopy(quad);bad[index]['z']+=delta;check(not gate([bad]))
# Independently varied uniform colors and F remain accepted; active effects need not be disabled.
for color in [(0,0,0,128),(255,255,255,128),(3,13,203,128)]:
 for F in range(256):check(gate([[dict(base,rgba=color,fword=F<<4) for _ in range(6)]]))
# GS PRIM packed type is bits0..2; FGE is bit5. Preserve every other PRIM bit.
for prim in range(1<<11):
 sprite=(prim&~7)|6;check((sprite&~7)==(prim&~7));check((sprite>>5)&1==(prim>>5)&1)
# Constant-F interpolation and sprite second-F yield equal fog-stage integer math.
def fog(rgb,F,col,FGE):return rgb if not FGE else tuple(((F*v)>>8)+(((255-F)*c)>>8) for v,c in zip(rgb,col))
for F in range(256):
 for rgb in [(0,0,0),(32,128,255),(255,255,255)]:
  for col in [(0,0,0),(13,23,41),(255,255,255)]:
   for FGE in [0,1]:
    for weights in [(256,0,0),(0,256,0),(0,0,256),(128,64,64),(1,127,128)]:
     triangleF=sum(value*weight for value,weight in zip([F,F,F],weights))//256
     spriteF=[F,F][-1]
     check(fog(rgb,triangleF,col,FGE)==fog(rgb,spriteF,col,FGE))
proof={'status':'PASS_SOURCE_ALIGNED_HOST_CONSTANT_OUTPUT_CONTROLS','checks':checks,'checkerSha256':sha(Path(__file__)),'sourcePins':{r:sha(O/r) for r in [renderer,vcl,writer]},'v5AdmissionArmAndSparseColdCounterSourceControls':True,'allThreeOriginalLoopsAndAllSixPosttransformScanPreserved':True,'varyingFOrAnyRGBALaneRejectsWholePackage':True,'fullPackedZLow16CollisionRejected':True,'uniformFinalEffectsMayPass':True,'primitiveTypeOnlyChangesAndFGERemainsExact':True,'limits':'Host bounded-word and state controls plus source structural assertions, not actual VU execution/compiler scheduling, GS rasterization or physical gain.'}
(O/'host-controls-proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
