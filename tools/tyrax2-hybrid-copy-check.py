"""A41 full preserved scene, copy ownership and physical completion gates."""
import argparse,copy,csv,hashlib,json,math,re,statistics,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true');p.add_argument('--output',type=Path)
p.add_argument('--physical-dir',type=Path,help='Separate physical attempt directory; requires --physical')
a=p.parse_args();root=a.fixture;platform='physical' if a.physical else 'emulator'
if a.physical_dir and not a.physical:p.error('--physical-dir requires --physical')
physical=a.physical_dir or root/'physical'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
manifest=json.loads((root/'hybrid-copy-manifest.json').read_text())
assert manifest['experiment'] in ('A41','A42','A43') and manifest['modes'] in ([0,1,0]*2,[1,0,1]*2)
retainReady=manifest['experiment']=='A43'
lateReady=manifest['experiment'] in ('A42','A43')
if lateReady:assert manifest['copyModes']==[1]*6 and manifest['heldCopy']==[1]*3
else:assert 'copyModes' not in manifest and 'heldCopy' not in manifest
copyModes=manifest['copyModes'] if lateReady else manifest['modes']
heldCopy=manifest['heldCopy'] if lateReady else manifest['held']
readyModes=manifest['readyModes'] if retainReady else manifest['modes']
heldReady=manifest['heldReady'] if retainReady else manifest['held']
if retainReady:assert readyModes==[1]*6 and heldReady==[1]*3
assert manifest['held']==manifest['modes'][:3] and manifest['buffers']==[2]*6
enabled='!=1' if manifest['modes'][0] else '==1'
firstCandidate=manifest['modes'].index(1)
firstHeldCandidate=manifest['held'].index(1)
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
for name,digest in manifest['headers'].items():assert sha(root/'tyra/engine/inc/debug'/name)==digest,name
folder=physical/'bin' if a.physical else root/'game/bin'
logpath=physical/'console.log' if a.physical else root/'emulator/emulator.log'
log=logpath.read_text(errors='replace')
launch=json.loads((physical/'editor-deploy.json' if a.physical else root/'emulator/launch.json').read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==binding['elfSha256']==launch['elfSha256']
if a.physical:
 assert launch['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
 for rel,digest in json.loads((physical/'launch-inputs.json').read_text(encoding='utf-8-sig')).items():assert sha(folder/rel)==digest,rel
parentBinding=json.loads((root/'a38-native-binding.json').read_text())
assert manifest['parentElf']==parentBinding['elfSha256']
changed={'tyra/engine/src/engine.cpp','tyra/engine/src/renderer/core/renderer_core.cpp',
 'tyra/engine/src/renderer/core/gs/renderer_core_gs.cpp','tyra/engine/inc/renderer/core/gs/renderer_core_gs.hpp',
 'tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp','tyra/engine/inc/renderer/core/paths/path1/vif1_queue.hpp',
 'game/src/gen/game_physics.gen.cpp'}
for rel,digest in parentBinding['files'].items():
 if rel not in changed:assert sha(root/rel)==digest,rel
core=(root/'tyra/engine/src/renderer/core/renderer_core.cpp').read_text()
assert 'const bool copied=HybridCopyTrial::complete(pipelineSequence);' in core
assert 'if(!copied)sync.waitAndClear();' in core
assert 'if (!copied && gs.getFrameBufferCount() < 3 && isFrameLimitOn) graph_wait_vsync();' in core
queue=(root/'tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp').read_text()
assert 'volatile u32 nativePresentationSequence = 0;' in queue
assert 'if (framePipelined) nativePresentationSequence = nativeInFlight;' in queue
run=(root/'tyra/engine/src/engine.cpp').read_text()
physics=(root/'game/src/gen/game_physics.gen.cpp').read_text()
if retainReady:
 assert f'setDiagnosticTriple(false);HybridCopyTrial::enabled=true;HybridCopyTrial::setLateReady(true);HybridCopyTrial::retainNativeReady=MotionProbe::phase%3{enabled};' in run
 assert f'core.setDiagnosticTriple(false,true);setupProjShadows();HybridCopyTrial::enabled=true;HybridCopyTrial::setLateReady(true);HybridCopyTrial::retainNativeReady=arm{enabled};HybridCopyTrial::heldQualification=true;' in physics
 assert f'HybridCopyTrial::heldQualification=false;HybridCopyTrial::setLateReady(true);HybridCopyTrial::retainNativeReady=MotionProbe::phase%3{enabled};' in physics
elif lateReady:
 assert f'setDiagnosticTriple(false);HybridCopyTrial::enabled=true;HybridCopyTrial::setLateReady(MotionProbe::phase%3{enabled});' in run
 assert f'core.setDiagnosticTriple(false,true);setupProjShadows();HybridCopyTrial::enabled=true;HybridCopyTrial::setLateReady(arm{enabled});HybridCopyTrial::heldQualification=true;' in physics
 assert f'HybridCopyTrial::heldQualification=false;HybridCopyTrial::setLateReady(MotionProbe::phase%3{enabled});' in physics
if lateReady:
 trial=(root/'tyra/engine/inc/debug/tyrax2-hybrid-copy-trial.hpp').read_text()
 assert 'lateReadySupport = true;' in trial and 'AddDmacHandler(DMAC_VIF1,nativeISR,0)' in trial
 assert 'static_assert(!TYRA_VIF1_QUEUE_ISR' in trial
 assert 'r32(0x1000e010)&(1u<<17)' in trial
 assert 'r64(0x12001010)=ownedIMR;' in trial
 assert log.count('HYBRIDRESTORE dmac=0 timers=0 imr=1 intc=1')==1
else:
 assert f'setDiagnosticTriple(false);HybridCopyTrial::enabled=MotionProbe::phase%3{enabled};' in run
 assert f'core.setDiagnosticTriple(false,true);setupProjShadows();HybridCopyTrial::enabled=arm{enabled};' in physics
 assert f'HybridCopyTrial::enabled=MotionProbe::phase%3{enabled};' in physics
assert 'if(frame==1){' in physics and 'u32(copyNow-copyStart)<11796480u' in physics
assert 'renderScene();engine->renderer.endFrame();\n    }\n    core.synchronizeFrame();core.sync.align2D();' in physics
if retainReady:
 assert 'retainReadySupport = true;' in trial
 assert 'adapter.takeMain(job,retainReadySupport && retainNativeReady)' in trial
 assert 'action=adapter.takeMain(job,true);' in trial
 adapter=(root/'tyra/engine/inc/debug/tyrax2-hybrid-copy-adapter.hpp').read_text()
 assert 'retainUntilReady && protocol.state() == Stage::Armed' in adapter
 assert '(!hw.oldJobReady(job) || !hw.gifIdle() || !hw.finishSet())' in adapter
gs=(root/'tyra/engine/src/renderer/core/gs/renderer_core_gs.cpp').read_text()
assert 'if(HybridCopyTrial::skipNext)HybridCopyTrial::skipNext=false;' in gs
assert 'settings->getDisplayMode()!=DisplayMode::Progressive480p' in gs
assert 'TripleTrial::record(index);' not in gs
# Reuse complete baseline travel, roads/reflections, immutable consumer and all
# 36 main/env pixel gates, not an A38 presentation or timer-sampler contract.
parent=Path(__file__).with_name('tyrax2-pc-moving-check.py')
source=parent.read_text();start=source.index('rows=[{k:float(v)');end=source.index('\ndef recordGate',start)
source=source[start:end].replace("periodUs=manifest['periods'][phase]","buffers=2")
assert source.count("r['flips']==1")==1
assert 'renderer.core.synchronizeFrame();HybridCopyTrial::begin(MotionProbe::phase);' in run
# Common capture-boundary retirement precedes begin(): its flip is excluded,
# leaving exactly zero at FIRST gas row only; subsequent rows retain one flip.
source=source.replace("r['flips']==1","r['flips']==int(r['offset']!=1320)")
exec(compile(source,str(parent),'exec'))
for phase in range(6):
 reference=None
 for arm in range(3):
  path=folder/f'tape-{phase*3+arm}-display.bin';raw=path.read_bytes()
  w,h,psm,stage=struct.unpack('<4I',raw[:16])
  assert (w,h,psm,stage)==(448,448,2,phase*3+arm)
  assert len(raw)==16+w*h*2 and len(set(raw[16:]))>8
  if reference is None:reference=raw[16:]
  assert raw[16:]==reference,(phase,arm,'display')
  images.append({'phase':phase,'arm':arm,'view':'display','sha256':sha(path)})
def modes(text):
 m=[tuple(map(int,v)) for v in re.findall(r'\bTRIPLEMODE phase=(\d+) requested=(\d+) actual=(\d+)',text)]
 h=[tuple(map(int,v)) for v in re.findall(r'\bTRIPLEHELD phase=(\d+) arm=(\d+) actual=(\d+)',text)]
 assert m==[(i,0,2) for i in range(6)]
 assert h==[(i,j,2) for i in range(6) for j in range(3)]
modes(log)
def witness(text):
 pattern=r'\bHYBRIDCOPY '+''.join(k+r'=(\d+) ' for k in
  ('phase','held','arm','enabled','installed','armed','isr','fallback','completed','outside','late','refused','rows','dropped'))+r'words=(\d+)'
 data=[tuple(map(int,v)) for v in re.findall(pattern,text)]
 assert len(data)==24
 assert sorted((v[0],v[1],v[2]) for v in data)==sorted(
  [(i,0,i%3) for i in range(6)]+[(i,1,j) for i in range(6) for j in range(3)])
 for phase,held,arm,on,installed,armed,isr,fallback,done,outside,late,refused,n,dropped,words in data:
  assert on==(heldCopy[arm] if held else copyModes[phase])
  assert installed==1 and words==81 and dropped==refused==0
  assert outside==late==0,'Copy completion was not witnessed inside qualified blank'
  if on:
   assert armed==isr+fallback==done>0 and n==2*done and fallback>0
   if held or a.physical:assert isr>0,'Candidate never independently dispatched an old native copy'
  else:assert armed==isr==fallback==done==n==0
 return data
witnesses=witness(log)
readyWitnesses=[]
if lateReady:
 def readyWitness(text):
  data=[tuple(map(int,v)) for v in re.findall(r'HYBRIDREADY phase=(\d+) held=(\d+) arm=(\d+) enabled=(\d+) vif=(\d+) gs=(\d+) copies=(\d+)',text)]
  assert len(data)==24
  assert sorted((v[0],v[1],v[2]) for v in data)==sorted((v[0],v[1],v[2]) for v in witnesses)
  for phase,held,arm,on,vif,gs,n in data:
   assert on==(heldReady[arm] if held else readyModes[phase])
   if on:
    assert vif>0 and gs>0
    if held or a.physical:assert n>0,'No actual late-ready IRQ copy activation'
   else:assert vif==gs==n==0
  return data
 readyWitnesses=readyWitness(log)
 for label,pattern,replacement in (
  ('ready-mode',rf'(HYBRIDREADY phase=0 held=1 arm={firstHeldCandidate} enabled=)1',r'\g<1>0'),
  ('ready-activation',rf'(HYBRIDREADY phase=0 held=1 arm={firstHeldCandidate} enabled=1 vif=\d+ gs=\d+ copies=)\d+',r'\g<1>0')):
  bad=re.sub(pattern,replacement,log);assert bad!=log
  try:readyWitness(bad)
  except AssertionError:negatives.append(label)
  else:raise AssertionError(label)
retainWitnesses=[]
if retainReady:
 def retainWitness(text):
  data=[tuple(map(int,v)) for v in re.findall(r'HYBRIDRETAIN phase=(\d+) held=(\d+) arm=(\d+) enabled=(\d+) waits=(\d+)',text)]
  assert len(data)==24
  assert sorted(v[:3] for v in data)==sorted(v[:3] for v in witnesses)
  for phase,held,arm,on,waits in data:
   assert on==(manifest['held'][arm] if held else manifest['modes'][phase])
   bound=[r for r in witnesses if r[:3]==(phase,held,arm)];assert len(bound)==1
   assert 0<=waits<=bound[0][5]
   if not on:assert waits==0
   elif a.physical and not held:assert waits>0,'No retained native wait activation'
  return data
 retainWitnesses=retainWitness(log)
 firstOff=manifest['held'].index(0)
 for label,pattern,replacement in (
  ('retain-mode',rf'(HYBRIDRETAIN phase=0 held=1 arm={firstHeldCandidate} enabled=)1',r'\g<1>0'),
  ('retain-off-wait',rf'(HYBRIDRETAIN phase=0 held=1 arm={firstOff} enabled=0 waits=)0',r'\g<1>1')):
  bad=re.sub(pattern,replacement,log);assert bad!=log
  try:retainWitness(bad)
  except AssertionError:negatives.append(label)
  else:raise AssertionError(label)
eventPins={};copyDurations=[]
def copies(data,n,allowLate=False):
 assert len(data)==n and len(data)%2==0
 seen=set()
 for kick,done in zip(data[::2],data[1::2]):
  assert kick['kind'] in ((1,3) if allowLate else (1,)) and done['kind']==2 and kick['job']==done['job']!=0
  assert kick['job'] not in seen;seen.add(kick['job'])
  elapsed=(done['tick']-kick['tick'])&0xffffffff
  # EE Count / BUSCLK-div256 is512. Ages alone can alias after a missed
  # whole field; require completion witnessed within the admitted budget too.
  assert 0<elapsed<288*512,'Completion exceeded admitted budget or aliased a later field'
  for row in (kick,done):
   assert set(row)=={'job','tick','kind','a0','b','a1','valid'} and row['valid']==1
   assert 0<=row['a0']<=row['a1']<800 and 8700<=row['b']-row['a1']<=8900
  assert kick['a1']<=done['a0'],'Completion belongs to a later physical blank'
  assert kick['a1']+288<800,'Kick had insufficient admitted remaining budget'
 return [((d['tick']-k['tick'])&0xffffffff)/294912 for k,d in zip(data[::2],data[1::2])]
for phase,held,arm,on,installed,armed,isr,fallback,done,outside,late,refused,n,dropped,words in witnesses:
 path=folder/f'hybrid-copy-{phase}-{held}-{arm}.csv'
 data=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(path.open())]
 notification=lateReady and (heldReady[arm] if held else readyModes[phase])
 durations=copies(data,n,notification);eventPins[path.relative_to(root).as_posix()]=sha(path)
 if lateReady:
  proof=[r for r in readyWitnesses if r[:3]==(phase,held,arm)];assert len(proof)==1
  assert sum(r['kind']==3 for r in data)==proof[0][6]<=isr
 if not held:copyDurations+=durations
 if phase==firstCandidate and held==0:
  for label,mutate in [('missing-copy-finish',lambda r:r.pop()),
   ('wrong-copy-job',lambda r:r[1].update(job=r[0]['job']+1)),
   ('outside-blank-copy',lambda r:r[1].update(valid=0)),
   ('insufficient-copy-budget',lambda r:r[0].update(a1=600)),
   ('over-budget-copy',lambda r:r[1].update(tick=(r[0]['tick']+288*512)&0xffffffff)),
   ('next-field-finish',lambda r:r[1].update(a0=0,a1=0))]:
   bad=copy.deepcopy(data);mutate(bad)
   try:copies(bad,n,notification)
   except AssertionError:negatives.append(label)
   else:raise AssertionError(label)
for label,bad in [('third-buffer',log.replace('actual=2','actual=3')),
 ('inactive-ISR',re.sub(rf'(HYBRIDCOPY phase=0 held=1 arm={firstHeldCandidate} enabled=1 installed=1 armed=\d+ isr=)\d+',r'\g<1>0',log)),
 ('late-copy',log.replace('outside=0','outside=1')),
 ('copy-overflow',log.replace('dropped=0','dropped=1'))]:
 try:modes(bad);witness(bad)
 except AssertionError:negatives.append(label)
 else:raise AssertionError(label)
presentations=[]
for phase in range(6):
 match=re.search(rf'\bTRIPLEPRESENT phase={phase} rows=(\d+) dropped=0\b',log)
 assert match
 path=folder/f'triple-present-{phase}.csv'
 data=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(path.open())]
 assert len(data)==int(match[1]) and phases[phase]['driveFrames']<=len(data)<=phases[phase]['driveFrames']+2
 assert all(set(r)=={'tick','slot','isr'} and r['slot']==1 and r['isr'] in (0,1) for r in data)
 if not copyModes[phase]:assert all(r['isr']==0 for r in data)
 else:
  matched=[r for r in witnesses if r[0]==phase and r[1]==0];assert len(matched)==1
  assert len(data)==matched[0][8] and sum(r['isr'] for r in data)==matched[0][6]
  if a.physical:assert any(r['isr']==1 for r in data)
 intervals=[((s['tick']-r['tick'])&0xffffffff)/294912 for r,s in zip(data,data[1:])][2:-2]
 assert intervals and all(0<v<1000 for v in intervals)
 item={'phase':phase,'rows':len(data)}
 if a.physical:
  item.update(copyKickHz=len(intervals)*1000/sum(intervals),p95Ms=sorted(intervals)[math.ceil(.95*len(intervals))-1],
   maxMs=max(intervals),lateIntervals=sum(v>25 for v in intervals))
 presentations.append(item);eventPins[path.relative_to(root).as_posix()]=sha(path)
