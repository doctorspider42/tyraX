from pathlib import Path
import argparse,json,subprocess,sys,statistics
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
balance=a.out.with_suffix('.balance.json');subprocess.run([sys.executable,str(Path(__file__).with_name('analyze-balance.py')),str(a.stdout),'--artifact',str(a.artifact),'--out',str(balance)],check=True)
names=['unobserved_render','producer_send_remainder','queue_submit_remainder','open_chain_callback','snapshot_copy','native_emission','native_sizing','seal_flush','enqueue','cache_writeback']
events=['sends','submits','source_qwords','snapshot_calls','copied_bytes','borrowed_bytes','snapshot_failures','snapshot_retries','counted_snapshots','emission_calls','emission_records','recorded_returns','enqueues','flushes','close_callbacks','prepare_calls','sizing_loops','gif_calls']
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
raw=keyed('CLOSEOUTRAW');bal=keyed('CLOSEOUTBAL');q=keyed('CLOSEOUTQUEUE');assert raw.keys()==bal.keys()==q.keys()
calls=rows('CLOSEOUTQUEUECALLS');cm={r['phase']:r for r in calls};assert len(calls)==len(cm)==3
expected={'phase','i','frame','enabled','ok','reads','edges','wait','pacing'}|{'q'+str(j)for j in range(10)}|{'n'+str(j)for j in range(18)}
def stats(v):
 assert v;ms=sorted(t/294912 for t in v);return dict(count=len(ms),meanMs=statistics.mean(ms),medianMs=statistics.median(ms),maxMs=max(ms))
phases={};driving=[];totalReads=0
for phase in range(3):
 active=[];gaps=[]
 for i in range(128):
  d,b,r=q[phase,i],bal[phase,i],raw[phase,i];assert d.keys()==expected
  assert d['frame']==b['frame']==r['frame']and d['enabled']==b['enabled']and d['ok']==1
  assert d['reads']==2*d['edges'];totalReads+=d['reads']
  if not d['enabled']:assert all(d[k]==0 for k in expected-{'phase','i','frame','enabled','ok'})
  else:
   exclusive=b['gameEnd']-b['renderStart']-b['pace2']-sum(b['wait2'+str(j)]for j in range(3))
   assert sum(d['q'+str(j)]for j in range(10))==exclusive
   assert d['wait']==sum(b['wait2'+str(j)]for j in range(3))and d['pacing']==b['pace2']
   assert b['pacedCalls0']==b['pacedCalls2']==0 and b['pacedCalls1']==2
   assert d['n1']>0 and d['n2']>0 and d['n3']==d['n1']+d['n7']
   assert d['n6']==d['n7']==0 and d['n8']==d['n3']
   assert d['n9']==d['n11']==d['n1']and d['n10']>=d['n9']
   assert d['n13']>0 and d['n12']>=d['n13']
   assert d['q6']==d['n16']==0
   active.append(dict(frame=r['frame'],workTicks=r['wholeLoop']-r['pacing'],queueTicks={k:d['q'+str(j)]for j,k in enumerate(names)},counters={k:d['n'+str(j)]for j,k in enumerate(events)},extraCountReads=d['reads'],scopeEdges=d['edges']))
  if i<127:
   gap=raw[phase,i+1]['entryPeriod']-r['wholeLoop'];assert gap>=0;gaps.append((d['enabled'],gap))
 c=cm[phase];assert set(c)=={'phase'}|{'q'+str(j)for j in range(10)}
 totals={k:sum(x['counters'][k]for x in active)for k in events}
 for j,k in [(1,'sends'),(2,'submits'),(3,'close_callbacks'),(4,'snapshot_calls'),(5,'emission_calls'),(6,'sizing_loops'),(7,'flushes'),(8,'enqueues')]:assert c['q'+str(j)]==totals[k],(phase,j,k)
 phases[phase]=dict(enabledRows=len(active),queueStats={k:stats([x['queueTicks'][k]for x in active])for k in names},counterTotals=totals,scopeCalls={k:c['q'+str(j)]for j,k in enumerate(names)},countReadsPerRow=dict(count=len(active),mean=statistics.mean(x['extraCountReads']for x in active),median=statistics.median(x['extraCountReads']for x in active),max=max(x['extraCountReads']for x in active)),extraReads=sum(x['extraCountReads']for x in active),interLoopGapStats={str(on):stats([t for flag,t in gaps if flag==on])for on in sorted({v[0]for v in gaps})})
 if phase:phases[phase]['gapOnMinusOffMs']=phases[phase]['interLoopGapStats']['1']['meanMs']-phases[phase]['interLoopGapStats']['0']['meanMs']
 if phase==0:driving=active
heavy=[r for r in driving if r['workTicks']>294912*16.68333]
out=dict(status='PASS_COMPLETED_SELECTIVE_QUEUE_AUDIT',balance=json.loads(balance.read_text()),byPhase=phases,additionalQueueCountReads=totalReads,driveRows=driving,heavy=dict(count=len(heavy),queueStats={k:stats([x['queueTicks'][k]for x in heavy])for k in names}if heavy else{}),limitations=['Exclusive elapsed queue subpaths include clocks/counters/preemption. Unobserved render is an explicit residual, not queue work.','Same-ELF active observer contrasts do not price common footprint or establish a uniform per-scope subtraction.','Input qwords are not physical DMA traffic. Copied/borrowed bytes describe snapshot output ownership, not unique mesh data.','Calls/bytes refer to the 128-row active cohort only; moving workload and selected park pose can change with observer cadence.','Presentation leaf waits are covered by pacing and never charged twice. Dense stage/work tags are compiled no-ops.'])
a.out.write_text(json.dumps(out,indent=2)+'\n');print(out['status']);print('additional queue Count reads',totalReads)
