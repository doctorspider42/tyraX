"""Strict completed private closeout parser; never device/source mutation."""
from pathlib import Path
import argparse,json,math,re,statistics
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--route',choices=['vehicle','gate'],required=True);a=p.parse_args()
def rows(text,label):
 result=[]
 for line in text.splitlines():
  line=re.sub(r'^\[\s*\d+\.\d+\]\s+(?=LOG: (?:CLOSEOUT|DAY))', '', line)
  if not line.startswith('LOG: '+label+' '):continue
  fields=line.split()[2:];r={}
  for pair in fields:
   assert '='in pair,pair;k,v=pair.split('=',1);assert k not in r;k.strip();r[k]=v
  result.append(r)
 return result
n=lambda r,k:int(r[k],10)
def index(rs,key,expected):
 assert len(rs)==len(expected);result={n(r,key):r for r in rs};assert len(result)==len(rs)and set(result)==set(expected);return result
def stats(values):
 if not values:return None
 v=sorted(x/294912 for x in values)
 def percentile(p):
  f=(len(v)-1)*p;lo=int(f);hi=math.ceil(f);return v[lo]+(v[hi]-v[lo])*(f-lo)
 return dict(count=len(v),meanMs=statistics.mean(v),p50Ms=percentile(.5),p95Ms=percentile(.95),maxMs=max(v),over16_68=sum(x>16.68 for x in v),over16_68Percent=100*sum(x>16.68 for x in v)/len(v),percentileMethod='linear interpolation over sorted raw values')