report={'valid':True,'platform':platform,'experiment':manifest['experiment'],'elfSha256':binding['elfSha256'],
 'sourceFiles':len(binding['files']),'phases':phases,'images':images,'contracts':contracts,
 'witnesses':witnesses,'presentations':presentations,'eventPins':eventPins,'negativeControls':negatives,
 'readerSha256':sha(Path(__file__)),'baselineReaderSha256':sha(parent),'logSha256':sha(logpath),
 'promotionAllowed':False,'scope':'Actual same Hybrid2 workload and physical completion witness; experimental admission not universal WCET'}
if lateReady:report['readyWitnesses']=readyWitnesses
if retainReady:report['retainWitnesses']=retainWitnesses
if a.physical:
 report['copyWitnessMs']={'n':len(copyDurations),'median':statistics.median(copyDurations),'max':max(copyDurations)}
 contrasts=[]
 for base in (0,3):
  if manifest['modes'][base]:
   candidates=[presentations[base],presentations[base+2]];off=presentations[base+1]
   contrasts.append({'hour':12 if base==0 else 0,
    'netGate':all(v['copyKickHz']>off['copyKickHz'] for v in candidates),
    'stable60Gate':all(v['copyKickHz']>=59 and v['lateIntervals']==0 for v in candidates),
    'candidates':candidates,'controls':[off]})
   continue
  on=presentations[base+1];off=[presentations[base],presentations[base+2]]
  contrasts.append({'hour':12 if base==0 else 0,'netGate':all(on['copyKickHz']>v['copyKickHz'] for v in off),
   'stable60Gate':on['copyKickHz']>=59 and on['lateIntervals']==0,'candidate':on,'controls':off})
 report['contrasts']=contrasts
output=a.output or root/f'hybrid-copy-{platform}.json'
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('images','contracts','eventPins')},indent=2))
