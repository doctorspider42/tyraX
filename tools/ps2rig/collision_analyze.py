"""Summarize physical-PS2 collision-stress phase CSV files."""
from pathlib import Path
import csv,json,argparse,statistics

def percentile(values,p):
 values=sorted(values)
 return values[min(len(values)-1,int((len(values)-1)*p))]
def analyze(root, complete=False, baseline_label='main-algorithms'):
 groups={};repeats={};phases=[]
 for file in sorted(Path(root).glob('collision-phase-*.csv')):
  rows=list(csv.DictReader(file.open()))
  if not rows:continue
  h=rows[0];phase=int(h['phase']);phases.append(phase)
  if complete:
   assert [int(r['frame']) for r in rows]==list(range(150,750)),file
   assert {state:sum(r['state']==state for r in rows) for state in ['park','drive','settle']}==dict(park=120,drive=360,settle=120),file
   variant=(phase//2)%7
   expected=dict(phase=phase,baseline=int(phase%2==phase//14%2),added=[0,32,64,128,32,64,128][variant],layout=int(variant>=4),repeat=phase//14)
   for r in rows:
    assert all(int(r[k])==v for k,v in expected.items()),file
    assert 0<int(r['period_ticks'])<2949120000,file
    assert 0<=int(r['physics_ticks'])<=int(r['work_ticks'])<=int(r['period_ticks']),file
    assert all(__import__('math').isfinite(float(r[k])) for k in ['car_z','car_speed']),file
    if 'generated_contacts' in r and not int(r['baseline']):
     assert int(r['generated_contacts'])==int(r['contacts']),file
     assert int(r['push_candidates'])==int(r['push_tested']),file
     assert int(r['dropped_contacts'])==int(r['push_overflow'])==0,file
  for r in rows:
   key=(int(r['baseline']),int(r['added']),int(r['layout']),r['state'])
   groups.setdefault(key,[]).append(r)
   repeats.setdefault((int(r['repeat']),key),[]).append(r)
 if complete:assert phases==list(range(28)),phases
 result=[];repeat_result=[]
 entries=[(None,k,v) for k,v in sorted(groups.items())]+[(rep,k,v) for (rep,k),v in sorted(repeats.items())]
 for rep,(old,count,layout,state),rows in entries:
  period=[int(r['period_ticks'])/294912.0 for r in rows];work=[int(r['work_ticks'])/294912.0 for r in rows];phys=[int(r['physics_ticks'])/294912.0 for r in rows]
  target=result if rep is None else repeat_result
  target.append(dict(repeat=rep,arm=baseline_label if old else 'candidate',added=count,layout='pile' if layout else 'spread',state=state,n=len(rows),fps=1000/statistics.mean(period),period_p95=percentile(period,.95),period_p99=percentile(period,.99),work_mean=statistics.mean(work),work_p95=percentile(work,.95),work_p99=percentile(work,.99),phys_mean=statistics.mean(phys),phys_p95=percentile(phys,.95),phys_p99=percentile(phys,.99),missed=sum(p>25 for p in period),awake_max=max(int(r['awake'])for r in rows),contacts_max=max(int(r['contacts'])for r in rows),dropped=sum(int(r['dropped_contacts'])for r in rows),push_overflow=sum(int(r['push_overflow'])for r in rows),pushes=sum(int(r['pushes'])for r in rows),z_min=min(float(r['car_z'])for r in rows),z_max=max(float(r['car_z'])for r in rows)))
 return dict(phases=phases,groups=result,repeats=repeat_result)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('root');p.add_argument('--out');p.add_argument('--complete',action='store_true');p.add_argument('--baseline-label',default='main-algorithms');a=p.parse_args();result=analyze(a.root,a.complete,a.baseline_label)
 if a.out:Path(a.out).write_text(json.dumps(result,indent=2))
 print('phases',result['phases'])
 for g in result['groups']:
  if g['state']=='drive':print('{arm:15s} +{added:3d} {layout:6s} n={n:4d} fps={fps:5.1f} work95={work_p95:6.2f} phys95={phys_p95:6.2f} missed={missed:3d} awake={awake_max:3d} contacts={contacts_max:3d} drops={dropped:5d} pushoverflow={push_overflow:5d} pushes={pushes:4d} z={z_min:.1f}..{z_max:.1f}'.format(**g))
