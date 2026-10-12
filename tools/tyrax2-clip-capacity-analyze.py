"""A24 moving qualification; no-clip is destructive attribution, never a fix."""
import argparse,copy,csv,hashlib,json,math,re,statistics,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true')
p.add_argument('--archive',action='store_true');p.add_argument('--output',type=Path)
a=p.parse_args();root=a.fixture
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
if a.archive:
 assert a.output and not a.output.resolve().is_relative_to(root.resolve())
 for rel,h in json.loads((root/'archive-sha256.json').read_text()).items():assert sha(root/rel)==h,rel
binding=json.loads((root/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert sha(root/rel)==h,rel
plan=json.loads((root/'clip-capacity-manifest.json').read_text())
group_only=plan.get('groupOnly',False);count=6 if group_only else 10;half=count//2
assert plan['modes']==([0,7,0]*2 if group_only else [0,7,0,8,0]*2) and plan['hours']==[12]*half+[0]*half
for name,flag in [('hardware_trace.hpp','TYRA_HARDWARE_TRACE'),('frame_profile.hpp','TYRA_FRAME_PROFILE')]:
 assert re.search(r'^#define '+flag+r' 0$',(root/'tyra/engine/inc/debug'/name).read_text(),re.M)
core=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text()
assert 'fullClipChecks=bag->info->fullClipChecks && ClipTrial::mode!=8' in core
assert 'const bool wide=ClipTrial::mode==7 && capacityClass' in core
assert 'ClipTrial::footprint(pkg.clipPlaneMask,n*3)+91' in core
assert ('A24 unrefined clip overflow' in core)==plan.get('unrefined',False)
qbuffer=root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp'
assert sha(qbuffer)=='ba059bd60f30728a7ebb0e96ca2e370676a9f26f0c278b379b0a02f130298eab'
probe=(root/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
assert f'inline constexpr U phaseCount={count};' in probe and 'FastJoin::active=false;' in probe
assert 'inline bool freezeCapture() { return false; }' in probe and 'inline bool skipBodyEnv(){return false;}' in probe
physics=(root/'game/src/gen/game_physics.gen.cpp').read_text()
assert 'false && stapip.core.supportsPaintVu(part.envBag.get())' in physics
platform='physical' if a.physical else 'emulator'
folder=root/('physical/bin' if a.physical else 'game/bin')
logpath=root/('physical/console.log' if a.physical else 'emulator/emulator.log')
launch=json.loads((root/('physical/editor-deploy.json' if a.physical else 'emulator/launch.json')).read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==binding['elfSha256']==launch['elfSha256']
if a.physical:
 assert launch['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
 omitted=json.loads((root/'physical-bin-hashes.json').read_text()) if a.archive else {}
 for rel,h in json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig')).items():
  assert (sha(folder/rel) if (folder/rel).exists() else omitted.get(rel))==h,rel
text=logpath.read_text(errors='replace')
rows=[{k:float(v) for k,v in row.items()} for row in csv.DictReader((folder/'drive-probe.csv').open())]
def validate(data,log):
 assert 'DRIVEDONE valid=1' in log
 assert not any(s in log for s in ('Assertion failed','std::bad_alloc','terminate called','DMA Busy'))
 assert sorted(set(r['phase'] for r in data))==list(range(count))
 witnesses=[tuple(map(int,r)) for r in re.findall(r'CLIPWITNESS phase=(\d+) frames=(\d+) clipCalls=(\d+) eligible=(\d+) merged=(\d+) removed=(\d+) maxInput=(\d+) bypassBags=(\d+)',log)]
 checkpoints=re.findall(r'CLIPCHECKPOINT phase=(\d+) target=(\d+) z=([^ ]+) speed=([^ ]+) arms=5',log)
 assert len(witnesses)==len(checkpoints)==count
 assert [r[0] for r in witnesses]==[int(r[0]) for r in checkpoints]==list(range(count))
 result=[]
 for phase in range(count):
  mode=plan['modes'][phase];hour=plan['hours'][phase]
  group=[r for r in data if r['phase']==phase];n=len(group)
  assert 120<n<=1200 and [r['offset'] for r in group]==list(range(1200,1200+n))
  for r in group:
   assert all(math.isfinite(v) and (v>=0 or k in ('x','y','z','yaw','eyeX','eyeY','eyeZ','lookX','lookY','lookZ')) for k,v in r.items())
   assert r['vehicles']==3 and r['driver']>=0 and r['observed']==0 and r['lifetime']==250
   assert r['gas']==(r['offset']>=1320) and r['flips']==1
   assert r['whole']==r['update']+r['render'] and r['pacing']<=r['whole'] and r['period']>0
   assert all(r[k]==0 for k in ('captureFrozen','captureBlocked','envSkipped','paintBlocked','paintVu','coreTicks','partialTicks','scopeErrors'))
   assert abs(r['x'])<1 and abs(r['yaw'])<1
  park,drive=group[:120],group[120:]
  assert all(abs(r['speed'])<.001 for r in park) and abs(group[0]['z']+74.303497)<.1
  assert drive[-1]['z']>=108 and all(r['z']<108 for r in drive[:-1])
  assert all(b['z']>=r['z'] for r,b in zip(group,group[1:])) and drive[-1]['z']-group[0]['z']>182
  angles=[math.degrees(math.atan2(r['lookX']-r['eyeX'],r['lookZ']-r['eyeZ'])) for r in drive]
  assert min(angles)<-50 and max(angles)>50
  assert all(sum(r[k] for r in drive)>0 for k in ('envDrawn','paint','probeCapture'))
  assert re.search(rf'NIGHTROADS phase={phase} hour={hour}\.000000 paused=1 chunks=66 lights='+('0' if hour else '[1-9][0-9]*')+r'\b',log)
  assert re.search(rf'TURNPINS phase={phase} count=18\b',log)
  assert re.search(rf'DRIVEPHASE phase={phase} .*valid=1 rows={n} .*variant={mode}\b',log)
  _,frames,clips,eligible,merged,removed,max_input,bypass=witnesses[phase]
  assert frames==sum(r['offset']%128==64 for r in drive)>0
  if mode==8:assert clips==eligible==merged==removed==max_input==0 and bypass>0
  else:
   assert clips>0 and bypass==0 and 0<=merged<=eligible
   if mode==0:assert merged==removed==max_input==0
   elif merged:assert removed>=merged and 12<max_input<=39 and max_input%3==0
  assert float(checkpoints[phase][2])==drive[-1]['z'] and float(checkpoints[phase][3])==drive[-1]['speed']
  priced=[r for r in drive if r['offset']%128 not in (64,65)]
  bins=[[r for r in priced if lo<=r['z']<lo+20] for lo in range(-60,100,20)];assert min(map(len,bins))>=5
  item=dict(phase=phase,mode=mode,hour=hour,rows=n,pricedFrames=len(priced),lastZ=drive[-1]['z'],
            witnessFrames=frames,clipCalls=clips,eligible=eligible,merged=merged,removed=removed,maxInput=max_input,bypassBags=bypass)
  if a.physical:
   work=lambda r:(r['whole']-r['pacing'])/294912
   times=list(map(work,priced))
   item.update(meanWorkMs=statistics.mean(times),p95WorkMs=sorted(times)[math.ceil(.95*len(times))-1],maxWorkMs=max(times),
               entryHz=len(priced)*294912000/sum(r['period'] for r in priced),spatialMeanWorkMs=[statistics.mean(map(work,b)) for b in bins])
  result.append(item)
 return result
phases=validate(rows,text)
negatives=[]
for name,key,value in [('stationary','z',-74.303497),('no-gas','gas',0),('freeze','captureFrozen',1),('cars','vehicles',1),('profiler','coreTicks',1),('paint','paintVu',1),('present','flips',0)]:
 data=copy.deepcopy(rows)
 for row in data:
  if row['gas']:row[key]=value
 try:validate(data,text)
 except AssertionError:negatives.append(name)
 else:raise AssertionError('Negative escaped: '+name)
for name,data,log in [('missing-phase',rows[:-1],text),('completion',rows,text.replace('DRIVEDONE valid=1','DRIVEDONE valid=0')),
 ('witness',rows,re.sub(r'CLIPWITNESS[^\r\n]+','',text)),('checkpoint',rows,text.replace('CLIPCHECKPOINT phase=4','LOSTCHECKPOINT phase=4'))]+(
 [] if group_only else [('bypass-clip',rows,re.sub(r'CLIPWITNESS phase=3 frames=(\d+) clipCalls=0',r'CLIPWITNESS phase=3 frames=\1 clipCalls=1',text))]):
 try:validate(data,log)
 except AssertionError:negatives.append(name)
 else:raise AssertionError('Negative escaped: '+name)
images=[]
for phase in range(count):
 for view in ('main','env'):
  pixels=[];dimensions=None
  for arm in range(5):
   stage=phase*5+arm;raw=(folder/f'tape-{stage}-{view}.bin').read_bytes()
   w,h,psm,actual=struct.unpack('<4I',raw[:16]);assert actual==stage and psm in (0,2) and w>0 and h>0
   assert len(raw)==16+w*h*(2 if psm==2 else 4) and len(set(raw[16:]))>8
   if dimensions is None:dimensions=(w,h,psm)
   assert dimensions==(w,h,psm);pixels.append(raw[16:])
  differences=[sum(a!=b for a,b in zip(pixels[0],image)) for image in pixels]
  assert all(differences[i]==0 for i in (0,1,2,4)),(phase,view,differences)
  images.append(dict(phase=phase,view=view,dimensions=dimensions,differingBytes=differences))
report=dict(valid=True,groupRasterEqual=True,platform=platform,elfSha256=binding['elfSha256'],sourceFiles=len(binding['files']),phases=phases,
 unrefined=plan.get('unrefined',False),groupOnly=group_only,
 images=images,negativeControls=negatives,logSha256=sha(logpath),csvSha256=sha(folder/'drive-probe.csv'),
 groupActive=any(r['merged']>0 for r in phases),scope='8 omits necessary clipping and changes workload/appearance; net contrast is not pure EE or a production fix')
if a.physical:
 contrasts=[]
 for base in (0,half):
  for offset,mode in (((1,7),) if group_only else ((1,7),(3,8))):
   on=phases[base+offset];off=[phases[base+offset-1],phases[base+offset+1]]
   contrasts.append(dict(hour=on['hour'],mode=mode,savedMeanMs=statistics.mean(r['meanWorkMs'] for r in off)-on['meanWorkMs'],
    savedSpatialMs=statistics.mean(statistics.mean(r['spatialMeanWorkMs']) for r in off)-statistics.mean(on['spatialMeanWorkMs']),
    candidateP95Ms=on['p95WorkMs'],maxControlP95Ms=max(r['p95WorkMs'] for r in off)))
 report['contrasts']=contrasts
output=a.output or root/f'clip-capacity-{platform}.json';output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='images'},indent=2))
