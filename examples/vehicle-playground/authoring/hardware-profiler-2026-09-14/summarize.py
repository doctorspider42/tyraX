from pathlib import Path
import csv,json,statistics,hashlib,math
r=Path(__file__).parent
arms=['control','no-host','16bpp','no-extra-passes','scissor/game','unlit/game','control-repeat']
report={'platform':'physical PS2','rows_per_phase':240,'arms':{}}
for arm in arms:
 p=r/arm/'bin';csvfile=p/'frame-cost.csv'
 if not csvfile.exists():continue
 rows=list(csv.DictReader(csvfile.open()));fps=list(csv.DictReader((p/'district-benchmark.csv').open()))
 assert len(rows)==960 and len(fps)==32
 assert len(set(x['frame'] for x in rows))==960
 phases={}
 for phase,label in enumerate(['garage-day','garage-night','outer-day','outer-night']):
  rr=[x for x in rows if int(x['phase'])==phase];assert len(rr)==240
  keys=[k for k in rr[0] if k not in ['frame','phase']]
  assert all(math.isfinite(float(x[k])) and float(x[k])>=0 for x in rr for k in keys)
  work=sorted(sum(float(x[k]) for k in ['update_ms','submit_ms','finish_ms']) for x in rr)
  phases[label]={'mean':{k:statistics.mean(float(x[k]) for x in rr) for k in keys},'work_mean_ms':statistics.mean(work),'work_p95_ms':work[227],'fps_median':statistics.median(float(x['fps']) for x in fps if int(x['phase'])==phase)}
 report['arms'][arm]={'elf_sha256':hashlib.sha256((p/'vehicle-playground.elf').read_bytes()).hexdigest(),'phases':phases}
 print(arm,[round(x['work_mean_ms'],3) for x in phases.values()])
(r/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