stdout=a.stdout.read_text(encoding='utf8',errors='replace');artifact=a.artifact.read_text(encoding='utf8');configs=rows(stdout,'CLOSEOUTCONFIG');dones=rows(stdout,'CLOSEOUTDONE');assert len(configs)==len(dones)==1;cfg=configs[0];done=dones[0];order=n(cfg,'order');kind=n(cfg,'kind');assert order==0 and kind==3
assert n(cfg,'valid')==n(done,'valid')==1 and n(done,'loops')==5400 and n(done,'order')==order and n(done,'kind')==kind
for k,v in dict(phases=3,phaseFrames=1800,taxFirst=800,taxLoops=320,sampleFirst=900,sampleCount=128,ordinaryClocks=1,addedWaits=0,frameProfile=0,hardwareTrace=0,devkit=0,sampleBytes=6144,chunkBytes=420).items():assert n(cfg,k)==v
phases=index(rows(stdout,'CLOSEOUTPHASE'),'phase',range(3));windows=index(rows(artifact,'CLOSEOUTWINDOW'),'phase',range(3));contexts=rows(stdout,'CLOSEOUTCONTEXT');assert len(contexts)==6
assert all(re.fullmatch(r'LOG: CLOSEOUT(CHUNK|RAW|WINDOW|VEH) .+',l)for l in artifact.splitlines())
raw=rows(artifact,'CLOSEOUTRAW');chunks=rows(artifact,'CLOSEOUTCHUNK');assert len(chunks)==15;results={}
contextmap={}
for phase in range(3):
 middle=(phase==1)!=(order==1);players=0;hidden=0;sampler=1;pr=phases[phase];wr=windows[phase]
 for k,v in dict(first=phase*1800,players=players,geometryHidden=hidden,sampler=sampler,countReads=262 if sampler else 6).items():assert n(pr,k)==v
 for k,v in dict(sampler=sampler,raw=128 if sampler else 0,chunks=5,valid=1).items():assert n(wr,k)==v
 cold=index([r for r in contexts if n(r,'phase')==phase],'offset',[750,1155]);contextmap[phase]=cold
 for r in cold.values():
  for k,v in dict(players=players,scene=0,requested=1,valid=1).items():assert n(r,k)==v
  assert n(r,'sceneGeneration')>0
  if a.route=='vehicle':
   assert n(r,'vehicleDriver')>=0 and n(r,'night')==0 and all(n(r,k)==1 for k in ('paused','hourZero'))
  else:
   assert n(r,'vehicleDriver')==-1 and n(r,'portalOther')==0
  assert 59900<=n(r,'refreshMilliHz')<=60010 and n(r,'width')>0 and n(r,'height')>0 and n(r,'depth')in(0,1,2)
  assert n(r,'oneArm')==1 and n(r,'route')==(1 if a.route=='vehicle' else 2)
  for key in (('positionX','positionY','positionZ','lookX','lookY','lookZ','playerX','playerY','playerZ')+ (('vehicleX','vehicleY','vehicleZ','vehicleSpeed')if a.route=='vehicle'else())):
   assert re.fullmatch('[0-9a-fA-F]{8}',r[key]) and (int(r[key],16)&0x7f800000)!=0x7f800000,key
  if kind==2:
   for k,v in dict(geometryMatched=11,geometryReady=1,geometryRejected=0,visibleMask=0 if hidden else 2047,activeMask=2047,savedMask=2047).items():assert n(r,k)==v
 rs=index([r for r in raw if n(r,'phase')==phase],'i',range(128)if sampler else[]);cs=index([r for r in chunks if n(r,'phase')==phase],'i',range(5));elapsed=pacing=rendered=synthetic=0
 values={k:[]for k in ('entryPeriod','wholeLoop','nonPacingLoop','singleRenderedPeriod')}
 for i,r in cs.items():
  assert n(r,'first')==phase*1800+800+i*64 and n(r,'loops')==64
  e,w=n(r,'elapsed'),n(r,'pacing');assert 0<=w<=e<0x80000000;assert n(r,'totalFlips')==n(r,'renderedFlips')+n(r,'syntheticFlips');elapsed+=e;pacing+=w;rendered+=n(r,'renderedFlips');synthetic+=n(r,'syntheticFlips')
 for i,r in rs.items():
  assert n(r,'frame')==phase*1800+900+i
  for k in ('entryPeriod','wholeLoop','pacing','presentPeriod','renderedPeriod'):assert 0<=n(r,k)<0x80000000
  assert n(r,'pacing')<=n(r,'wholeLoop')and n(r,'totalFlips')==n(r,'renderedFlips')+n(r,'syntheticFlips');assert n(r,'lastKind')in(0,1,2)
  for k in ('entryPeriod','wholeLoop'):values[k].append(n(r,k))
  values['nonPacingLoop'].append(n(r,'wholeLoop')-n(r,'pacing'))
  if n(r,'renderedFlips')==1:values['singleRenderedPeriod'].append(n(r,'renderedPeriod'))
 results[phase]=dict(players=players,geometryHidden=hidden,sampler=sampler,chunkMeanNonPacingMs=(elapsed-pacing)/320/294912,chunkMeanEntryMs=elapsed/320/294912,perChunkMeanEntryMs=[n(cs[i],'elapsed')/64/294912 for i in range(5)],maxObservedChunkMeanEntryMs=max(n(c,'elapsed')/64/294912 for c in cs.values()),renderedCompletions=rendered,syntheticCompletions=synthetic,rawStats={k:stats(v)for k,v in values.items()})
assert len(raw)==sum(128 if r['sampler']else 0 for r in results.values())
for key in ('video','display','depth','width','height','refreshMilliHz','sceneGeneration','vehicleDriver'):assert len({r[key]for r in contexts})==1,key
positionKeys=('vehicleX','vehicleY','vehicleZ')if a.route=='vehicle'else('playerX','playerY','playerZ')
moved={p:any(contextmap[p][750][k]!=contextmap[p][1155][k] for k in positionKeys)for p in range(3)}
assert any(moved.values()),'No actual cold endpoint movement established; route activation missing'
portalCounts=None
if a.route=='gate':
 ordered=[contextmap[p][o] for p in range(3)for o in (750,1155)]
 portalCounts={k:[n(r,k)for r in ordered]for k in ('portal0','portal1')}
 for counts in portalCounts.values():assert counts==sorted(counts) and counts[-1]>0,'actual portal roundtrip branch activation missing'
