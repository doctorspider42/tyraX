"""Synthetic decoder/protocol controls only; never target evidence."""
from pathlib import Path
import struct,json,importlib.util
from protocol import verify
P=Path(__file__).parent;s=importlib.util.spec_from_file_location('d',P/'decode-vu1-output.py');d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
checks=0
def check(v,s):
 global checks
 checks+=1
 if not v:raise ValueError(s)
def rejected(f):
 try:f()
 except ValueError:return True
 return False
def image(case,arm,epoch):
 b=bytearray(16384);struct.pack_into('<4I',b,8*16,0,0xffffffff,0,0);b[8*16+8:9*16]=bytes.fromhex('01007fc20048b744');b[12*16:15*16]=bytes.fromhex('0000000000000000000080bf0ad7233c00000000000000000000803ffd04f73c0000804000000041000040411bf0a23e')
 for base,n in zip(d.BASES,([75,21]if case==4 else[72])):
  first=75 if n==21 else 0;marker=arm and case!=4;state=1
  struct.pack_into('<4I',b,base*16,0,0,struct.unpack('<I',struct.pack('<f',32767.5))[0],n|(d.SPRITE if marker else 0))
  for i in range(n):
   p,s,c=d.source(case,first+i,epoch)
   for dest,x in [(base+2+i,p),(base+2+n+i,s),(base+2+2*n+i,c)]:b[dest*16:(dest+1)*16]=x
  kick=base+2+3*n+int(marker);t=n|(1<<15)|(3<<60)|(3<<47)|(1<<(47+5))|(0x412<<64)
  corners=(0,1,2,0,2,3);xy=((100,100),(200,100),(200,200),(100,200));verts=[]
  for i in range(n):
   index=first+i;k=corners[index%6];f=100+(20 if case==2 and k in(1,2)else 0)
   rgba=(34+epoch-1+(8 if case==3 and index%6==1 else 0),69,104,128)
   verts.append([struct.pack('<4f',0,0,.05,0),struct.pack('<4I',*rgba),struct.pack('<4I',*xy[k],10000,f<<4)])
  if arm and case==1:
   t=(t&~(0x7fff|(7<<47)))|(n//3)|(6<<47);verts=[v for i in range(0,n,6)for v in(verts[i],verts[i+2])]
  if marker:b[(kick-1)*16:kick*16]=((t&~(0x7fff|(7<<47)))|(n//3)|(6<<47)).to_bytes(16,'little')
  b[kick*16:(kick+1)*16]=t.to_bytes(16,'little')
  for i,q in enumerate(q for v in verts for q in v):b[(kick+1+i)*16:(kick+2+i)*16]=q
 return b
def log(case,arm,r):
 n=96 if case==4 else 72;p=75 if case==4 else 72
 s=f'LOG: CORONAPROBE_CONFIG arm={arm} case={case} vertices={n} repeats={r} package={p} fog=1 light=1 oracle=0 depth=16 capability=1\n'
 for i in range(1,r+1):s+=f'LOG: CORONAPROBE_FRAME frame={i} arm={arm} case={case} generation={i} counters=0\n'
 return s+f'LOG: CORONAPROBE_READY arm={arm} case={case} repeats={r} completed={r}\n'
for case in(1,2,3,4):
 for r in(1,2,3):
  for arm in(0,1):
   b=image(case,arm,r);banks=d.decode(b,case,arm,r);check(bool(banks),'positive actual-layout synthetic decode')
   check(verify(log(case,arm,r),arm,case,r)['countersDisabled'],'protocol positive')
   for bank in banks.values():
    base=bank['base'];n=bank['count'];kick=bank['kickQword']
    for addr in [base*16+12,(base+2)*16,(base+2+2*n)*16,kick*16]:
     bad=bytearray(b);struct.pack_into('<I',bad,addr,0xffffffff);check(rejected(lambda:d.decode(bad,case,arm,r)),'source/route/tag corrupt rejected')
   if r>1:check(rejected(lambda:d.decode(image(case,arm,r-1),case,arm,r)),'stale source epoch rejected')
   for old,new in [('fog=1','fog=0'),('light=1','light=0'),('generation=1','generation=0'),('counters=0','counters=1')]:check(rejected(lambda:verify(log(case,arm,r).replace(old,new,1),arm,case,r)),'bad protocol rejected')
  off,on=image(case,0,r),image(case,1,r);check(d.compare(off,on,case,r)['inputTopologyValidated'],'paired positive')
  bank=next(iter(d.decode(on,case,1,r).values()));bad=bytearray(on);bad[(bank['kickQword']+bank['stateQwords'])*16]^=1;check(rejected(lambda:d.compare(off,bad,case,r)),'payload corruption rejected')
for addr,value in [(8*16+4,1),(12*16,0xffffffff),(8*16+8,0),(22*16+8,0x4affffff)]:
 b=image(1,0,1);struct.pack_into('<I',b,addr,value);check(rejected(lambda:d.decode(b,1,0,1)),'inactive or wrong uniform/depth rejected')
for case in(2,3):
 b=image(case,0,1);bank=next(iter(d.decode(b,case,0,1).values()));k=bank['kickQword']+bank['stateQwords']
 for i in range(bank['count']):
  if case==2:struct.pack_into('<I',b,(k+3*i+2)*16+12,100<<4)
  else:b[(k+3*i+1)*16:(k+3*i+2)*16]=struct.pack('<4I',34,69,104,128)
 check(rejected(lambda:d.decode(b,case,0,1)),'missing genuine negative witness rejected')
report=dict(status='PASS_SYNTHETIC_CORONA_SAVED_VU_HELPER_CONTROLS_ONLY',checks=checks,actualTargetCaptured=False);(P/'offline-controls.json').write_bytes((json.dumps(report,indent=2)+'\n').encode());print(report)
