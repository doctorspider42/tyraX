from pathlib import Path
import struct,json,hashlib
root=Path(__file__).parent;bin=root/'vehicle/game/bin';reports=[]
for stem in ['veh-ravager000001-body','veh-ravager000001-wheel','veh-cc96playground01-body']:
 p=bin/'vehicles'/(stem+'.tmdl');data=p.read_bytes();at=0
 def take(n):
  global at
  assert at+n<=len(data);v=data[at:at+n];at+=n;return v
 def u():return struct.unpack('<I',take(4))[0]
 def mesh():
  n=u();take(n*32);a=u();assert a in (0,n);take(a);return n
 assert take(4)==b'TMDL';version=u();assert 1<=version<=4;take(24);count=u();parts=[]
 for i in range(count):
  name=take(32).split(b'\0')[0].decode();texture=take(64).split(b'\0')[0].decode();take(64);take(12)
  if version>=2:take(12)
  take(8);vertices=mesh();lods=u()
  for j in range(lods):mesh()
  run=u()if version>=4 else 0;strip=mesh()if version>=4 else 0
  if version>=4:
   for j in range(lods):mesh()
  assert (not strip and not run)or(run>0 and strip>0)
  parts.append(dict(name=name,listVertices=vertices,stripVertices=strip,stripRun=run,texture=texture))
 if version>=3:shadow=u();take(shadow*12)
 assert at==len(data),(stem,at,len(data))
 reports.append(dict(file=p.relative_to(bin).as_posix(),sha256=hashlib.sha256(data).hexdigest(),version=version,parts=parts))
out=dict(status='READ_ONLY_SHIPPED_STRIP_ASSET_INVENTORY',reports=reports,limitations='Baked stream availability only; not runtime strip admission, visibility, submitted counts or physical speed.')
(root/'strip-asset-inventory.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
