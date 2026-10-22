"""Synthetic schema controls only. No emulator, PS2 or measured timing."""
from pathlib import Path
import hashlib,json,subprocess,sys,struct
root=Path(__file__).parent;out=root/'parser-controls-v1';assert not out.exists();out.mkdir();parser=root/'analyze-80k.py';records=[]
hexfloat=lambda v:struct.pack('<f',v).hex() # Serialize target bit pattern below, not byte-order hex.
bits=lambda v:f'{struct.unpack("<I",struct.pack("<f",v))[0]:08x}'
def build(route):
 s=['LOG: CLOSEOUTCONFIG order=0 kind=4 valid=1 phases=3 phaseFrames=1800 taxFirst=800 taxLoops=320 sampleFirst=900 sampleCount=128 ordinaryClocks=1 addedWaits=0 frameProfile=0 hardwareTrace=0 devkit=0 sampleBytes=6144 chunkBytes=420'];a=[]
 for p in range(3):
  s.append(f'LOG: CLOSEOUTPHASE phase={p} first={p*1800} players=0 geometryHidden=0 sampler=1 countReads=262')
  for o in (750,1155):
   x=p*10+(o==1155);fields=dict(phase=p,offset=o,players=0,oneArm=1,route=9 if route=='vehicle'else 2,scene=0,vehicleDriver=0 if route=='vehicle'else-1,sceneGeneration=1,video=1,display=1,depth=1,width=640,height=448,refreshMilliHz=59940,requested=1,valid=1)
   fields.update({k:bits(x if k.endswith('X')else 0.0)for k in ('positionX','positionY','positionZ','lookX','lookY','lookZ','playerX','playerY','playerZ')})
   if route=='vehicle':fields.update(night=1,paused=1,hourZero=1,vehicleX=bits(x),vehicleY=bits(0),vehicleZ=bits(1 if p>0 else 0),vehicleSpeed=bits(1))
   else:fields.update(portal0=int(p>0 or o==1155),portal1=int(p>0 or o==1155),portalOther=0)
   s.append('LOG: CLOSEOUTCONTEXT '+' '.join(f'{k}={v}'for k,v in fields.items()))
   s.append(f'LOG: EMPTYRENDER phase={p} offset={o} acceptedBags=58 acceptedVertices=80133 billboards=0 roadBags=3 roadVertices=5961 carBags=4 carVertices=3072 valid=1')
   s.append(f'LOG: BUILDINGRENDER phase={p} offset={o} acceptedBags=50 acceptedVertices=70800 refs0=1 refs1=1 refs2=1 refs3=1 refs4=1 refs5=1 refs6=1 refs7=1 refs8=1 refs9=1 refs10=1 refs11=1 refs12=1 refs13=1 refs14=1 refs15=1 refs16=1 refs17=1 refs18=1 refs19=1 refs20=1 refs21=1 refs22=1 refs23=1 refs24=1 refs25=1 refs26=1 refs27=1 refs28=1 refs29=1 refs30=1 refs31=1 refs32=1 refs33=1 refs34=1 refs35=1 refs36=1 refs37=1 refs38=1 refs39=1 refs40=1 refs41=1 refs42=1 refs43=1 refs44=1 refs45=1 refs46=1 refs47=1 refs48=1 refs49=1 valid=1')
   s.append(f'LOG: MODELRENDER phase={p} offset={o} owner=4 acceptedBags=1 acceptedVertices=300 valid=1')
   s.append(f'LOG: LIGHTCONTEXT phase={p} offset={o} count=1 slot=0 enabled=1 point=1 x={bits(-1.15+(o==1155)*.25)} y={bits(3)} z={bits(4)} range=20 r=32 g=96 b=128 valid=1')
   s.append(f'LOG: TWOCARLIGHT phase={p} offset={o} ravagerBodySelected=1 picaBodySelected=1 ravagerWheelSelected=1 picaWheelSelected=1 ravagerBodyEffective=1 picaBodyEffective=1 ravagerWheelEffective=1 picaWheelEffective=1 valid=1')
   s.append(f'LOG: TWOCARCONTEXT phase={p} offset={o} vehicleCount=2 driverDef=0 secondDef=1 driverObject=1 secondObject=3 secondX={bits(-2.3)} secondY={bits(.38)} secondZ={bits(8)} secondSpeed={bits(0)}')
   s.append(f'LOG: TWOCARRENDER phase={p} offset={o} ravagerBodyBags=1 ravagerBodyVertices=768 picaBodyBags=1 picaBodyVertices=768 ravagerWheelBags=1 ravagerWheelVertices=768 picaWheelBags=1 picaWheelVertices=768 valid=1')
   s.append(f'LOG: ROADCONTEXT phase={p} offset={o} paved=4 grounded=1 roadY={bits(.12)} centerOffset={bits(0)} grip={bits(1)} cover={bits(1)}')
  for i in range(5):a.append(f'LOG: CLOSEOUTCHUNK phase={p} i={i} first={p*1800+800+i*64} loops=64 elapsed=128000000 pacing=32000000 totalFlips=64 renderedFlips=64 syntheticFlips=0')
  for i in range(128):a.append(f'LOG: CLOSEOUTRAW phase={p} i={i} frame={p*1800+900+i} entryPeriod=2000000 wholeLoop=1800000 pacing=500000 totalFlips=1 renderedFlips=1 syntheticFlips=0 presentPeriod=2000000 renderedPeriod=2000000 lastKind=1 sequence={p*1800+900+i} context=0')
  a.append(f'LOG: CLOSEOUTWINDOW phase={p} sampler=1 raw=128 chunks=5 valid=1')
 s.append('LOG: CLOSEOUTDONE order=0 kind=4 valid=1 loops=5400');return '\n'.join(s)+'\n','\n'.join(a)+'\n'
