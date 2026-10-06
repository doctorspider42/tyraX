from pathlib import Path
p=Path(__file__).parent
s=(p.parent/'tyrax2-city-day-drive-v1/analyze-day-drive.py').read_text()
s=s.replace('(CHUNK|RAW|WINDOW)', '(CHUNK|RAW|WINDOW|VEH)')
marker='a.out.write_text(json.dumps(result,indent=2)';pos=s.index(marker)
extra='''probe=rows(artifact,'CLOSEOUTVEH');assert len(probe)==384
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
'''
s=s[:pos]+extra+s[pos:];(p/'analyze-physics.py').write_bytes(s.encode())
print('PHYSICS_PARSER_READY')
