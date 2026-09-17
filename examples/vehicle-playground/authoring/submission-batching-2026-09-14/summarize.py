
from pathlib import Path
import csv, json, hashlib, statistics, math
r=Path(__file__).parent
manifest=json.loads((r/'asset-manifest.json').read_text())
report={}
for arm in ['control','candidate','textured','large','final','production','gated','repeat']:
 b=r/arm/'bin'
 if not (b/'frame-cost.csv').exists():continue
 rows=list(csv.DictReader((b/'frame-cost.csv').open()))
 assert len(rows)==960,(arm,len(rows))
 assert len({x['frame'] for x in rows})==960
 assets={str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in b.rglob('*') if p.suffix.lower() in ['.png','.tmdl','.mtl']}
 assert assets==manifest,arm
 phases=[]
 for phase in range(4):
  rr=[x for x in rows if int(x['phase'])==phase];assert len(rr)==240
  keys=[k for k in rr[0] if k not in ['frame','phase']]
  assert all(math.isfinite(float(x[k])) and float(x[k])>=0 for x in rr for k in keys)
  means={k:statistics.mean(float(x[k]) for x in rr) for k in keys}
  work=sorted(sum(float(x[k]) for k in ['update_ms','submit_ms','finish_ms']) for x in rr)
  phases.append({'mean':means,'work_ms':statistics.mean(work),'work_p95_ms':work[227]})
 report[arm]={'elf_sha256':hashlib.sha256((b/'vehicle-playground.elf').read_bytes()).hexdigest(),'assets':len(assets),'phases':phases}
 print(arm,[(round(x['mean']['submit_ms'],3),round(x['mean']['flushes'],1),round(x['work_ms'],3)) for x in phases])
(r/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