result=dict(status='PASS_COMPLETED_DAY_DRIVE_REPRESENTATIVE_PARK_PROTOCOL',route=a.route,portalColdCounts=portalCounts,byPhase=results,movedAtColdEndpoints=moved,oneArm=True,matchedAB=False,hardwarePerformanceQualified=False,nativeSixtyFpsAccepted=False,commonControllerObserverCostUnpriced=True,limitations=['Cold positions are endpoints, not trajectory proof.','Phase world state carries forward; no savings comparison.','Worst window is maximum observed64-loop mean, not worst frame or p99.','128 raw loops per phase do not cover all measured chunks.','Emulator qualifies controller/scene correctness only; hardware cadence remains separate.','Private quieting, controller and observer footprint remain unpriced; hardware performance needs root qualification.'])
import struct
def f(r,k):return struct.unpack('!f',bytes.fromhex(r[k]))[0]
poses=index(rows(stdout,'DAYPOSE'),'chunk',range(5));selections=rows(stdout,'DAYSELECT');parks=rows(stdout,'DAYPARK');assert len(selections)==len(parks)==1
selection=selections[0];park=parks[0];selected=n(selection,'chunk')
assert n(selection,'captured')==31 and n(selection,'valid')==1 and 0<=selected<5
assert n(park,'frame')==1700 and n(park,'selected')==selected and n(park,'parked')==n(park,'valid')==1
driveChunks=index([r for r in chunks if n(r,'phase')==0],'i',range(5))
for i,r in poses.items():
 assert n(r,'sampleFrame')==832+i*64
 assert n(r,'workTicks')==n(driveChunks[i],'elapsed')-n(driveChunks[i],'pacing')
 for k in ('carX','carY','carZ','yaw','pitch','roll','camX','camY','camZ','lookX','lookY','lookZ'):assert math.isfinite(f(r,k))
assert selected==max(range(5),key=lambda i:n(poses[i],'workTicks'))
assert moved[0], 'actual driving did not move car'
parkedPositions=[]
for phase in (1,2):
 for r in contextmap[phase].values():
  for cold,pose in [('positionX','camX'),('positionY','camY'),('positionZ','camZ'),('lookX','lookX'),('lookY','lookY'),('lookZ','lookZ')]:assert r[cold]==poses[selected][pose], 'parked camera differs from selected drive midpoint'
  driftXZ=math.hypot(f(r,'vehicleX')-f(poses[selected],'carX'),f(r,'vehicleZ')-f(poses[selected],'carZ'))
  assert driftXZ<0.3 and abs(f(r,'vehicleSpeed'))<0.1, 'car did not remain parked near selected point'
  parkedPositions.append(dict(phase=phase,offset=n(r,'offset'),x=f(r,'vehicleX'),y=f(r,'vehicleY'),z=f(r,'vehicleZ'),speed=f(r,'vehicleSpeed'),driftXZ=driftXZ))
