"""Synthetic helper controls only; no target output or devices."""
from pathlib import Path
import struct,json,importlib.util
from protocol import verify
ROOT=Path(__file__).resolve().parent
sp=importlib.util.spec_from_file_location('decoder',ROOT/'decode-vu1-output.py');d=importlib.util.module_from_spec(sp);sp.loader.exec_module(d)
checks=0
def check(x,s):
 global checks
 checks+=1
 if not x:raise ValueError(s)
def rejected(call):
 try:call()
 except ValueError:return True
 return False
def image(n,arm,r):
 b=bytearray(16384)
 for base,count in zip(d.BASES,([75,21]if n==96 else[75,42])):
  source=(0 if count==75 else 75)if n==96 else(75 if count==75 else 150)
  lattice=arm and source//96==(source+count-1)//96
  struct.pack_into('<4I',b,base*16,0,0,0,count|(d.LATTICE if lattice else d.TABLE))
  if lattice:
   ps,ss,indices=d.expected_unique(source,count);u=len(ps);t=count//3
   struct.pack_into('<4I',b,(base+2)*16,u,t,4+2*u+t,4+5*u+t)
   b[(base+3)*16:(base+4)*16]=d.color(source//96,r)
   for i in range(u):b[(base+4+i)*16:(base+5+i)*16]=ps[i];b[(base+4+u+i)*16:(base+5+u+i)*16]=ss[i]
   for i in range(t):struct.pack_into('<4I',b,(base+4+2*u+i)*16,*(indices[i*3:i*3+3]+[0]))
   kick=base+4+5*u+t
  else:
   for i in range(count):
    p,s=d.input_source(source+i);b[(base+2+i)*16:(base+3+i)*16]=p;b[(base+2+count+i)*16:(base+3+count+i)*16]=s
   struct.pack_into('<4I',b,(base+2+2*count)*16,min(count,96-source%96),count,1,0)
   b[(base+3+2*count)*16:(base+4+2*count)*16]=d.color(source//96,r)
   b[(base+4+2*count)*16:(base+5+2*count)*16]=d.color((source+count-1)//96,r)
   kick=base+2+2*count+3
  struct.pack_into('<QQ',b,kick*16,count|(1<<15)|(3<<60),2|(1<<4)|(4<<8))
  for i in range(count):b[(kick+1+3*i+1)*16:(kick+1+3*i+2)*16]=d.color((source+i)//96,r,True)
  for i in range(count):struct.pack_into('<I',b,(kick+1+3*i+2)*16+12,(100+(source+i)%80)<<4)
 return b
def log(n,a,r):
 s=f'LOG: POOL2PROBE_CONFIG arm={a} vertices={n} members={n//96} repeats={r} package=75 alpha=128\nLOG: POOL2EEPROBE_CONFIG kind=8 clip=0 lazy=1 table=1 lattice={a} fog=1 fogStart=19 fogEnd=23 oracle=0\n'
 for i in range(1,r+1):s+=f'LOG: POOL2EEPROBE_SOURCE clip=0 ready=0 total={n} generation={i} coldOracle=0\nLOG: POOL2PROBE_FRAME frame={i} cold={int(i==1)} arm={a} eligible=0 applied=0 compared=0 mismatches=0\n'
 return s+f'LOG: POOL2PROBE_READY arm={a} vertices={n} repeats={r} completed={r} warmBypass=0\n'
for n in(96,192):
 for r in(1,2,3):
  case='A96'if n==96 else'B192'
  for arm in(0,1):
   b=image(n,arm,r);banks=d.decode(b,case,arm,r);check(len(banks)==2,'two positive banks')
   check(verify(log(n,arm,r),arm,n,r)['countersDisabled'],'protocol positive')
   for bank in banks.values():
    base=bank['base'];count=bank['count'];kick=bank['kickQword']
    mutations=[(base*16+12,0),((kick+bank['stateQwords']+1)*16,0),((base+2)*16,0)]
    if bank['layout']=='lattice':
     u=bank['descriptor'][0];mutations.extend([((base+4+2*u)*16,25),((base+4+u)*16,0x7f800000),((base+2)*16+8,0)])
    else:mutations.extend([((base+3+2*count)*16,0),((base+2)*16,0x7f800000)])
    for offset,value in mutations:
     bad=bytearray(b);struct.pack_into('<I',bad,offset,value)
     check(rejected(lambda:d.decode(bad,case,arm,r)),'mutated input/route/output rejected')
   if r>1:check(rejected(lambda:d.decode(image(n,arm,r-1),case,arm,r)),'stale epoch rejected')
   for old,new in [('lattice='+str(arm),'lattice='+str(1-arm)),('table=1','table=0'),('ready=0','ready=1'),('compared=0','compared=1'),('generation=1','generation=0')]:check(rejected(lambda:verify(log(n,arm,r).replace(old,new,1),arm,n,r)),'protocol corrupt rejected')
  baseline=image(n,0,r);candidate=image(n,1,r)
  check(d.compare(baseline,candidate,case,r)['finalEpochRGBAValidated'],'paired positive')
  bank=next(iter(d.decode(candidate,case,1,r).values()));idx=(bank['kickQword']+bank['stateQwords'])*16
  candidate[idx]=1;check(rejected(lambda:d.compare(baseline,candidate,case,r)),'different actual STQ output rejected')
for n in(96,192):
 for arm in(0,1):
  b=image(n,arm,1);banks=d.decode(b,'A96'if n==96 else'B192',arm,1)
  for bank in banks.values():
   bad=bytearray(b)
   for i in range(bank['count']):struct.pack_into('<I',bad,(bank['kickQword']+bank['stateQwords']+3*i+2)*16+12,255<<4)
   check(rejected(lambda:d.decode(bad,'A96'if n==96 else'B192',arm,1)),'constant255 fog rejected')
record={'status':'PASS_OFFLINE_SYNTHETIC_FOG_LATTICE_HELPER_CONTROLS_ONLY','checks':checks,'limits':'Helper acceptance/rejection only; no actual packets/VU execution/native/device acceptance.'}
(ROOT/'offline-controls.json').write_text(json.dumps(record,indent=2)+'\n');print(record)
