from pathlib import Path
import struct,json
root=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-queue-audit-v2/vehicle/game/bin')
rows=[]
for p in sorted(root.rglob('*.tmdl')):
 d=p.read_bytes();at=0
 def take(n):
  global at
  assert at+n<=len(d);v=d[at:at+n];at+=n;return v
 def u():return struct.unpack('<I',take(4))[0]
 def mesh():
  n=u();take(n*32);a=u();assert a in (0,n);take(a);return n
 assert take(4)==b'TMDL';v=u();take(24);parts=u()
 for i in range(parts):
  name=take(32).split(b'\0')[0].decode();take(128+12+(12 if v>=2 else 0)+8)
  base=mesh();lc=u();lod=[mesh() for _ in range(lc)]
  run=u() if v>=4 else 0;st=mesh() if v>=4 else 0
  sl=[mesh() for _ in range(lc)] if v>=4 else []
  rows.append(dict(file=p.relative_to(root).as_posix(),part=name,base=base,strip=st,run=run,lod=lod,lodStrip=sl))
 if v>=3:take(u()*12)
 assert at==len(d)
Path('F:/Projects/tyrax2-lab-20261001/strip-inventory-all.json').write_text(json.dumps(rows,indent=2))
static=[r for r in rows if r['file'].startswith('models/')]
print(json.dumps(dict(parts=len(static),base=sum(r['base'] for r in static),render=sum(r['strip']or r['base'] for r in static),stripParts=sum(bool(r['strip']) for r in static),lod=sum(sum(r['lod']) for r in static),lodStrip=sum(sum(r['lodStrip']) for r in static)),indent=2))
for r in sorted(static,key=lambda r:r['base'] if not r['strip'] else 0,reverse=True)[:18]:print(r)
