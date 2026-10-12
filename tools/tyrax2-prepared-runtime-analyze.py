"""A32 combined first frontend stage; retain A31 gates plus actual frontend contracts."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-runtime-analyze.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("plan['experiment']=='A31-owner'", "plan['experiment']=='A32-front' and plan['preparedFrontend'] and plan['frontNetProducerCost'] and plan['combinedWorldOwner']")
replace("assert 'WorldRuntime::active=ClipTrial::mode==7;prepareWorldOwners();' in game",
        "assert 'WorldRuntime::active=ClipTrial::mode==7;FrontRuntime::active=WorldRuntime::active;prepareWorldOwners();' in game")
replace("assert 'else b->worldOwner.reset();' in game",
        "assert 'else {b->worldOwner.reset();b->frontOwner.value.reset();}' in game")
replace("assert 'ClipTrial::' not in core",
        "assert 'ClipTrial::mode' not in core and 'if(ClipTrial::observe){++FrontRuntime::trialDraws;' in core")
replace('phases=validate(rows,text)',r'''phases=validate(rows,text)
assert plan['preparedFrontendStage']=='owned-validation-capacity-bounds' and plan['frontPartBudget']==8192
front=(root/'tyra/engine/inc/debug/prepared_runtime.hpp').read_text()
facts=(root/'tyra/engine/inc/debug/prepared_front.hpp').read_text()
assert 'PartBudget=8192' in front and 'if(next>PartBudget-liveParts)return false;' in front
assert 'if(!preparedFront){' in core and 'auto* preparedFront=admitFrontGeometry(bag);' in core
assert 'preparedFront?preparedFront:cacher.getBBoxes' in core
assert 'preparedFront?bag->frontOwner.value->key().capacity:getMaxVertCountByBag(bag)' in core
assert 'A32 stale capacity' in core and 'A32 stale main bounds' in core and 'A32 stale child bounds' in core
assert '(!FrontRuntime::active && !WorldRuntime::cold)' in core
assert 'FrontRuntime::active=WorldRuntime::active;prepareWorldOwners();' in game
assert 'if(!g.matrixMode)for(auto& p:g.parts)' in game
assert 'else b->frontOwner.value.reset();' in game
assert 'b->frontGeneration==UINT64_MAX' in core and 'k.generation<=lastGeneration_' in facts

def frontendContracts(log):
 records=[]
 pattern=r'\bFRONTCONTRACT phase=(\d+) arm=(\d+) sources=(\d+) draws=(\d+) parts=(\d+) hash=([0-9a-f]{8}) live=(\d+) peak=(\d+) owners=(\d+)'
 for row in re.findall(pattern,log):
  records.append(tuple(int(v,16 if i==5 else 10) for i,v in enumerate(row)))
 assert len(records)==count*5
 for phase in range(count):
  group=records[phase*5:phase*5+5]
  assert [(r[0],r[1]) for r in group]==[(phase,arm) for arm in range(5)]
  assert len({(r[2],r[4],r[5]) for r in group})==1
  for _,arm,sources,draws,parts,digest,live,peak,owners in group:
   assert sources>0 and parts>=sources and digest
   assert (draws==sources) if arm==1 else draws==0
   assert 0<live<=peak<=8192 and 0<owners<=live
 return records
def frontendMoving(log):
 records=[tuple(map(int,r)) for r in re.findall(r'\bFRONTWITNESS phase=(\d+) mode=(\d+) frames=(\d+) draws=(\d+) parts=(\d+)',log)]
 assert len(records)==count and [r[0] for r in records]==list(range(count))
 for record,phase in zip(records,phases):
  _,mode,frames,draws,parts=record
  assert mode==phase['mode'] and frames==phase['witnessFrames']>0
  assert (draws>0 and parts>=draws) if mode==7 else draws==parts==0
 return records
frontCold=frontendContracts(text);frontMoving=frontendMoving(text)
frontNegatives=[]
for label,bad,check in (
 ('missing-front',text.replace('FRONTCONTRACT phase=0 arm=1','MISSINGFRONTCONTRACT phase=0 arm=1'),frontendContracts),
 ('inactive-front',re.sub(r'(FRONTCONTRACT phase=0 arm=1 sources=\d+ draws=)\d+',r'\g<1>0',text),frontendContracts),
 ('front-control',re.sub(r'(FRONTCONTRACT phase=0 arm=0 sources=\d+ draws=)\d+',r'\g<1>1',text),frontendContracts),
 ('front-source',re.sub(r'(FRONTCONTRACT phase=0 arm=1 sources=)\d+',r'\g<1>0',text),frontendContracts),
 ('front-part-budget',re.sub(r'(FRONTCONTRACT phase=0 arm=1[^\r\n]* peak=)\d+',r'\g<1>8193',text),frontendContracts),
 ('front-digest',re.sub(r'(FRONTCONTRACT phase=0 arm=1[^\r\n]* hash=)[0-9a-f]{8}',r'\g<1>00000000',text),frontendContracts),
 ('missing-moving-front',text.replace('FRONTWITNESS phase=1','MISSINGFRONTWITNESS phase=1'),frontendMoving),
 ('inactive-moving-front',re.sub(r'(FRONTWITNESS phase=1 mode=7 frames=\d+ draws=)\d+',r'\g<1>0',text),frontendMoving),
 ('moving-front-control',re.sub(r'(FRONTWITNESS phase=0 mode=0 frames=\d+ draws=)\d+',r'\g<1>1',text),frontendMoving)):
 try:check(bad)
 except AssertionError:frontNegatives.append(label)
 else:raise AssertionError('A32 frontend negative escaped: '+label)
''')
replace("report['residentWords']=resident", """report['negativeControls'].extend(frontNegatives)
report['preparedFrontendContracts']=frontCold
report['movingPreparedFrontend']=frontMoving
report['preparedFrontendStage']='owned-validation-capacity-bounds'
report['combinedWorldOwner']=True
report['residentWords']=resident""")
replace('world-runtime-{platform}.json','prepared-runtime-{platform}.json')
replace('Producer-owned compiled partial-world VIF bodies; live heads and same coarse native shader both arms; full-source/residual/moving/raster gates; net elapsed, not pure EE cycles',
        'Combined A31 owner + A32 first frontend stage (owned validation/capacity/bounds); original live view/light/texture/head/dispatch retained; net elapsed, not isolated A32 or pure EE cycles')
exec(compile(source,str(__file__),'exec'))