def run(name,route,edit=lambda s,a:(s,a),positive=False):
 case=out/name;case.mkdir();s,a=edit(*build(route));(case/'stdout.txt').write_text(s);(case/'artifact.txt').write_text(a)
 r=subprocess.run([sys.executable,str(parser),str(case/'stdout.txt'),'--artifact',str(case/'artifact.txt'),'--out',str(case/'result.json'),'--route',route],capture_output=True);(case/'parser.stdout').write_bytes(r.stdout);(case/'parser.stderr').write_bytes(r.stderr);assert (r.returncode==0)==positive,(name,r.stderr.decode())
 records.append(dict(name=name,route=route,expectedAccept=positive,exitCode=r.returncode))
for route in ('vehicle',):
 run(route+'-positive',route,positive=True)
 for name,edit in [
 ('wrong-kind',lambda s,a:(s.replace('kind=4','kind=3'),a)),
 ('wrong-route',lambda s,a:(s.replace('route=9','route=1'),a)),
 ('missing-done',lambda s,a:('\n'.join(l for l in s.splitlines()if 'CLOSEOUTDONE'not in l)+'\n',a)),
 ('nan-endpoint',lambda s,a:(s.replace('positionX=00000000','positionX=7fc00000',1),a)),
 ('duplicate-context',lambda s,a:(s+next(l for l in s.splitlines()if 'CLOSEOUTCONTEXT'in l)+'\n',a)),
 ('duplicate-raw',lambda s,a:(s,a+next(l for l in a.splitlines()if 'CLOSEOUTRAW'in l)+'\n')),
 ('wrong-first',lambda s,a:(s,a.replace('first=800','first=801',1))),
 ('pacing-over-loop',lambda s,a:(s,a.replace('pacing=500000','pacing=2000000',1))),
 ('invalid-window',lambda s,a:(s,a.replace('chunks=5 valid=1','chunks=5 valid=0',1))),
 ('synthetic-mismatch',lambda s,a:(s,a.replace('syntheticFlips=0','syntheticFlips=1',1)))]:run(route+'-'+name,route,edit)
