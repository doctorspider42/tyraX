from pathlib import Path
import importlib.util,struct,json,hashlib
P=Path(__file__).parent;O=P/'host';O.mkdir(exist_ok=False)
s=importlib.util.spec_from_file_location('verify',P/'verify-capture.py');v=importlib.util.module_from_spec(s);s.loader.exec_module(v)
def elf(rows):
 names=b'\0pool2ProbeReady\0pool2ProbeHalt\0';syms=b''.join(struct.pack('<IIIBBH',names.index(n.encode()),addr,size,info,0,index)for n,addr,size,info,index in rows)
 off=52+120;h=struct.pack('<16sHHIIIIIHHHHHH',b'\x7fELF\x01\x01'+bytes(10),2,8,1,0,0,52,0,52,0,0,40,3,0)
 sections=bytes(40)+struct.pack('<10I',0,2,0,0,off,len(syms),2,0,4,16)+struct.pack('<10I',0,3,0,0,off+len(syms),len(names),0,0,1,0)
 return h+sections+syms+names
ready=('pool2ProbeReady',0x200000,4,17,1);halt=('pool2ProbeHalt',0x110000,16,18,1)
positives=[];negatives=[]
for i,rows in enumerate(([ready,halt],[halt,ready])):
 f=O/f'valid{i}.sym';f.write_bytes(elf(rows));r=v.symbols(f);assert r['pool2ProbeReady']['address']==0x200000 and r['pool2ProbeHalt']['address']==0x110000;positives.append(i)
bad={'missingHalt':[ready],'duplicateReady':[ready,ready,halt],'undefinedReady':[(*ready[:4],0),halt],'wrongReadySize':[(ready[0],ready[1],8,ready[3],1),halt],'wrongReadyType':[(ready[0],ready[1],4,18,1),halt],'wrongHaltType':[ready,(halt[0],halt[1],16,17,1)]}
for n,rows in bad.items():
 f=O/(n+'.sym');f.write_bytes(elf(rows))
 try:v.symbols(f)
 except ValueError:negatives.append(n)
 else:raise AssertionError(n)
for arm in(0,1):
 for n in(96,192):
  for repeat in(1,2,3):assert (P/'configs'/f'arm{arm}-vertices{n}-repeat{repeat}.cfg').read_text()==f'{arm} {n} {repeat}\n'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
r=dict(status='PASS_OFFLINE_WORKFLOW_SYMBOL_CONFIG_CONTROLS_ONLY',symbolPositiveCases=positives,symbolNegativeGuards=negatives,configFixtures=12,
 actualBootElfOrSaveStateOrNativeAccepted=False,files={str(p):sha(p)for p in [Path(__file__),P/'verify-capture.py']+list(O.iterdir())})
(O/'proof.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf8');print(sha(O/'proof.json'))