result.update(selectedChunk=selected,representativeDrivePose=poses[selected],parkedPositions=parkedPositions,perChunkNonPacingMs={p:[(n(c,'elapsed')-n(c,'pacing'))/64/294912 for c in sorted([r for r in chunks if n(r,'phase')==p],key=lambda r:n(r,'i'))]for p in range(3)},matchedAB=False)
result['limitations']+=['Moving window changes its view; parked view is the selected midpoint, not a pixel-identical moving-frame A/B.','One-time relocation and speed clearing occur outside measured windows. Normal physics/logic continue.','New pose storage and fixed-camera selector overhead are unpriced.','hourZero is an inherited field name: in this fixture it verifies noon (12), not midnight.']
probe=rows(artifact,'CLOSEOUTVEH');assert len(probe)==384
paired={};calibration={};extraReads=0
def mean(xs):return statistics.mean(xs) if xs else None
def f32(x):return struct.unpack('!f',struct.pack('!f',x))[0]
def timer(phase,i):return phase==0 or (40<=i<88 if phase==1 else i<40 or i>=88)
for phase in range(3):
 ps=index([r for r in probe if n(r,'phase')==phase],'i',range(128))
 rs=index([r for r in raw if n(r,'phase')==phase],'i',range(128));combined=[]
 for i,v in ps.items():
  dt=f(v,'dtBits');assert math.isfinite(dt) and dt>0
  expected=min(4,max(1,int(f32(f32(dt*50)+0.5))))
  enabled=int(timer(phase,i));reads=2*enabled
  for k,value in dict(frame=phase*1800+900+i,planned=expected,executed=expected,enabled=enabled,addedReads=reads,captured=128).items():assert n(v,k)==value,(phase,i,k)
  elapsed=n(v,'elapsed');loop=n(rs[i],'wholeLoop');work=loop-n(rs[i],'pacing')
  assert 0<=elapsed<0x80000000 and elapsed<=work
  if not enabled:assert elapsed==0
  extraReads+=reads
  combined.append(dict(frame=n(v,'frame'),dtMs=dt*1000,steps=expected,timer=enabled,scopeMs=elapsed/294912 if enabled else None,nonPacingMs=work/294912,entryMs=n(rs[i],'entryPeriod')/294912,renderedPeriodMs=n(rs[i],'renderedPeriod')/294912))
 paired[phase]=combined
 if phase in (1,2):
  groups=[combined[:40],combined[40:88],combined[88:]]
  means=[mean([v['nonPacingMs']for v in group])for group in groups]
  outer=mean([v['nonPacingMs']for group in (groups[0],groups[2])for v in group])
  tax=means[1]-outer if phase==1 else outer-means[1]
  assert all(v['steps']==1 and v['entryMs']<25 for v in combined),'Parked tax control did not remain one-step/full-cadence'
  calibration[phase]=dict(order='Off40/On48/Off40'if phase==1 else'On40/Off48/On40',groupMeanNonPacingMs=means,outerMeanMs=outer,netActiveObserverContrastMs=tax,outerSpreadMs=abs(means[2]-means[0]),scopeMeanWhenOnMs=mean([v['scopeMs']for v in combined if v['timer']]),exactCameraColdVerified=True,tinyVehicleDriftReported=True,commonMetadataCostUnpriced=True)
assert extraReads==512
drive=paired[0];bySteps={}
for steps in range(1,5):
 group=[v for v in drive if v['steps']==steps]
 if group:bySteps[steps]=dict(count=len(group),meanScopeMs=mean([v['scopeMs']for v in group]),maxScopeMs=max(v['scopeMs']for v in group),meanNonPacingMs=mean([v['nonPacingMs']for v in group]),maxNonPacingMs=max(v['nonPacingMs']for v in group),meanResidualMs=mean([v['nonPacingMs']-v['scopeMs']for v in group]),overBudgetLoops=sum(v['nonPacingMs']>16.68333 for v in group),longPriorEntryLoops=sum(v['entryMs']>25 for v in group))
over=[v for v in drive if v['nonPacingMs']>16.68333]
transitions=[dict(frame=v['frame'],previousSteps=drive[i-1]['steps'],previousWorkMs=drive[i-1]['nonPacingMs'],previousScopeMs=drive[i-1]['scopeMs'],currentSteps=v['steps'],currentEntryMs=v['entryMs'])for i,v in enumerate(drive)if i and v['steps']>=2 and drive[i-1]['steps']==1]
result.update(status='PASS_COMPLETED_PHYSICS_SUBSTEP_SCOPE_PROTOCOL',addedScopedCountReads=extraReads,vehicleScopeDefinition='stepVehicles through updateVehicleTutorial; excludes smoke/skids/debris and renderer',driveByExecutedSteps=bySteps,driveOverBudgetScopeMeanMs=mean([v['scopeMs']for v in over]),driveOverBudgetResidualMeanMs=mean([v['nonPacingMs']-v['scopeMs']for v in over]),oneToMultiStepTransitions=transitions,parkedObserverCalibration=calibration,pairedRaw=paired,physicsOnlyCausalPrice=False)
result['limitations']+=['Scope includes private planned/executed counter overhead and elapsed clock path.','Executed-step cohorts differ in dt/world/view and do not price a causal extra substep.','Parked short-window tax contrast prices only enabled observer path; common disabled metadata/buffer footprint is unpriced.','Entry period is the preceding loop-entry interval; correlate step count against it and inspect prior measured loop for trigger.']
a.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(result['status'])