run('no-driver','vehicle',lambda s,a:(s.replace('vehicleDriver=0','vehicleDriver=-1',1),a))
run('missing-render','vehicle',lambda s,a:('\n'.join(l for l in s.splitlines()if 'EMPTYRENDER'not in l)+'\n',a))
run('empty-bag','vehicle',lambda s,a:(s.replace('acceptedBags=58','acceptedBags=0',1),a))
run('empty-vertices','vehicle',lambda s,a:(s.replace('acceptedVertices=80133','acceptedVertices=0',1),a))
run('billboard','vehicle',lambda s,a:(s.replace('billboards=0','billboards=1',1),a))
for name,edit in [('road-zero',lambda s,a:(s.replace('roadBags=3','roadBags=0',1),a)),('car-zero',lambda s,a:(s.replace('carVertices=3072','carVertices=0',1),a)),('road-sum',lambda s,a:(s.replace('roadVertices=5961','roadVertices=1025',1),a)),('unpaved',lambda s,a:(s.replace('paved=4','paved=3',1),a)),('airborne',lambda s,a:(s.replace('grounded=1','grounded=0',1),a)),('too-far-short-drive',lambda s,a:(s.replace('vehicleZ=3f800000','vehicleZ=41200000'),a)),('road-nan',lambda s,a:(s.replace('roadY=3df5c28f','roadY=7fc00000',1),a)),('road-missing',lambda s,a:('\n'.join(l for l in s.splitlines()if 'ROADCONTEXT'not in l)+'\n',a))]:run(name,'vehicle',edit)
for name,edit in [('missing-two-car',lambda s,a:('\n'.join(l for l in s.splitlines()if 'TWOCARCONTEXT'not in l)+'\n',a)),('wrong-second-def',lambda s,a:(s.replace('secondDef=1','secondDef=0',1),a)),('moving-second',lambda s,a:(s.replace('secondSpeed=00000000','secondSpeed=3f800000',1),a)),('missing-pica-body',lambda s,a:(s.replace('picaBodyBags=1','picaBodyBags=0',1),a)),('missing-pica-wheels',lambda s,a:(s.replace('picaWheelVertices=768','picaWheelVertices=0',1),a)),('carrier-sum',lambda s,a:(s.replace('picaWheelVertices=768','picaWheelVertices=769',1),a))]:run(name,'vehicle',edit)
for name,edit in [('light-disabled',lambda s,a:(s.replace('enabled=1 point=1','enabled=0 point=1',1),a)),('wrong-light-type',lambda s,a:(s.replace('point=1 x=','point=0 x=',1),a)),('two-lights',lambda s,a:(s.replace('count=1 slot=0','count=2 slot=0',1),a)),('unselected-pica-body',lambda s,a:(s.replace('picaBodySelected=1','picaBodySelected=0',1),a)),('filtered-pica-wheels',lambda s,a:(s.replace('picaWheelEffective=1','picaWheelEffective=0',1),a)),('static-light',lambda s,a:(s.replace('x=bf666666','x=bf933333'),a)),('missing-light-records',lambda s,a:('\n'.join(l for l in s.splitlines()if 'LIGHTCONTEXT'not in l)+'\n',a))]:run(name,'vehicle',edit)
for name,edit in [('missing-model',lambda s,a:('\n'.join(l for l in s.splitlines()if 'MODELRENDER'not in l)+'\n',a)),('wrong-model-owner',lambda s,a:(s.replace('owner=4 acceptedBags','owner=3 acceptedBags',1),a)),('zero-model-bags',lambda s,a:(s.replace('owner=4 acceptedBags=1','owner=4 acceptedBags=0',1),a)),('zero-model-vertices',lambda s,a:(s.replace('acceptedVertices=300','acceptedVertices=0',1),a)),('model-sum',lambda s,a:(s.replace('acceptedVertices=300','acceptedVertices=301',1),a))]:run(name,'vehicle',edit)
for name,edit in [('missing-buildings',lambda s,a:('\n'.join(l for l in s.splitlines()if 'BUILDINGRENDER'not in l)+'\n',a)),('zero-building-bags',lambda s,a:(s.replace('acceptedBags=50 acceptedVertices=70800','acceptedBags=0 acceptedVertices=70800',1),a)),('zero-building-vertices',lambda s,a:(s.replace('acceptedVertices=70800','acceptedVertices=0',1),a)),('missing-owner7',lambda s,a:(s.replace('refs7=1','refs7=0',1),a)),('overstated-owner',lambda s,a:(s.replace('refs0=1','refs0=51',1),a)),('building-sum',lambda s,a:(s.replace('acceptedVertices=70800','acceptedVertices=70801',1),a))]:run(name,'vehicle',edit)
for name,edit in [('owner32-highbit-loss',lambda s,a:(s.replace('refs32=1','refs32=0',1),a)),('owner49-highbit-loss',lambda s,a:(s.replace('refs49=1','refs49=0',1),a)),('overflow-invalid',lambda s,a:(s.replace('refs49=1 valid=1','refs49=1 valid=0',1),a)),('wrong80k-total',lambda s,a:(s.replace('acceptedVertices=80133','acceptedVertices=80132',1),a))]:run(name,'vehicle',edit)
proof={'status':'PASS_SYNTHETIC_FIFTY_BUILDINGS_80K_PARSER_CONTROLS','cases':records,'caseCount':len(records),'parserSha256':hashlib.sha256(parser.read_bytes()).hexdigest(),'harnessSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'syntheticOnly':True,'noRuntimeOrTimingEvidence':True};(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status'],len(records))
