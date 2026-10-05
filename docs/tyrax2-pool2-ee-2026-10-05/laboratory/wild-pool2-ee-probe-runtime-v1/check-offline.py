from pathlib import Path
import importlib.util,struct,json
from protocol import verify
ROOT=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('decoder',ROOT/'decode-vu1-output.py');d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
checks=0
def check(b,msg):
 global checks
 checks+=1
 if not b:raise ValueError(msg)
def rejects(call):
 try:call()
 except ValueError:return True
 return False
def fixture(count,arm,repeat):
 b=bytearray(16384)
 for base,n in zip(d.BASES,([75,21]if count==96 else[75,42])):
  source=(0 if n==75 else 75)if count==96 else(75 if n==75 else 150)
  struct.pack_into('<4I',b,base*16,0,0,0,n|(d.FLAG if arm else 0))
  extent=2+2*n+3 if arm else 2+3*n;kick=base+extent
  if arm:struct.pack_into('<4I',b,(base+2+2*n)*16,21 if count==192 and n==75 else n,n,1,0)
  low=n|(1<<15)|(3<<60);high=2|(1<<4)|(4<<8);struct.pack_into('<QQ',b,kick*16,low,high)
  for i in range(n):
   member=(source+i)//96;e=repeat-1
   rgba=(31+e,63,95,128)if member==0 else(95-e,15,1,128)
   struct.pack_into('<4I',b,(kick+1+3*i+1)*16,*rgba)
 return b
def log(a,n,r):
 s=f'LOG: POOL2PROBE_CONFIG arm={a} vertices={n} members={n//96} repeats={r} package=75 alpha=128\nLOG: POOL2EEPROBE_CONFIG kind=7 clip=0 lazy={a} oracle=0\n'
 for i in range(1,r+1):s+=f'LOG: POOL2EEPROBE_SOURCE clip=0 ready=0 total={n} generation={i} coldOracle=0\nLOG: POOL2PROBE_FRAME frame={i} cold={int(i==1)} arm={a} eligible=0 applied=0 compared=0 mismatches=0\n'
 return s+f'LOG: POOL2PROBE_READY arm={a} vertices={n} repeats={r} completed={r} warmBypass=0\n'
for n in(96,192):
 for r in(1,2,3):
  for a in(0,1):
   b=fixture(n,a,r);banks=d.decode(b,'A96'if n==96 else'B192',a,r);check(len(banks)==2,'banks')
   for bank in banks.values():
    mutated=bytearray(b);idx=(bank['kickQword']+bank['stateQwords']+1)*16
    struct.pack_into('<I',mutated,idx,0)
    check(rejects(lambda:d.decode(mutated,'A96'if n==96 else'B192',a,r)),'wrong saved RGBA rejection')
   if r>1:check(rejects(lambda:d.decode(fixture(n,a,r-1),'A96'if n==96 else'B192',a,r)),'stale bank epoch rejection')
   text=log(a,n,r);check(verify(text,a,n,r)['countersDisabled'],'protocol positive')
   for old,new in [('compared=0','compared=1'),('coldOracle=0','coldOracle=1'),('generation=1','generation=0'),('ready=0','ready=1'),('kind=7','kind=6')]:
    check(rejects(lambda:verify(text.replace(old,new,1),a,n,r)),'protocol corrupt '+old)
   check(rejects(lambda:verify(text+text,a,n,r)),'duplicate protocol')
  check(d.compare(fixture(n,0,r),fixture(n,1,r),'A96'if n==96 else'B192',r)['status'].startswith('PASS'),'paired output positive')
report={'status':'PASS_OFFLINE_SYNTHETIC_HELPER_CONTROLS_ONLY','checks':checks,'limitations':'Synthetic bytes test helper acceptance/rejection, never target output, capture lifecycle or timing.'}
(ROOT/'offline-controls.json').write_text(json.dumps(report,indent=2));print(report)
