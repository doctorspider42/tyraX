"""Synthetic schema controls only. No emulator, PS2 or measured timing."""
from pathlib import Path
import hashlib,json,subprocess,sys,struct
root=Path(__file__).parent;out=root/'quiet-parser-controls-v1';assert not out.exists();out.mkdir();parser=root/'analyze-quiet-motion.py';records=[]
hexfloat=lambda v:struct.pack('<f',v).hex() # Serialize target bit pattern below, not byte-order hex.
bits=lambda v:f'{struct.unpack("<I",struct.pack("<f",v))[0]:08x}'
def build(route):
 s=['LOG: CLOSEOUTCONFIG order=0 kind=3 valid=1 phases=3 phaseFrames=1800 taxFirst=800 taxLoops=320 sampleFirst=900 sampleCount=128 ordinaryClocks=1 addedWaits=0 frameProfile=0 hardwareTrace=0 devkit=0 sampleBytes=6144 chunkBytes=420'];a=[]
 for p in range(3):
  s.append(f'LOG: CLOSEOUTPHASE phase={p} first={p*1800} players=1 geometryHidden=0 sampler=1 countReads=262')
  for o in (750,1155):
   x=p*10+(o==1155);fields=dict(phase=p,offset=o,players=1,oneArm=1,route=1 if route=='vehicle'else 2,scene=0,vehicleDriver=0 if route=='vehicle'else-1,sceneGeneration=1,video=1,display=1,depth=1,width=640,height=448,refreshMilliHz=59940,requested=1,valid=1)
   fields.update({k:bits(x if k.endswith('X')else 0.0)for k in ('positionX','positionY','positionZ','lookX','lookY','lookZ','playerX','playerY','playerZ')})
   if route=='vehicle':fields.update(night=1,paused=1,hourZero=1,vehicleX=bits(x),vehicleY=bits(0),vehicleZ=bits(0),vehicleSpeed=bits(1))
   else:fields.update(portal0=int(p>0 or o==1155),portal1=int(p>0 or o==1155),portalOther=0)
   s.append('LOG: CLOSEOUTCONTEXT '+' '.join(f'{k}={v}'for k,v in fields.items()))
  for i in range(5):a.append(f'LOG: CLOSEOUTCHUNK phase={p} i={i} first={p*1800+800+i*64} loops=64 elapsed=128000000 pacing=32000000 totalFlips=64 renderedFlips=64 syntheticFlips=0')
  for i in range(128):a.append(f'LOG: CLOSEOUTRAW phase={p} i={i} frame={p*1800+900+i} entryPeriod=2000000 wholeLoop=1800000 pacing=500000 totalFlips=1 renderedFlips=1 syntheticFlips=0 presentPeriod=2000000 renderedPeriod=2000000 lastKind=1 sequence={p*1800+900+i} context=0')
  a.append(f'LOG: CLOSEOUTWINDOW phase={p} sampler=1 raw=128 chunks=5 valid=1')
 s.append('LOG: CLOSEOUTDONE order=0 kind=3 valid=1 loops=5400');return '\n'.join(s)+'\n','\n'.join(a)+'\n'
def run(name,route,edit=lambda s,a:(s,a),positive=False):
 case=out/name;case.mkdir();s,a=edit(*build(route));(case/'stdout.txt').write_text(s);(case/'artifact.txt').write_text(a)
 r=subprocess.run([sys.executable,str(parser),str(case/'stdout.txt'),'--artifact',str(case/'artifact.txt'),'--out',str(case/'result.json'),'--route',route],capture_output=True);(case/'parser.stdout').write_bytes(r.stdout);(case/'parser.stderr').write_bytes(r.stderr);assert (r.returncode==0)==positive,(name,r.stderr.decode())
 records.append(dict(name=name,route=route,expectedAccept=positive,exitCode=r.returncode))
for route in ('vehicle','gate'):
 run(route+'-positive',route,positive=True)
 for name,edit in [
 ('wrong-kind',lambda s,a:(s.replace('kind=3','kind=0'),a)),
 ('wrong-route',lambda s,a:(s.replace('route=1','route=2')if route=='vehicle'else s.replace('route=2','route=1'),a)),
 ('missing-done',lambda s,a:('\n'.join(l for l in s.splitlines()if 'CLOSEOUTDONE'not in l)+'\n',a)),
 ('nan-endpoint',lambda s,a:(s.replace('positionX=00000000','positionX=7fc00000',1),a)),
 ('duplicate-context',lambda s,a:(s+next(l for l in s.splitlines()if 'CLOSEOUTCONTEXT'in l)+'\n',a)),
 ('duplicate-raw',lambda s,a:(s,a+next(l for l in a.splitlines()if 'CLOSEOUTRAW'in l)+'\n')),
 ('wrong-first',lambda s,a:(s,a.replace('first=800','first=801',1))),
 ('pacing-over-loop',lambda s,a:(s,a.replace('pacing=500000','pacing=2000000',1))),
 ('invalid-window',lambda s,a:(s,a.replace('chunks=5 valid=1','chunks=5 valid=0',1))),
 ('synthetic-mismatch',lambda s,a:(s,a.replace('syntheticFlips=0','syntheticFlips=1',1)))]:run(route+'-'+name,route,edit)
run('vehicle-unpaused','vehicle',lambda s,a:(s.replace('paused=1','paused=0',1),a))
run('vehicle-no-driver','vehicle',lambda s,a:(s.replace('vehicleDriver=0','vehicleDriver=-1',1),a))
run('gate-no-roundtrip','gate',lambda s,a:(s.replace('portal1=1','portal1=0'),a))
run('gate-unexpected-id','gate',lambda s,a:(s.replace('portalOther=0','portalOther=1',1),a))
run('gate-decreasing-count','gate',lambda s,a:(s.replace('portal0=1','portal0=2',1),a))
proof={'status':'PASS_SYNTHETIC_QUIET_MOTION_PARSER_CONTROLS','cases':records,'caseCount':len(records),'parserSha256':hashlib.sha256(parser.read_bytes()).hexdigest(),'harnessSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'syntheticOnly':True,'noRuntimeOrTimingEvidence':True};(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status'],len(records))
