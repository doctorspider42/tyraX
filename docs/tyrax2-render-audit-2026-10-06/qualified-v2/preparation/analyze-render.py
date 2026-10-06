from pathlib import Path
import argparse,json,subprocess,sys,statistics
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
balance=a.out.with_suffix('.balance.json');subprocess.run([sys.executable,str(Path(__file__).with_name('analyze-balance.py')),str(a.stdout),'--artifact',str(a.artifact),'--out',str(balance)],check=True)
stages=['other','frame_setup','day_night','scene_preparation','reflection','portal','sky','terrain','occlusion','static_batches','roads','procedural','objects','wheels_animation','shadows','light_pools','particles_beams','hud_effects','end_frame','highlights']
work=['render_other','core_preparation','bounds','packages_dispatch','geometry_copy','ee_clip','texture','object_state','packet_build','submission','cache_flush']
counters=['bag_calls','bag_input_vertices','package_routes','classified_vertex_inputs','geometry_copy_calls','ee_clip_input_vertices','texture_upload_calls','texture_chain_qwords','submit_source_qwords']
def rows(label):
 out=[]
 for line in a.artifact.read_text().splitlines():
  if not line.startswith('LOG: '+label+' '):continue
  r={}
  for pair in line.split()[2:]:k,v=pair.split('=');assert k not in r;r[k]=int(v)
  out.append(r)
 return out
def keyed(label):
 rs=rows(label);d={(r['phase'],r['i']):r for r in rs};assert len(rs)==len(d)==384;return d
raw=keyed('CLOSEOUTRAW');bal=keyed('CLOSEOUTBAL');aud=keyed('CLOSEOUTRENDER');assert raw.keys()==bal.keys()==aud.keys()
calls=rows('CLOSEOUTRENDERCALLS');assert len(calls)==3;cm={r['phase']:r for r in calls};assert len(cm)==3
def stats(v):
 assert v;v=sorted(x/294912 for x in v);return dict(count=len(v),meanMs=statistics.mean(v),medianMs=statistics.median(v),maxMs=max(v))
priced={p:[]for p in range(3)};pairs={p:[]for p in range(3)};readTotal=0
expected={'phase','i','frame','enabled','ok','reads','edges','wait','pacing'}|{'s'+str(i)for i in range(20)}|{'w'+str(i)for i in range(11)}|{'n'+str(i)for i in range(9)}
for p0 in range(3):
 for i in range(128):
  r,b,d=raw[p0,i],bal[p0,i],aud[p0,i];assert d.keys()==expected
  assert d['frame']==b['frame']==r['frame']and d['enabled']==b['enabled']and d['ok']==1
  assert d['reads']==2*d['edges'];readTotal+=d['reads']
  if not d['enabled']:
   assert all(d[k]==0 for k in expected-{'phase','i','frame','enabled','ok'});continue
  render=b['gameEnd']-b['renderStart']-b['pace2']-sum(b['wait2'+str(k)]for k in range(3))
  ss={name:d['s'+str(j)]for j,name in enumerate(stages)};ws={name:d['w'+str(j)]for j,name in enumerate(work)};ns={name:d['n'+str(j)]for j,name in enumerate(counters)}
  assert sum(ss.values())==sum(ws.values())==render
  assert d['wait']==sum(b['wait2'+str(k)]for k in range(3))and d['pacing']==b['pace2']
  assert b['pacedCalls0']==b['pacedCalls2']==0 and 1<=b['pacedCalls1']<=8
  assert d['reads']>0 and ns['bag_calls']>0 and ns['bag_input_vertices']>0
  priced[p0].append(dict(frame=r['frame'],workTicks=r['wholeLoop']-r['pacing'],renderTicks=render,stageTicks=ss,workTicksByType=ws,counters=ns,extraCountReads=d['reads'],scopeTransitions=d['edges']))
 # The inherited measured interval excludes sample-copy/verification postlude.
 # Next entry minus prior wholeLoop measures the inter-loop gap, by prior mode.
 for i in range(127):
  gap=raw[p0,i+1]['entryPeriod']-raw[p0,i]['wholeLoop'];assert gap>=0
  pairs[p0].append(dict(i=i,enabled=aud[p0,i]['enabled'],gapTicks=gap))
results={}
for p0 in range(3):
 ps=priced[p0];c=cm[p0];assert set(c)=={'phase'}|{'s'+str(i)for i in range(20)}|{'w'+str(i)for i in range(11)}
 for j in (1,2,3,17,18):assert c['s'+str(j)]==len(ps),(p0,j,c['s'+str(j)],len(ps))
 results[p0]=dict(enabledRows=len(ps),stageStats={k:stats([r['stageTicks'][k]for r in ps])for k in stages},workTypeStats={k:stats([r['workTicksByType'][k]for r in ps])for k in work},counterTotals={k:sum(r['counters'][k]for r in ps)for k in counters},stageCalls={name:c['s'+str(j)]for j,name in enumerate(stages)},workCalls={name:c['w'+str(j)]for j,name in enumerate(work)},extraReads=sum(r['extraCountReads']for r in ps),interLoopGapStats={str(on):stats([r['gapTicks']for r in pairs[p0]if r['enabled']==on])for on in sorted({r['enabled']for r in pairs[p0]})})
 if p0:
  off=results[p0]['interLoopGapStats']['0']['meanMs'];on=results[p0]['interLoopGapStats']['1']['meanMs'];results[p0]['gapOnMinusOffMs']=on-off
drive=priced[0];heavy=[r for r in drive if r['workTicks']>294912*16.68333]
heavyResult=dict(count=len(heavy),stageStats={k:stats([r['stageTicks'][k]for r in heavy])for k in stages}if heavy else{},workTypeStats={k:stats([r['workTicksByType'][k]for r in heavy])for k in work}if heavy else{})
out=dict(status='PASS_COMPLETED_TWO_AXIS_RENDER_AUDIT',balance=json.loads(balance.read_text()),byPhase=results,additionalRenderCountReads=readTotal,heavy=heavyResult,driveRows=drive,largestWorkRows=sorted(drive,key=lambda r:r['workTicks'],reverse=True)[:10],limitations=['Stage and work type are two partitions of the same render time; never add both tables.','GIF waits nested inside presentation are covered by pacing, separately counted and never timed again as leaf work.','RenderOther is work outside annotated engine scopes, including game-side preparation; stage Other is explicit root gaps.','Stage labels price where work executes; interleaving can submit earlier collected geometry under a later stage.','Calls include inactive effect gate checks. Counters are input/route events, not unique vertices or final GS triangles.','Texture chain QWs omit REF pixel payload. Submit source QWs can include rerecording and are not physical DMA traffic.','Scope clocks/counters and common footprint perturb execution. Calibration prices active instrumentation only.','Inter-loop gap is an elapsed endpoint observation and includes postlude/prelude/preemption; no instruction-retirement claim.'])
a.out.write_text(json.dumps(out,indent=2)+'\n');print(out['status']);print('render Count reads',readTotal)
