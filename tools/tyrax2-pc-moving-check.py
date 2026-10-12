"""A37 independent moving/held/IRQ gate. Histograms are not exclusive EE cycles."""
import argparse,collections,copy,csv,hashlib,json,math,re,statistics,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true');p.add_argument('--output',type=Path)
p.add_argument('--archive',action='store_true')
a=p.parse_args();root=a.fixture;platform='physical' if a.physical else 'emulator'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
if a.archive:
 assert a.output and not a.output.resolve().is_relative_to(root.resolve())
 for rel,digest in json.loads((root/'archive-sha256.json').read_text()).items():assert sha(root/rel)==digest,rel
b=json.loads((root/'native-binding.json').read_text());manifest=json.loads((root/'pc-moving-manifest.json').read_text())
for rel,digest in b['files'].items():assert sha(root/rel)==digest,rel
folder=root/('physical/bin' if a.physical else 'game/bin')
logpath=root/('physical/console.log' if a.physical else 'emulator/emulator.log');log=logpath.read_text(errors='replace')
launch=json.loads((root/('physical/editor-deploy.json' if a.physical else 'emulator/launch.json')).read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==b['elfSha256']==launch['elfSha256']
if a.physical:
 assert launch['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
 omitted=json.loads((root/'omitted-physical-inputs.json').read_text()) if a.archive else {}
 for rel,digest in json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig')).items():
  assert (sha(folder/rel) if (folder/rel).exists() else omitted.get(rel))==digest,rel
sdk=root/'sdk' if a.archive else Path('C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2sdk')
for rel,digest in manifest['sdkPins'].items():assert sha(sdk/rel)==digest,rel
calibration=root/'calibration-evidence' if a.archive else Path(__file__).resolve().parents[1]/'docs/tyrax2-pc-sampling-2026-10-11/calibration-v1'
cal=json.loads((calibration/'pc-calibration-physical.json').read_text())
assert cal['valid'] and cal['elfSha256']==manifest['calibrationElf']
assert json.loads((calibration/'pc-calibration-manifest.json').read_text())['sdkPins']==manifest['sdkPins']
assert not re.search(r'^.* [TW] _+mcount$',(root/'symbols.txt').read_text(),re.M)
rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader((folder/'drive-probe.csv').open())]
def travel(data,text):
 assert 'DRIVEDONE valid=1' in text
 assert not any(s in text for s in ('Assertion failed','std::bad_alloc','terminate called','DMA Busy'))
 assert sorted(set(r['phase'] for r in data))==list(range(6))
 result=[]
 for phase in range(6):
  group=[r for r in data if r['phase']==phase];n=len(group)
  assert 120<n<=1200 and [r['offset'] for r in group]==list(range(1200,1200+n))
  for r in group:
   assert all(math.isfinite(v) for v in r.values())
   assert r['vehicles']==3 and r['driver']>=0 and r['observed']==0 and r['lifetime']==250
   assert r['gas']==(r['offset']>=1320) and r['flips']==1 and r['whole']==r['update']+r['render']
   assert 0<=r['pacing']<=r['whole'] and r['period']>0 and abs(r['x'])<1 and abs(r['yaw'])<1
   assert all(r[k]==0 for k in ('captureFrozen','captureBlocked','envSkipped','paintBlocked','paintVu','coreTicks','partialTicks','scopeErrors'))
  park,drive=group[:120],group[120:]
  assert all(abs(r['speed'])<.001 for r in park) and abs(group[0]['z']+74.303497)<.1
  assert drive[-1]['z']>=108 and all(r['z']<108 for r in drive[:-1])
  assert all(r['z']<=s['z'] for r,s in zip(group,group[1:]))
  angles=[math.degrees(math.atan2(r['lookX']-r['eyeX'],r['lookZ']-r['eyeZ'])) for r in drive]
  assert min(angles)<-50 and max(angles)>50
  assert all(sum(r[k] for r in drive)>0 for k in ('envDrawn','paint','probeCapture'))
  hour=12 if phase<3 else 0
  assert re.search(rf'\bNIGHTROADS phase={phase} hour={hour}\.000000 paused=1 chunks=66 lights='+('0' if hour else '[1-9][0-9]*')+r'\b',text)
  assert re.search(rf'\bTURNPINS phase={phase} count=18\b',text)
  assert re.search(rf'\bDRIVEPHASE phase={phase} .*valid=1 rows={n} .*variant=0\b',text)
  checkpoint=re.search(rf'\bCLIPCHECKPOINT phase={phase} target=\d+ z=([^ ]+) speed=([^ ]+) arms=3\b',text)
  assert checkpoint and float(checkpoint[1])==drive[-1]['z'] and float(checkpoint[2])==drive[-1]['speed']
  witness=re.search(rf'\bWORLDWITNESS phase={phase} mode=0 frames=(\d+) draws=(\d+) qwords=(\d+)',text)
  assert witness and int(witness[1])>0 and int(witness[2])>0 and int(witness[3])>=4*int(witness[2])
  assert re.search(rf'\bFRONTWITNESS phase={phase} mode=0 frames=\d+ inside=0 partial=0 outside=0 live=0 ',text)
  item=dict(phase=phase,periodUs=manifest['periods'][phase],rows=n,driveFrames=len(drive),lastOffset=int(group[-1]['offset']),lastZ=group[-1]['z'])
  if a.physical:
   priced=[r for r in drive if r['offset']%128 not in (64,65)]
   work=[(r['whole']-r['pacing'])/294912 for r in priced]
   item.update(meanWorkMs=statistics.mean(work),p95WorkMs=sorted(work)[math.ceil(.95*len(work))-1],entryHz=len(priced)*294912000/sum(r['period'] for r in priced))
  result.append(item)
 return result
phases=travel(rows,log);negatives=[]
for label,key,value in [('cars','vehicles',1),('frozen','captureFrozen',1),('no-gas','gas',0),('profiler','coreTicks',1),('stationary','z',-74.303497)]:
 bad=copy.deepcopy(rows)
 for r in bad:
  if r['gas']:r[key]=value
 try:travel(bad,log)
 except AssertionError:negatives.append(label)
 else:raise AssertionError(label)
try:travel(rows,log.replace('DRIVEDONE valid=1','DRIVEDONE valid=0'))
except AssertionError:negatives.append('completion')
else:raise AssertionError('completion')
contracts={}
for name,fields in [('WORLDOWNER','sources draws triangles hash'),('CONSUMERINPUT','calls verts hash'),('HEADERORACLE','checks'),
 ('COARSESOURCE','packages triangles vertices hash'),('COARSEDELIVERY','jobs triangles vertices hash'),('COARSERESIDUAL','triangles vertices hash'),
 ('RINGCONTRACT','calls triangles'),('FRONTORACLE','inside partial outside heads words hash')]:
 pattern=r'\b'+name+r' phase=(\d+) arm=(\d+)'+''.join(' '+k+r'=([0-9a-f]+)' for k in fields.split())
 parsed=re.findall(pattern,log);assert len(parsed)==18,name
 assert [(int(r[0]),int(r[1])) for r in parsed]==[(p,i) for p in range(6) for i in range(3)],name
 for phase in range(6):
  group=parsed[phase*3:phase*3+3];assert all(r[2:]==group[0][2:] for r in group),name
  if name=='WORLDOWNER':assert int(group[0][2])==int(group[0][3])>0 and int(group[0][4])>0
  if name=='HEADERORACLE':assert int(group[0][2])>0
  if name=='FRONTORACLE':assert group[0][2:5]==('0','0','0') and int(group[0][5])>0
 contracts[name]=parsed
for s,d in zip(contracts['COARSESOURCE'],contracts['COARSEDELIVERY']):assert s[3:]==d[3:]
images=[]
for phase in range(6):
 for view in ('main','env'):
  reference=None
  for arm in range(3):
   path=folder/f'tape-{phase*3+arm}-{view}.bin';raw=path.read_bytes()
   w,h,psm,stage=struct.unpack('<4I',raw[:16]);assert stage==phase*3+arm and 0<w<=1024 and 0<h<=1024 and psm in (0,2)
   assert len(raw)==16+w*h*(2 if psm==2 else 4) and len(set(raw[16:]))>8
   image=(w,h,psm,raw[16:])
   if reference is None:reference=image
   assert image==reference,(phase,arm,view)
   images.append(dict(phase=phase,arm=arm,view=view,sha256=sha(path)))
def recordGate(text):
 records=[tuple(map(int,r)) for r in re.findall(r'\bPCMOVING phase=(\d+) kind=(\d+) periodUs=(\d+) rows=(\d+) dropped=(\d+) afterStop=(\d+)',text)]
 assert [(r[0],r[1]) for r in records]==[(p,k) for p in range(6) for k in range(2)]
 for phase,kind,us,n,dropped,late in records:
  assert dropped==late==0 and us==(manifest['periods'][phase] if not kind else 1733 if phase%2 else 997)
  assert (n>=40) if us else n==0
 return records
records=recordGate(log)
for label,bad in [('missing-sampler',log.replace('PCMOVING phase=0 kind=0','MISSINGPCMOVING phase=0 kind=0')),
 ('sampler-overflow',log.replace('dropped=0','dropped=1')),('late-IRQ',log.replace('afterStop=0','afterStop=1')),
 ('wrong-period',log.replace('periodUs=997','periodUs=998'))]:
 try:recordGate(bad)
 except AssertionError:negatives.append(label)
 else:raise AssertionError(label)
def sampleGate(raw,phase,kind,n):
 assert len(raw)==n
 assert all(r['phase']==phase and r['actual']>=r['scheduled'] for r in raw)
 assert all(s['actual']>r['actual'] for r,s in zip(raw,raw[1:]))
 assert all((r['region']&15)==8 and (r['region']>>4)<32 if kind else
            (r['region']&15) in (0,1,2) and 1320<=(r['region']>>4)<=phases[phase]['lastOffset']+1 for r in raw)
samples={};samplePins={}
for phase,kind,us,n,dropped,late in records:
 assert dropped==late==0 and us==(manifest['periods'][phase] if not kind else 1733 if phase%2 else 997)
 path=folder/f'pc-{"held" if kind else "drive"}-{phase}.csv'
 raw=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(path.open())];sampleGate(raw,phase,kind,n)
 samples[f'{phase}-{kind}']=raw;samplePins[path.relative_to(root).as_posix()]=sha(path)
control=samples['0-0']
for label,mutate in [('missing-PC-row',lambda r:r.pop()),('wrong-PC-phase',lambda r:r[0].update(phase=1)),
 ('backwards-time',lambda r:r[1].update(actual=r[0]['actual'])),('IO-in-drive',lambda r:r[0].update(region=8)),
 ('warmup-in-drive',lambda r:r[0].update(region=1200<<4))]:
 bad=copy.deepcopy(control);mutate(bad)
 try:sampleGate(bad,0,0,len(control))
 except AssertionError:negatives.append(label)
 else:raise AssertionError(label)
report=dict(valid=True,platform=platform,elfSha256=b['elfSha256'],sourceFiles=len(b['files']),
 scope='Qualified raw PC residency only; IRQ masking/preemption bias, not exclusive cycles or60fps impossibility',
 phases=phases,images=images,contracts=contracts,sampleRecords=records,samplePins=samplePins,
 negativeControls=negatives,logSha256=sha(logpath),csvSha256=sha(folder/'drive-probe.csv'))
output=a.output or root/f'pc-moving-{platform}.json';output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(valid=True,platform=platform,images=len(images),samples=sum(r[3] for r in records),phases=phases),indent=2))
