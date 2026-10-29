"""Validate same-ELF VU0 shadow/timing gates and retain overlap evidence."""
from pathlib import Path
import argparse,csv,hashlib,json,math,re
from collision_analyze import analyze

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('root',type=Path);p.add_argument('--mode',choices=['verify','timing'],required=True)
 p.add_argument('--log',type=Path,required=True);p.add_argument('--metadata',type=Path,required=True)
 p.add_argument('--settle-check',action='store_true',help='Four stationary settle/kick/recovery phases')
 p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.settle_check:assert a.mode=='verify','Settle qualification requires full shadow verification'
 expected=list(range(4)) if a.settle_check else [12,13] if a.mode=='verify' else list(range(28))
 frame_end=1410 if a.settle_check else 750
 metadata=json.loads(a.metadata.read_text(encoding='utf8'))
 files=sorted(a.root.glob('collision-phase-*.csv'))
 assert [int(f.stem.rsplit('-',1)[1]) for f in files]==expected
 rows_total=0;metrics=[];hashes={}
 for f in files:
  rows=list(csv.DictReader(f.open()));phase=int(rows[0]['phase'])
  assert [int(r['frame'])for r in rows]==list(range(150,frame_end)),f
  assert len(rows)==frame_end-150
  for r in rows:
   assert int(r['generated_contacts'])==int(r['contacts']),f
   assert int(r['push_candidates'])==int(r['push_tested']),f
   assert int(r['dropped_contacts'])==int(r['push_overflow'])==0,f
   phys=int(r['physics_ticks']);body=int(r['body_ticks']);world=int(r['world_ticks'])
   assert 0<=phys<=int(r['work_ticks'])<=int(r['period_ticks']),f
   assert sum(int(r[k])for k in ['predict_ticks','world_ticks','body_ticks','solver_ticks','writeback_ticks'])<=phys,f
   assert int(r['body_grid_ticks'])<=body and int(r['query_ticks'])<=body+world and int(r['sat_ticks'])<=body+world,f
   assert all(math.isfinite(float(r[k]))for k in ['car_z','car_speed','velocity_energy','max_depth']),f
  side=a.root/f'vu0-phase-{phase:02d}.csv'
  values=list(csv.DictReader(side.open()));assert len(values)==1
  m={k:(float(v)if k.startswith('max_')else int(v))for k,v in values[0].items()};assert m['phase']==phase
  assert m['validation_errors']==0,side
  assert m['missed_reference_hits']==0,side
  assert m['max_depth_ratio']<=1.00001e-4 and m['max_extra_depth_ratio']<=1.00001e-4,side
  assert m['max_point_ratio']<=.0100001 and m['max_normal_error']<=.0100001,side
  assert 0<=m['busy_finish']<=m['batches']<=m['jobs']<=m['batches']*7,side
  if int(rows[0]['baseline']):assert m['jobs']==m['batches']==m['busy_ee_query']==0,side
  m['simulated_frames']=frame_end+(300 if phase==0 else 0)
  m['includes_warmup']=True
  m['wait_ms_per_simulated_frame']=m['wait_ticks']/294912/m['simulated_frames']
  if metadata.get('certified_negatives'):
   classification=a.root/f'vu0-class-{phase}.csv'
   classes=list(csv.DictReader(classification.open()));assert len(classes)==1,classification
   counts={k:int(v)for k,v in classes[0].items()}
   assert counts['phase']==phase and counts['jobs']==m['jobs'],classification
   reasons=['proved_negative','confirmed_negative','confirmed_ccd','confirmed_tie','confirmed_parallel']
   assert all(counts[k]>=0 for k in reasons),classification
   assert sum(counts[k]for k in reasons)<=m['jobs'],classification
   m['classifications']=counts
   hashes[classification.name]=hashlib.sha256(classification.read_bytes()).hexdigest()
  metrics.append(m);rows_total+=len(rows)
  for x in [f,side]:hashes[x.name]=hashlib.sha256(x.read_bytes()).hexdigest()
 assert sum(m['jobs']for m in metrics)>0
 assert sum(m['busy_ee_query']for m in metrics)>0,'No sampled EE/VU0 overlap'
 log=a.log.read_text(encoding='utf8',errors='replace')
 if metadata.get('min_axis_guard'):assert 'VUSATTIE cases=1 failures=0' in log
 if metadata.get('winning_axis_uncertainty'):assert 'VUSATCLEAR cases=1 failures=0' in log
 if metadata.get('common_motion_shortcut'):assert 'VUSATCOMMON cases=4 failures=0' in log
 if metadata.get('micro_precision'):assert 'VUSATPRECISION cases=50000 failures=0' in log
 if metadata.get('precision_replay'):assert re.search(r'VUSATREPLAY reference=0 .*accepted=1',log)
 assert f'VUSAT mode={a.mode}' in log
 assert 'VUSATCHECK cases=2048 failures=0 ' in log
 assert 'VU0OWNERCHECK switches=3 failures=0' in log
 assert 'VUSATCHECK FAIL' not in log
 assert 'VUSATDIFF ' not in log,'A hard runtime geometry error, including after the measured window, rejects qualification'
 done=4 if a.settle_check else 14 if a.mode=='verify' else 28
 assert f'CB DONE phases={done}' in log
 summary=analyze(a.root,complete=a.mode=='timing' or a.settle_check,baseline_label='scalar',solver_comparison=True,temporal_comparison=True,settle_check=a.settle_check,temporal_arms=2)
 # The reused temporal analyzer's second label is historical.
 def labels(value):
  if isinstance(value,dict):return {k:labels(v)for k,v in value.items()}
  if isinstance(value,list):return [labels(v)for v in value]
  return 'vu0-micro' if value=='cache-only' else value
 summary=labels(summary)
 out=dict(mode=a.mode,settle_check=a.settle_check,measured_frames=rows_total,startup_oracle_cases=2048,
          metadata=metadata,summary=summary,
          micro_metrics=metrics,raw_sha256=hashes,log_sha256=hashlib.sha256(a.log.read_bytes()).hexdigest(),
          accounting='All arms: generated=stored contacts, car candidates=tested, no capacity omissions; finite and scope gates pass. Reference geometry is compared only in verify mode.',
          reference_comparison_performed=a.mode=='verify',
          overlap_scope='Busy observations during EE queries; not a utilization percentage. Sidecars include warmup frames.')
 out['tolerances']=dict(depth_relative_radius=1e-4,extra_touch_relative_radius=1e-4,point_relative_radius=.01,normal_vector_error=.01,missed_reference_hits=0)
 a.output.write_text(json.dumps(out,indent=2),encoding='utf8')
 print(f'{a.mode}: {rows_total} frames, {sum(m["jobs"]for m in metrics)} jobs, {sum(m["busy_ee_query"]for m in metrics)} busy EE observations, zero validation/accounting failures')
if __name__=='__main__':main()
