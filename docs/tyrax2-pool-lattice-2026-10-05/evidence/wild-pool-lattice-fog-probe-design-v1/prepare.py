"""Source/helper-only V11 fog diagnostic preparation; never launches/builds."""
from pathlib import Path
import hashlib,json,py_compile
ROOT=Path(__file__).resolve().parent;LAB=ROOT.parent
BASE=LAB/'wild-pool-lattice-probe-design-v1';OLD=LAB/'wild-pool-lattice-probe-runtime-v1'
RUNTIME=LAB/'wild-pool-lattice-fog-probe-runtime-v1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pins={}
def read(p):pins[str(p)]=sha(p);return p.read_text()
def replace(s,a,b):
 assert s.count(a)==1,a
 return s.replace(a,b)
s=read(BASE/'source/game/inc/pool2_static_probe.hpp')
s=replace(s,'engine->renderer.core.disableFog();','engine->renderer.core.setFog(Color(23.0F,47.0F,71.0F,128.0F),19.0F,23.0F);')
s=replace(s,'info.fogDisabled=true;','info.fogDisabled=false;')
s=replace(s,'lattice=%u oracle=0','lattice=%u fog=1 fogStart=19 fogEnd=23 oracle=0')
p=ROOT/'source/game/inc/pool2_static_probe.hpp';p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(s.encode())
p=ROOT/'source/game/src/terrain_game.cpp';p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((BASE/'source/game/src/terrain_game.cpp').read_bytes());pins[str(BASE/'source/game/src/terrain_game.cpp')]=sha(p)
for arm in(0,1):
 for n in(96,192):
  for r in(1,2,3):
   p=ROOT/f'configs/arm{arm}-vertices{n}-repeat{r}.cfg';p.parent.mkdir(exist_ok=True);p.write_bytes(f'{arm} {n} {r}\n'.encode())
v11=LAB/'wild-pool-lattice-source-v11/preparation-v11.json';pins[str(v11)]=sha(v11)
RUNTIME.mkdir(exist_ok=True)
for name in ['savestate-reader-pinned.py','launch-probe.py','run-case.py','capture-ready.py','verify-capture.py','finish-probe.py','protocol.py','decode-vu1-output.py','check-offline.py']:
 s=read(OLD/name)
 s=s.replace('wild-pool-lattice-probe-physical-v1','wild-pool-lattice-fog-probe-physical-v1')
 s=s.replace('pool-lattice-probe-','pool-lattice-fog-probe-')
 if name=='run-case.py':s=replace(s,'assert 184<=a.slot<=195','assert 196<=a.slot<=207')
 if name=='protocol.py':
  s=replace(s,' lattice=(\\d+) oracle=(\\d+)',' lattice=(\\d+) fog=(\\d+) fogStart=(\\d+) fogEnd=(\\d+) oracle=(\\d+)')
  s=replace(s,"[('8','0','1','1',str(arm),'0')]","[('8','0','1','1',str(arm),'1','19','23','0')]")
  s=s.replace('PASS_EXACT_LATTICE_EPOCH_PROTOCOL','PASS_EXACT_FOG_LATTICE_EPOCH_PROTOCOL')
 if name=='decode-vu1-output.py':
  s=replace(s,"  banks[n]=dict(","  fog=[(struct.unpack_from('<I',output,(3*i+2)*16+12)[0]>>4)&255 for i in range(n)]\n  need(len(set(fog))>=2 and all(0<f<255 for f in fog),'actual nonconstant interior fog coefficients required')\n  banks[n]=dict(")
  s=replace(s,'inputTopologyValidated=True)','inputTopologyValidated=True,fogValues=fog,nonconstantInteriorFogValidated=True)')
  s=replace(s,'outputQwords=3*n))','outputQwords=3*n,fogValues=x[n][\'fogValues\'],fogUnique=sorted(set(x[n][\'fogValues\']))))')
  s=replace(s,'finalEpochRGBAValidated=True,inputTopologyValidated=True,first75Overwritten','finalEpochRGBAValidated=True,inputTopologyValidated=True,nonconstantInteriorFogValidated=True,first75Overwritten')
  s=s.replace('PASS_ACTUAL_FINAL_LATTICE_POOL2_PACKED_OUTPUT_BYTE_EQUAL','PASS_ACTUAL_FINAL_FOG_LATTICE_POOL2_PACKED_OUTPUT_BYTE_EQUAL')
 if name=='verify-capture.py':
  s=replace(s,'finalEpochRGBAValidated=True,finalEpoch=repeats,','finalEpochRGBAValidated=True,nonconstantInteriorFogValidated=True,fogWitnesses={str(k):v[\'fogValues\'] for k,v in bank.items()},finalEpoch=repeats,')
 if name=='finish-probe.py':
  s=replace(s,"assert v['finalEpochRGBAValidated'] and v['inputTopologyValidated']", "assert v['nonconstantInteriorFogValidated'] and v['finalEpochRGBAValidated'] and v['inputTopologyValidated']")
 if name=='check-offline.py':
  s=s.replace('lattice={a} oracle=0','lattice={a} fog=1 fogStart=19 fogEnd=23 oracle=0')
  s=replace(s,' return b','  for i in range(count):struct.pack_into(\'<I\',b,(kick+1+3*i+2)*16+12,(100+(source+i)%80)<<4)\n return b')
  s=replace(s,"record={'status'", "for n in(96,192):\n for arm in(0,1):\n  b=image(n,arm,1);banks=d.decode(b,'A96'if n==96 else'B192',arm,1)\n  for bank in banks.values():\n   bad=bytearray(b)\n   for i in range(bank['count']):struct.pack_into('<I',bad,(bank['kickQword']+bank['stateQwords']+3*i+2)*16+12,255<<4)\n   check(rejected(lambda:d.decode(bad,'A96'if n==96 else'B192',arm,1)),'constant255 fog rejected')\nrecord={'status'")
  s=s.replace('PASS_OFFLINE_SYNTHETIC_LATTICE_HELPER_CONTROLS_ONLY','PASS_OFFLINE_SYNTHETIC_FOG_LATTICE_HELPER_CONTROLS_ONLY')
 (RUNTIME/name).write_bytes(s.encode())
for p in RUNTIME.glob('*.py'):py_compile.compile(str(p),doraise=True)
for p,h in pins.items():assert sha(Path(p))==h
report=dict(status='SOURCE_ONLY_V11_FOG_DIAGNOSTIC_AND_HELPERS_NOT_BUILT',inputs=pins,expectedPricingFiles=501,expectedDiagnosticFiles=502,sourceFixture='wild-pool-lattice-fog-probe-physical-v1',slots='196..207',fog=dict(start=19,end=23,bagDisabled=False,finiteGridViewDepth='20..21.375'),outputs={str(p.relative_to(LAB)):sha(p)for folder in(ROOT,RUNTIME)for p in folder.rglob('*')if p.is_file()and '__pycache__'not in p.parts and p.name!='preparation-proof.json'},limits=['Root must copy two source files into a fresh V11 pricing-derived fixture and independently freeze/build/audit 502 sources.','Actual fog witness and paired output acceptance pending target captures.','No CLIP/raster/performance acceptance.'])
(ROOT/'preparation-proof.json').write_bytes((json.dumps(report,indent=2)+'\n').encode());print(report['status'])
