from pathlib import Path
import argparse,subprocess,sys,json,re,statistics
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
base=a.out.with_suffix('.base.json')
subprocess.run([sys.executable,str(Path(__file__).with_name('analyze-day-drive.py')),str(a.stdout),'--artifact',str(a.artifact),'--out',str(base),'--route','vehicle'],check=True)
def rows(label):
 result=[]
 for line in a.artifact.read_text().splitlines():
  if not line.startswith('LOG: '+label+' '):continue
  r={}
  for pair in line.split()[2:]:
   k,v=pair.split('=');assert k not in r;r[k]=int(v)
  result.append(r)
 return result
raw=rows('CLOSEOUTRAW');bal=rows('CLOSEOUTBAL');assert len(raw)==len(bal)==384
rm={(r['phase'],r['i']):r for r in raw};bm={(r['phase'],r['i']):r for r in bal};assert len(bm)==384 and rm.keys()==bm.keys()
def stats(v):
 v=sorted(x/294912 for x in v);assert v
 return dict(count=len(v),meanMs=statistics.mean(v),medianMs=statistics.median(v),maxMs=max(v))
phases={};drive=[];reads=0
for phase in range(3):
 priced=[];cohorts={k:[]for k in ('first','middle','last')}
 for i in range(128):
  r=rm[phase,i];b=bm[phase,i];enabled=phase==0 or ((40<=i<88)if phase==1 else not(40<=i<88))
  assert b['frame']==r['frame'] and b['enabled']==int(enabled) and b['ok']==1
  calls=sum(b['calls'+str(k)]for k in range(3));reads+=b['reads']
  work=r['wholeLoop']-r['pacing'];cohorts['first'if i<40 else'middle'if i<88 else'last'].append(work)
  if not enabled:
   assert all(b[k]==0 for k in ('gameStart','renderStart','gameEnd','reads','nested','jobs','firstSeq','lastSeq','jobPhaseMask'))
   assert all(b['wait'+str(j)+str(k)]==0 for j in range(3)for k in range(3))
   assert all(b['pace'+str(j)]==b['calls'+str(j)]==0 for j in range(3));continue
  assert b['reads']==3+2*calls and b['nested']==0
  gs,rs,ge=b['gameStart'],b['renderStart'],b['gameEnd'];assert 0<=gs<=rs<=ge<=r['wholeLoop']<0x80000000
  spans=[gs+r['wholeLoop']-ge,rs-gs,ge-rs];pace=[b['pace'+str(j)]for j in range(3)];assert sum(pace)==r['pacing']
  waits=[[b['wait'+str(j)+str(k)]for k in range(3)]for j in range(3)]
  pure=[spans[j]-pace[j]-sum(waits[j])for j in range(3)];assert all(x>=0 for x in pure)
  assert sum(pure)+sum(pace)+sum(map(sum,waits))==r['wholeLoop']
  assert b['jobs']==1 and b['firstSeq']==b['lastSeq']==r['sequence'] and b['firstCtx']==b['lastCtx']==r['context']
  assert b['firstOwner']==b['lastOwner'] and b['firstOwner']in(r['frame']-1,r['frame']) and b['jobPhaseMask']in(1,2,4)
  d=dict(frame=r['frame'],entry=r['entryPeriod'],whole=r['wholeLoop'],work=work,misc=pure[0],logic=pure[1],render=pure[2],vif=sum(w[0]for w in waits),gif=sum(w[1]for w in waits),gs=sum(w[2]for w in waits),pacing=sum(pace),reads=b['reads'],calls=calls,jobOwner=b['firstOwner'],jobPhase=b['jobPhaseMask'])
  priced.append(d)
  if phase==0:drive.append(d)
 phases[phase]=dict(enabledRows=len(priced),stats={k:stats([r[k]for r in priced])for k in ('whole','work','misc','logic','render','vif','gif','gs','pacing')},cohorts={k:stats(v)for k,v in cohorts.items()})
 if phase:
  c=phases[phase]['cohorts'];outer=(c['first']['meanMs']+c['last']['meanMs'])/2;middle=c['middle']['meanMs']
  phases[phase]['observerOnMinusOffMs']=middle-outer if phase==1 else outer-middle
  phases[phase]['outerSpreadMs']=abs(c['first']['meanMs']-c['last']['meanMs'])
over=[r for r in drive if r['work']>294912*16.68333]
result=dict(status='PASS_COMPLETED_LOOP_BALANCE_PROTOCOL',base=json.loads(base.read_text()),byPhase=phases,addedCountReadsInRawRows=reads,driveRows=drive,overBudget=dict(count=len(over),stats={k:stats([r[k]for r in over])for k in ('work','misc','logic','render','vif','gif','gs')}if over else{}),largestWorkRows=sorted(drive,key=lambda r:r['work'],reverse=True)[:10],limitations=['Elapsed buckets include preemption and instrumentation. Render phase includes HUD and renderer-side CPU work, not geometry alone.','Wait intervals include queue servicing and SDK call overhead; they are not pure VU/GS busy time.','Closure proves accounting consistency; source coverage review is separately required.','OFF retains code/data footprint and metadata; runtime contrasts do not price that common footprint.','Moving view differs across frames/boots. No automatic tax subtraction.'])
a.out.write_text(json.dumps(result,indent=2)+'\n');print(result['status']);print(json.dumps({k:v['observerOnMinusOffMs'] for k,v in phases.items()if k},indent=2))
