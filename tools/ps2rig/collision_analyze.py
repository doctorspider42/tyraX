"""Summarize physical-PS2 collision-stress phase CSV files."""
from pathlib import Path
import csv,json,argparse,statistics

def percentile(values,p):
 values=sorted(values)
 return values[min(len(values)-1,int((len(values)-1)*p))]
def analyze(root, complete=False, baseline_label='main-algorithms', solver_comparison=False, temporal_comparison=False,settle_check=False,temporal_arms=4):
 groups={};repeats={};phases=[];settle_results=[]
 for file in sorted(Path(root).glob('collision-phase-*.csv')):
  rows=list(csv.DictReader(file.open()))
  if not rows:continue
  if any(None in r or None in r.values() for r in rows):raise ValueError(f"Malformed CSV fields: {file}")
  h=rows[0];phase=int(h['phase']);phases.append(phase)
  if complete:
   assert [int(r['frame']) for r in rows]==list(range(150,1410 if settle_check else 750)),file
   assert {state:sum(r['state']==state for r in rows) for state in ['park','drive','settle']}==(dict(park=600,drive=60,settle=600) if settle_check else dict(park=120,drive=360,settle=120)),file
   variant=(phase//2)%7
   expected=dict(phase=phase,baseline=int(phase%2==phase//14%2),added=[0,32,64,128,32,64,128][variant],layout=int(variant>=4),repeat=phase//14)
   if solver_comparison:
    arms=temporal_arms if temporal_comparison else 3;rep=phase//(7*arms);variant=(phase//arms)%7;arm=arms-1-phase%arms if rep else phase%arms
    expected=dict(phase=phase,baseline=int(arm==0),added=[0,32,64,128,32,64,128][variant],layout=int(variant>=4),repeat=rep,solver_arm=arm)
   if settle_check:
    rep=phase//temporal_arms;arm=temporal_arms-1-phase%temporal_arms if rep else phase%temporal_arms
    expected=dict(phase=phase,baseline=int(arm==0),added=64,layout=1,repeat=rep,solver_arm=arm)
   for r in rows:
    assert all(int(r[k])==v for k,v in expected.items()),file
    assert 0<int(r['period_ticks'])<2949120000,file
    assert 0<=int(r['physics_ticks'])<=int(r['work_ticks'])<=int(r['period_ticks']),file
    if 'solver_ticks' in r:assert 0<=int(r['solver_ticks'])<=int(r['physics_ticks']),file
    if temporal_comparison:
     assert sum(int(r[k])for k in ['predict_ticks','world_ticks','body_ticks','solver_ticks','writeback_ticks'])<=int(r['physics_ticks']),file
     assert int(r['body_grid_ticks'])<=int(r['body_ticks']),file
     assert int(r['query_ticks'])<=int(r['world_ticks'])+int(r['body_ticks']),file
     assert int(r['sat_ticks'])<=int(r['world_ticks'])+int(r['body_ticks']),file
    assert all(__import__('math').isfinite(float(r[k])) for k in ['car_z','car_speed']),file
    if 'generated_contacts' in r and not int(r['baseline']):
     assert int(r['generated_contacts'])==int(r['contacts']),file
     assert int(r['push_candidates'])==int(r['push_tested']),file
     assert int(r['dropped_contacts'])==int(r['push_overflow'])==0,file
  if settle_check:
   def window(lo,hi):return [r for r in rows if lo<=int(r['frame'])<hi]
   pre=window(630,750);wake=window(750,753);post=window(1290,1410)
   if pre and wake and post:
    settle_results.append(dict(phase=phase,arm=int(h['solver_arm']),repeat=int(h['repeat']),park_awake_max=max(int(r['awake'])for r in pre),park_energy_mean=statistics.mean(float(r['velocity_energy'])for r in pre),wake_first_frames=[int(r['awake'])for r in wake],recovery_awake_max=max(int(r['awake'])for r in post),recovery_energy_mean=statistics.mean(float(r['velocity_energy'])for r in post),recovery_depth_max=max(float(r['max_depth'])for r in post)))
  for r in rows:
   key=(int(r['solver_arm']) if solver_comparison else int(r['baseline']),int(r['added']),int(r['layout']),r['state'])
   groups.setdefault(key,[]).append(r)
   repeats.setdefault((int(r['repeat']),key),[]).append(r)
 if complete:assert phases==list(range(temporal_arms*2 if settle_check else temporal_arms*14 if temporal_comparison else 42 if solver_comparison else 28)),phases
 result=[];repeat_result=[]
 entries=[(None,k,v) for k,v in sorted(groups.items())]+[(rep,k,v) for (rep,k),v in sorted(repeats.items())]
 for rep,(old,count,layout,state),rows in entries:
  period=[int(r['period_ticks'])/294912.0 for r in rows];work=[int(r['work_ticks'])/294912.0 for r in rows];phys=[int(r['physics_ticks'])/294912.0 for r in rows]
  target=result if rep is None else repeat_result
  target.append(dict(repeat=rep,arm=((([baseline_label,'cache-only','cache-warm-islands'] if temporal_arms==3 else [baseline_label,'cache-only','cache-warm','cache-warm-islands']) if temporal_comparison else [baseline_label,'prepared-scalar','prepared-vu0'])[old] if solver_comparison else baseline_label if old else 'candidate'),added=count,layout='pile' if layout else 'spread',state=state,n=len(rows),fps=1000/statistics.mean(period),period_p95=percentile(period,.95),period_p99=percentile(period,.99),work_mean=statistics.mean(work),work_p95=percentile(work,.95),work_p99=percentile(work,.99),phys_mean=statistics.mean(phys),phys_p95=percentile(phys,.95),phys_p99=percentile(phys,.99),missed=sum(p>25 for p in period),awake_max=max(int(r['awake'])for r in rows),contacts_max=max(int(r['contacts'])for r in rows),dropped=sum(int(r['dropped_contacts'])for r in rows),push_overflow=sum(int(r['push_overflow'])for r in rows),pushes=sum(int(r['pushes'])for r in rows),z_min=min(float(r['car_z'])for r in rows),z_max=max(float(r['car_z'])for r in rows)))
  if temporal_comparison:
   stages=['predict_ticks','world_ticks','body_ticks','body_grid_ticks','query_ticks','sat_ticks','writeback_ticks']
   target[-1]['stages']={k:dict(mean=statistics.mean(int(r[k])/294912. for r in rows),p95=percentile([int(r[k])/294912. for r in rows],.95)) for k in stages}
   assert all(__import__('math').isfinite(float(r[k])) and float(r[k])>=0 for r in rows for k in ['velocity_energy','max_depth'])
   target[-1].update(warm_hits=sum(int(r['warm_hits'])for r in rows),islands_max=max(int(r['islands'])for r in rows),velocity_energy_mean=statistics.mean(float(r['velocity_energy'])for r in rows),velocity_energy_p95=percentile([float(r['velocity_energy'])for r in rows],.95),max_depth=max(float(r['max_depth'])for r in rows))
  if 'solver_ticks' in rows[0]:
   ticks=[int(r['solver_ticks'])/294912.0 for r in rows]
   contact_total=sum(int(r['contacts'])for r in rows)
   target[-1]['solver_us_per_contact']=sum(ticks)*1000/contact_total if contact_total else None
   target[-1].update(solver_mean=statistics.mean(ticks),solver_p95=percentile(ticks,.95),solver_p99=percentile(ticks,.99))
 return dict(phases=phases,groups=result,repeats=repeat_result,settle_checks=settle_results)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('root');p.add_argument('--out');p.add_argument('--complete',action='store_true');p.add_argument('--baseline-label',default='main-algorithms');p.add_argument('--solver-comparison',action='store_true');p.add_argument('--temporal-comparison',action='store_true');p.add_argument('--settle-check',action='store_true');p.add_argument('--temporal-arms',type=int,choices=[3,4],default=4);a=p.parse_args();result=analyze(a.root,a.complete,a.baseline_label,a.solver_comparison or a.temporal_comparison or a.settle_check,a.temporal_comparison or a.settle_check,a.settle_check,a.temporal_arms)
 if a.out:Path(a.out).write_text(json.dumps(result,indent=2))
 print('phases',result['phases'])
 for g in result['groups']:
  if g['state']=='drive':print('{arm:15s} +{added:3d} {layout:6s} n={n:4d} fps={fps:5.1f} work95={work_p95:6.2f} phys95={phys_p95:6.2f} missed={missed:3d} awake={awake_max:3d} contacts={contacts_max:3d} drops={dropped:5d} pushoverflow={push_overflow:5d} pushes={pushes:4d} z={z_min:.1f}..{z_max:.1f}'.format(**g))
