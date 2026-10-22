"""A38 complete current-scene net/presentation qualification; no emulator pricing."""
import argparse,collections,copy,csv,hashlib,json,math,re,statistics,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true');p.add_argument('--output',type=Path)
p.add_argument('--archive',action='store_true')
a=p.parse_args();root=a.fixture;platform='physical' if a.physical else 'emulator'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
if a.archive:
 assert a.output and not a.output.resolve().is_relative_to(root.resolve())
 for rel,digest in json.loads((root/'archive-sha256.json').read_text()).items():assert sha(root/rel)==digest,rel
b=json.loads((root/'native-binding.json').read_text());manifest=json.loads((root/'triple-manifest.json').read_text())
assert manifest['experiment']=='A38' and manifest['phases']==[2,3,2,2,3,2] and manifest['held']==[2,3,2]
for rel,digest in b['files'].items():assert sha(root/rel)==digest,rel
folder=root/('physical/bin' if a.physical else 'game/bin');logpath=root/('physical/console.log' if a.physical else 'emulator/emulator.log')
log=logpath.read_text(errors='replace');launch=json.loads((root/('physical/editor-deploy.json' if a.physical else 'emulator/launch.json')).read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==launch['elfSha256']==b['elfSha256']
if a.physical:
 assert launch['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
 omitted=json.loads((root/'omitted-physical-inputs.json').read_text()) if a.archive else {}
 for rel,digest in json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig')).items():
  assert (sha(folder/rel) if (folder/rel).exists() else omitted.get(rel))==digest,rel
core=(root/'tyra/engine/src/renderer/core/renderer_core.cpp').read_text()
assert 'settings.setTripleBuffering(on);rebuildPermanentBuffers();hasPresentedFrame=false;' in core
assert 'path3Fence();sync.align2D();if(path1.isVU1Configured())sync.align3D();' in core
gs=(root/'tyra/engine/src/renderer/core/gs/renderer_core_gs.cpp').read_text()
assert 'TripleTrial::inVBlank=true;presentFrameBuffer(static_cast<u8>(queued));TripleTrial::inVBlank=false;' in gs
assert 'TripleTrial::record(index);' in gs
assert 'PcMoving::' not in (root/'tyra/engine/src/engine.cpp').read_text()
parentBinding=json.loads((root/'parent-native-binding.json').read_text())
assert parentBinding['elfSha256']==manifest['parentElf']
headWriter='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
assert sha(root/headWriter)==parentBinding['files'][headWriter], 'Original SDK head writer changed'
parent=Path(__file__).with_name('tyrax2-pc-moving-check.py')
source=parent.read_text();start=source.index('rows=[{k:float(v)');end=source.index('\ndef recordGate',start)
source=source[start:end].replace("periodUs=manifest['periods'][phase]","buffers=manifest['phases'][phase]")
# TEX0 contains the relocated VRAM addresses. This experiment does not change
# the original head writer; retain head counts/lengths and full raster/source
# equality, but raw head hash invariance is not a valid relocation contract.
old="group=parsed[phase*3:phase*3+3];assert all(r[2:]==group[0][2:] for r in group),name"
assert source.count(old)==1
source=source.replace(old,"group=parsed[phase*3:phase*3+3];assert all((r[2:-1] if name=='FRONTORACLE' else r[2:])==(group[0][2:-1] if name=='FRONTORACLE' else group[0][2:]) for r in group),name")
# Reuse the complete A37 ordinary baseline travel/owner/header/36-raster gate,
# not its sampler-specific lifecycle gate or A35 ticket-activation reader.
exec(compile(source,str(parent),'exec'))
if a.physical:
 for item in phases:
  drive=[r for r in rows if r['phase']==item['phase'] and r['gas'] and r['offset']%128 not in (64,65)]
  bins=[[r for r in drive if z<=r['z']<z+20] for z in range(-60,100,20)]
  assert min(map(len,bins))>=5
  item['spatialEntryHz']=[len(group)*294912000/sum(r['period'] for r in group) for group in bins]
  item['spatialMeanWorkMs']=[statistics.mean((r['whole']-r['pacing'])/294912 for r in group) for group in bins]
def modeGate(text):
 records=[tuple(map(int,r)) for r in re.findall(r'\bTRIPLEMODE phase=(\d+) requested=(\d+) actual=(\d+)',text)]
 assert records==[(p,int(p%3==1),manifest['phases'][p]) for p in range(6)]
 held=[tuple(map(int,r)) for r in re.findall(r'\bTRIPLEHELD phase=(\d+) arm=(\d+) actual=(\d+)',text)]
 assert held==[(p,arm,manifest['held'][arm]) for p in range(6) for arm in range(3)]
 records=[tuple(map(int,r)) for r in re.findall(r'\bTRIPLEPRESENT phase=(\d+) rows=(\d+) dropped=(\d+)',text)]
 assert [r[0] for r in records]==list(range(6)) and all(n>120 and dropped==0 for _,n,dropped in records)
 return records
presentationRecords=modeGate(log)
for label,bad in [('missing-mode',log.replace('TRIPLEMODE phase=1','MISSINGTRIPLEMODE phase=1')),
 ('wrong-grant',log.replace('actual=3','actual=2')),('presentation-overflow',log.replace('dropped=0','dropped=1')),
 ('missing-held',log.replace('TRIPLEHELD phase=0 arm=1','MISSINGTRIPLEHELD phase=0 arm=1'))]:
 try:modeGate(bad)
 except AssertionError:negatives.append(label)
 else:raise AssertionError(label)
def eventGate(data,phase,n):
 assert len(data)==n and phases[phase]['driveFrames']<=n<=phases[phase]['driveFrames']+2
 assert all(set(r)=={'tick','slot','isr'} and all(0<=v<=0xffffffff for v in r.values()) for r in data)
 triple=manifest['phases'][phase]==3
 assert {r['slot'] for r in data}==({1,2} if triple else {1})
 assert all(r['isr']==int(triple) for r in data)
 intervals=[(s['tick']-r['tick'])&0xffffffff for r,s in zip(data,data[1:])]
 assert all(0<t<294912000 for t in intervals)
 return intervals
presentations=[];eventPins={}
for phase,n,dropped in presentationRecords:
 path=folder/f'triple-present-{phase}.csv';data=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(path.open())]
 eventGate(data,phase,n);eventPins[path.relative_to(root).as_posix()]=sha(path)
 intervals=eventGate(data,phase,n)[2:-2]
 item=dict(phase=phase,rows=n,first=data[0],last=data[-1],isr=phase%3==1)
 if a.physical:
  ms=[t/294912 for t in intervals]
  item.update(interiorIntervals=len(ms),registerHz=len(intervals)*294912000/sum(intervals),
   meanIntervalMs=statistics.mean(ms),p95IntervalMs=sorted(ms)[math.ceil(.95*len(ms))-1],
   maxIntervalMs=max(ms),lateIntervals=sum(t>25 for t in ms))
 presentations.append(item)
 if phase==1:
  for label,mutate in [('missing-present-event',lambda r:r.pop()),('not-ISR-present',lambda r:r[2].update(isr=0)),
   ('invalid-display-slot',lambda r:r[2].update(slot=0)),('zero-present-interval',lambda r:r[2].update(tick=r[1]['tick']))]:
   bad=copy.deepcopy(data);mutate(bad)
   try:eventGate(bad,phase,n)
   except AssertionError:negatives.append(label)
   else:raise AssertionError(label)
report=dict(valid=True,platform=platform,elfSha256=b['elfSha256'],sourceFiles=len(b['files']),phases=phases,
 presentations=presentations,images=images,contracts=contracts,negativeControls=negatives,eventPins=eventPins,
 parentReaderSha256=sha(parent),logSha256=sha(logpath),csvSha256=sha(folder/'drive-probe.csv'),
 originalHeadWriterSha256=sha(root/headWriter),rawHeadHashEqualityRequired=False,
 promotionAllowed=False,scope='Current Hybrid net moving throughput and DISPFB register hand-offs, not photons/pure EE cycles; initial rebuild excluded, steady VRAM/FINISH included')
if a.physical:
 contrasts=[]
 for start in (0,3):
  on=phases[start+1];before,after=phases[start],phases[start+2]
  pOn=presentations[start+1];pOff=[presentations[start],presentations[start+2]]
  row=dict(hour=12 if start==0 else 0,
   entryGainHz=[on['entryHz']-r['entryHz'] for r in (before,after)],
   registerGainHz=[pOn['registerHz']-r['registerHz'] for r in pOff],
   spatialEntryGainHz=[[v-c for v,c in zip(on['spatialEntryHz'],r['spatialEntryHz'])] for r in (before,after)],
   candidate=pOn,controls=pOff)
  row['netGate']=all(n>0 for n in row['entryGainHz']+row['registerGainHz']) and pOn['p95IntervalMs']<=max(r['p95IntervalMs'] for r in pOff)+.15
  row['stable60Gate']=pOn['registerHz']>=59 and pOn['lateIntervals']==0
  contrasts.append(row)
 report['contrasts']=contrasts;report['firstOrderGate']=all(r['netGate'] for r in contrasts)
output=a.output or root/f'triple-{platform}.json';output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('images','contracts')},indent=2))
