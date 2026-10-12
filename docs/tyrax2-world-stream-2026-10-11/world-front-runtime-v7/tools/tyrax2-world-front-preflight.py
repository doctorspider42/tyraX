"""A35 complete parent correctness/early activation gates, explicitly NO pricing."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-runtime-analyze.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("assert 'ClipTrial::' not in core",
 "assert 'ClipTrial::mode' not in core and 'if(ClipTrial::observe)++WorldFront::movingInside;' in core")
replace("plan['experiment']=='A31-owner'","plan['experiment']=='A35-world-front' and plan['worldFront'] and plan['commonWorldHot'] and plan['originalHeads']")
replace("assert 'CoarseStrip::reset();ClipConsumer::observe=frame==31;' in physics",
 "assert 'CoarseStrip::reset();WorldFront::resetOracle();ClipConsumer::observe=frame==31;' in physics")
replace("assert 'WorldRuntime::active=ClipTrial::mode==7;prepareWorldOwners();' in game",
 "assert 'WorldRuntime::active=true;WorldFront::active=ClipTrial::mode==7;prepareWorldOwners();' in game")
replace("assert 'if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active)return;' in game",
 "assert 'if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active && frontSeenActive==WorldFront::active)return;' in game")
replace('assert (draws>0 and qwords>=4*draws) if mode==7 else draws==qwords==0',
 'assert draws>0 and qwords>=4*draws')
replace("('moving-owner-control',re.sub(r'(WORLDWITNESS phase=0 mode=0 frames=\\d+ draws=)\\d+',r'\\g<1>1',text)),",
 "('inactive-common-owner',re.sub(r'(WORLDWITNESS phase=0 mode=0 frames=\\d+ draws=)\\d+',r'\\g<1>0',text)),")
replace("report['performanceCandidate']=True",r'''assert plan['worldFrontStage']=='early-Core-unqualified' and not plan['performanceCandidate']
assert plan['worldFrontOwnerLimit']==1024 and plan['worldFrontWholeHeadOracle']
assert sha(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp')==plan['worldFrontRendererSha256']
assert 'if(renderWorldTicket(bag,reception))return;' in core
assert core.index('if(renderWorldTicket(bag,reception))return;')<core.index('HardwareTrace::Scope traceBag("Static_bag")')
assert 'qbufferRenderer.prepareWorldCullBody(b,capacity)' in core
assert 'qbufferRenderer.worldBodyReady(b)' in core and 'qbufferRenderer.worldCullReady()' in core
assert 'A35 lost prepared cull body after head' in core and 'A35 lost prepared partial body after head' in core
assert 'if(!WorldFront::active)WorldFront::Owner::releaseAll();' in game
assert 'WorldFront::active=arm==1;prepareWorldOwners();' in physics
assert 'headHot' not in plan and 'ObjectHead::' not in renderer
def frontWitness(log):
 records=[tuple(map(int,r)) for r in re.findall(r'\bFRONTWITNESS phase=(\d+) mode=(\d+) frames=(\d+) inside=(\d+) partial=(\d+) outside=(\d+) live=(\d+) peak=(\d+)',log)]
 assert len(records)==count and [r[0] for r in records]==list(range(count))
 for row,phase in zip(records,phases):
  _,mode,frames,inside,partial,outside,live,peak=row
  assert mode==phase['mode'] and frames==phase['witnessFrames']>0 and live<=peak<=1024
  if mode==7:assert inside>0 and partial>0 and live>0
  else:assert inside==partial==outside==live==0
 return records
def frontOracle(log):
 records=[tuple(int(v,16 if i==7 else 10) for i,v in enumerate(r)) for r in re.findall(r'\bFRONTORACLE phase=(\d+) arm=(\d+) inside=(\d+) partial=(\d+) outside=(\d+) heads=(\d+) words=(\d+) hash=([0-9a-f]{8})',log)]
 assert len(records)==count*5
 for phase in range(count):
  group=records[phase*5:phase*5+5]
  assert [(r[0],r[1]) for r in group]==[(phase,arm) for arm in range(5)]
  assert len({r[5:] for r in group})==1
  for _,arm,inside,partial,outside,heads,words,digest in group:
   assert heads>0 and words>=11*heads and digest
   if arm==1:assert inside>0 and partial>0
   else:assert inside==partial==outside==0
 return records
frontMoving=frontWitness(text);frontCold=frontOracle(text)
def frontAdmission(log):
 records=[tuple(map(int,r)) for r in re.findall(r'\bFRONTADMISSION phase=(\d+) checks=(\d+) headUnchanged=(\d+)',log)]
 assert len(records)==count and [r[0] for r in records]==list(range(count))
 assert all(checks>=39 and unchanged==1 for _,checks,unchanged in records)
 return records
if plan.get('worldFrontTargetAdmission',False):
 assert 'unsigned StaPipCore::verifyWorldTicketAdmission(StaPipBag* b)' in core
 assert 'A35 rejected ticket emitted a head' in core and 'A35 stale ticket admitted' in core
 assert 'stapip.core.verifyWorldTicketAdmission(ch.bag.get())' in physics
 report['targetAdmission']=frontAdmission(text)
 for label,bad in (
  ('missing-target-admission',text.replace('FRONTADMISSION phase=1','MISSINGFRONTADMISSION phase=1')),
  ('duplicate-target-admission',text+'\n'+next(l for l in text.splitlines() if 'FRONTADMISSION phase=1' in l)),
  ('target-admission-not-tested',re.sub(r'(FRONTADMISSION phase=1 checks=)\d+',r'\g<1>0',text)),
  ('target-admission-head-write',text.replace('headUnchanged=1','headUnchanged=0')),
  ('target-admission-wrong-phase',text.replace('FRONTADMISSION phase=1','FRONTADMISSION phase=0'))):
  try:frontAdmission(bad)
  except AssertionError:report['negativeControls'].append(label)
  else:raise AssertionError('A35 admission negative escaped: '+label)
for label,bad,parser in (
 ('missing-front-moving',text.replace('FRONTWITNESS phase=1','MISSINGFRONTWITNESS phase=1'),frontWitness),
 ('inactive-front-inside',re.sub(r'(FRONTWITNESS phase=1[^\r\n]* inside=)\d+',r'\g<1>0',text),frontWitness),
 ('inactive-front-partial',re.sub(r'(FRONTWITNESS phase=1[^\r\n]* partial=)\d+',r'\g<1>0',text),frontWitness),
 ('front-baseline-leak',re.sub(r'(FRONTWITNESS phase=0[^\r\n]* live=)\d+',r'\g<1>1',text),frontWitness),
 ('front-budget-overflow',re.sub(r'(FRONTWITNESS phase=1[^\r\n]* peak=)\d+',r'\g<1>1025',text),frontWitness),
 ('missing-front-oracle',text.replace('FRONTORACLE phase=0 arm=1','MISSINGFRONTORACLE phase=0 arm=1'),frontOracle),
 ('changed-actual-head',re.sub(r'(FRONTORACLE phase=0 arm=1[^\r\n]* hash=)[0-9a-f]{8}',r'\g<1>00000000',text),frontOracle),
 ('inactive-cold-front',re.sub(r'(FRONTORACLE phase=0 arm=1 inside=)\d+',r'\g<1>0',text),frontOracle),
 ('cold-front-control',re.sub(r'(FRONTORACLE phase=0 arm=0 inside=)\d+',r'\g<1>1',text),frontOracle)):
 try:parser(bad)
 except AssertionError:report['negativeControls'].append(label)
 else:raise AssertionError('A35 preflight negative escaped: '+label)
report['earlyFrontWitness']=frontMoving
report['actualWholeHeadOracle']=frontCold
report['performanceCandidate']=False
report['worldFrontStage']='early-Core-preflight-only'
report['originalHeads']=True
report['commonWorldHot']=True
report.pop('contrasts',None);report.pop('firstOrderGate',None)''')
replace('world-runtime-{platform}.json','world-front-preflight-{platform}.json')
start=source.index("if a.physical:report['firstOrderGate']=")
end=source.index("\nreport['reverse']=",start);source=source[:start]+source[end+1:]
start=source.index('if a.physical and reverse:\n');end=source.index('output=a.output or root/',start)
source=source[:start]+source[end:]
exec(compile(source,str(__file__),'exec'))
