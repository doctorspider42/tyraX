"""A34 independently prepared hot object heads; common world owner in both arms."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-runtime-analyze.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("plan['experiment']=='A31-owner'", "plan['experiment']=='A34-head-hot' and plan['headHot'] and plan['commonWorldHot'] and plan['headNetCompileCost']")
replace("assert 'CoarseStrip::reset();ClipConsumer::observe=frame==31;' in physics",
        "assert 'CoarseStrip::reset();HeadTrial::reset();ClipConsumer::observe=frame==31;' in physics")
replace("assert 'WorldRuntime::active=ClipTrial::mode==7;prepareWorldOwners();' in game",
        "assert 'WorldRuntime::active=true;HeadHot::active=ClipTrial::mode==7;prepareWorldOwners();' in game")
replace("assert 'if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active)return;' in game",
        "assert 'if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active && headSeenActive==HeadHot::active)return;' in game")
replace('assert (draws>0 and qwords>=4*draws) if mode==7 else draws==qwords==0',
        'assert draws>0 and qwords>=4*draws')
# Common body-owner baseline admission must be positive, not treated as leakage.
replace("('moving-owner-control',re.sub(r'(WORLDWITNESS phase=0 mode=0 frames=\\d+ draws=)\\d+',r'\\g<1>1',text)),",
        "('inactive-common-owner',re.sub(r'(WORLDWITNESS phase=0 mode=0 frames=\\d+ draws=)\\d+',r'\\g<1>0',text)),")
head=Path(__file__).with_name('tyrax2-object-head-analyze.py').read_text()
begin=head.index('def heads(log):')
end=head.index("report['performanceCandidate']=False",begin)
cold=head[begin:end].replace('\\\\','\\')
insert=r'''assert plan['objectHeadStage']=='direct-semantic-hot-owner'
assert plan['headWordBudget']==32768 and not plan['performanceCandidate']
assert sha(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp')==plan['objectHeadRendererSha256']
assert 'if(HeadHot::active && !ClipConsumer::observe)' in renderer
assert 'objectDataPacket->next+=count;bagMayClip=true;objectDataPending=true;' in renderer
assert 'A34 semantic head differs from original writer' in renderer
assert 'if(ClipConsumer::observe)testPreparedObjectHead(' in renderer
assert 'cell->image.writeBytes(facts,live,destination,capacity)' in renderer
assert 'if(!installObjectHead(bag,facts,index,ref))return false;' in renderer
assert 'bag->lighting || bag->billboard || vuCustomEnabled || hasProgramOverrides()' in renderer
assert 'HeadHot::active=arm==1;prepareWorldOwners();' in physics
assert 'if(!HeadHot::active)HeadHot::Owner::releaseAll();' in game
assert 'std::make_unique<HeadHot::Owner>(&bag->headOwner.value)' in renderer
assert 'testPreparedObjectHead' not in core
def movingHeads(log):
 records=[tuple(map(int,r)) for r in re.findall(r'\bHEADWITNESS phase=(\d+) mode=(\d+) frames=(\d+) draws=(\d+) qwords=(\d+) compiles=(\d+) live=(\d+) peak=(\d+)',log)]
 assert len(records)==count and [r[0] for r in records]==list(range(count))
 for record,phase in zip(records,phases):
  _,mode,frames,draws,words,compiles,live,peak=record
  assert mode==phase['mode'] and frames==phase['witnessFrames']>0
  assert 0<=live<=peak<=32768
  if mode==7:assert draws>0 and words>=11*draws and live>0
  else:assert draws==words==compiles==live==0
 return records
movingHead=movingHeads(text)
for label,bad in (
 ('missing-moving-head',text.replace('HEADWITNESS phase=1','MISSINGHEADWITNESS phase=1')),
 ('inactive-moving-head',re.sub(r'(HEADWITNESS phase=1 mode=7 frames=\d+ draws=)\d+',r'\g<1>0',text)),
 ('head-baseline-leak',re.sub(r'(HEADWITNESS phase=0 mode=0 frames=\d+ draws=)\d+',r'\g<1>1',text)),
 ('empty-moving-head',re.sub(r'(HEADWITNESS phase=1 mode=7 frames=\d+ draws=\d+ qwords=)\d+',r'\g<1>0',text)),
 ('head-budget-overflow',re.sub(r'(HEADWITNESS phase=1[^\r\n]* peak=)\d+',r'\g<1>32769',text))):
 try:movingHeads(bad)
 except AssertionError:report['negativeControls'].append(label)
 else:raise AssertionError('A34 moving head negative escaped: '+label)
report['movingHeads']=movingHead
report['commonWorldHot']=True
report['objectHeadStage']='direct-semantic-hot-owner'
report['admittedHotOriginalHead']=False
report['unsupportedOriginalHead']=True
report['headNetCompileCost']=True
'''
replace("report['performanceCandidate']=True",insert+cold+"report['performanceCandidate']=True")
replace('world-runtime-{platform}.json','head-hot-{platform}.json')
replace('Producer-owned compiled partial-world VIF bodies; live heads and same coarse native shader both arms; full-source/residual/moving/raster gates; net elapsed, not pure EE cycles',
        'Independent semantic hot heads with common A31 body owner; original unsupported/Core paths retained; full byte/source/raster and net compile gates; elapsed, not pure EE cycles')
exec(compile(source,str(__file__),'exec'))
