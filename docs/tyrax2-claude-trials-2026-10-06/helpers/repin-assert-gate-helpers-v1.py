from pathlib import Path
import hashlib,json
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'assert-gate-physical-v1';h=b/'assert-gate-root-helpers-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((f/'target-source-manifest.json').read_text());freeze=json.loads((f/'root-source-freeze.json').read_text());assert m['frozen'] and freeze['frozen'];assert freeze['sourceManifestSha256']==sha(f/'target-source-manifest.json')
for n,d in m['files'].items():assert sha(f/n)==d,n
p=h/'host-authority.json';d=json.loads(p.read_text());d['pricingSourceManifestSha256']=sha(f/'target-source-manifest.json')
for name,digest in d['sourcePins'].items():assert sha(Path(name))==digest,name
p.write_bytes((json.dumps(d,indent=2)+'\n').encode())
(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print('REPIN_AFTER_ROOT_FREEZE',d['pricingSourceManifestSha256'])
