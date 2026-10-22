from pathlib import Path
import ctypes,struct,json,collections,hashlib
root=Path('/mnt/f/Projects/tyrax2-lab-20261001/tyrax2-queue-audit-v2/vehicle/game/bin')
outroot=Path('/mnt/f/Projects/tyrax2-lab-20261001/strip-trial-v1/repacked')
lib=ctypes.CDLL(str(Path(__file__).parent/'candidate.so'))
lib.stripify.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint,ctypes.c_uint,ctypes.c_void_p,ctypes.c_void_p]
rows=[]
def oracle(geo,ao,run=0):
 vs=[geo[i:i+32]+(ao[i//32:i//32+1] if ao else b'') for i in range(0,len(geo),32)]
 result=collections.Counter()
 for first in range(0,len(vs),run or len(vs)):
  arr=vs[first:first+(run or len(vs))]
  for i in range(0,len(arr)-2,1 if run else 3):
   tri=arr[i:i+3];pos=[struct.unpack('<3f',v[:12]) for v in tri]
   a=[pos[1][j]-pos[0][j] for j in range(3)];b=[pos[2][j]-pos[0][j] for j in range(3)]
   if all(a[(j+1)%3]*b[(j+2)%3]-a[(j+2)%3]*b[(j+1)%3]==0 for j in range(3)):continue
   result[tuple(sorted(tri))]+=1
 return result
for p in sorted((root/'models').rglob('*.tmdl')):
 d=p.read_bytes();at=0;chunks=[];replaced=0
 def take(n):
  global at
  assert at+n<=len(d);v=d[at:at+n];at+=n;return v
 def u():return struct.unpack('<I',take(4))[0]
 def mesh():
  n=u();geo=take(n*32);ac=u();ao=take(ac);assert ac in (0,n)
  return geo,ao
 def enc(m):return struct.pack('<I',len(m[0])//32)+m[0]+struct.pack('<I',len(m[1]))+m[1]
 assert take(4)==b'TMDL';version=u();assert version==4;take(24);pc=u();chunks.append(d[:at])
 for pi in range(pc):
  start=at;name=take(32).split(b'\0')[0].decode();take(128+12+12+8)
  base=mesh();lc=u();lods=[mesh() for _ in range(lc)];prefix=d[start:at]
  run=u();streams=[mesh() for _ in range(1+lc)]
  n=len(base[0])//32;vb=ctypes.create_string_buffer(base[0]);ab=ctypes.create_string_buffer(base[1]);obuf=ctypes.create_string_buffer(n*32);aobuf=ctypes.create_string_buffer(n)
  count=lib.stripify(vb,ab,n,len(base[1]),obuf,aobuf)
  old=len(streams[0][0])//32 or n
  chosen=count if count and count<old else old
  if chosen<old:
   new=(obuf.raw[:count*32],aobuf.raw[:count] if base[1] else b'')
   assert oracle(*base)==oracle(*new,75),(str(p),name,'triangle/attribute mismatch')
   if streams[0][0]:assert oracle(*base)==oracle(*streams[0],run)
   streams[0]=new;run=75;replaced+=1
  chunks.append(prefix+struct.pack('<I',run)+b''.join(enc(m)for m in streams))
  rows.append(dict(file=p.relative_to(root).as_posix(),part=name,list=n,old=old,candidate=count,chosen=chosen,changed=chosen<old))
 chunks.append(d[at:]);newdata=b''.join(chunks)
 target=outroot/p.relative_to(root);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(newdata)
report=dict(status='HOST_EXACT_UNORIENTED_TRIANGLE_ATTRIBUTE_ORACLE_PASS',parts=len(rows),changed=sum(r['changed']for r in rows),old=sum(r['old']for r in rows),chosen=sum(r['chosen']for r in rows),rows=rows,limitations='No winding culling; ignores zero-area triangles. Asset inventory, not visible frame counts or hardware timing.')
(Path(__file__).parent/'repack-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='rows'},indent=2))
for r in sorted(rows,key=lambda r:r['old']-r['chosen'],reverse=True)[:15]:print(r)
