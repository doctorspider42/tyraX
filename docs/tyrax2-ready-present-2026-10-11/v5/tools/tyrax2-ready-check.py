"""A39 independent activation + inherited complete travel/raster/presentation gates."""
import json,re
from pathlib import Path
parent=Path(__file__).with_name('tyrax2-triple-check.py')
a38Path=parent
source=parent.read_text();source=source[:source.index('\noutput=a.output or root/')]
def once(old,new):
 global source
 assert source.count(old)==1,old
 source=source.replace(old,new)
once("manifest['phases'] in ([2,3,2,2,3,2],[3,2,3,3,2,3]) and manifest['held']==[2,3,2]",
     "manifest['phases']==[3]*6 and manifest['held']==[3]*3")
once("reverse=manifest['phases'][0]==3","reverse=False")
once("condition='MotionProbe::phase%3!=1' if reverse else 'MotionProbe::phase%3==1'","condition='true'")
exec(compile(source,str(parent),'exec'))
ready=json.loads((root/'ready-manifest.json').read_text())
assert ready['experiment']=='A39' and ready['modes']==[0,1,0,0,1,0] and ready['held']==[0,1,0]
binding=json.loads((root/'a38-native-binding.json').read_text());assert ready['parentElf']==binding['elfSha256']
queue=(root/'tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp').read_text()
assert 'ReadyRecordingSuspend suspended(frameRecording,protectRecording,path3FencePending,nativePendingSequence);' in queue
assert 'advanceIfIdle();\n  if(static_cast<s32>(completed-sequence)<0)return false;' in queue
assert '!gs.readyHybridPresent() || !sync.check()' in core and 'gs.isRasterRedirected()' in core
assert 'rendererCore->tryReadyPresent();' in (root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text()
def readyGate(text):
 records=[tuple(map(int,r)) for r in re.findall(r'\bREADYWITNESS phase=(\d+) kind=(\d+) arm=(\d+) mode=(\d+) checks=(\d+) presented=(\d+) protected=(\d+)',text)]
 assert [(p,k,arm) for p,k,arm,*_ in records]==[(p,k,arm) for p in range(6) for k,arm in [(0,0),(1,0),(1,1),(1,2)]]
 for p,k,arm,mode,checks,presents,protected in records:
  assert mode==(ready['held'][arm] if k else ready['modes'][p])
  assert 0<=protected<=presents<=checks
  if not mode:assert checks==presents==protected==0
  elif k or a.physical:assert protected>0
  else:assert checks>0
 return records
activation=readyGate(log)
for label,bad in [('missing-ready-witness',log.replace('READYWITNESS phase=1 kind=0','MISSINGREADY phase=1 kind=0')),
 ('wrong-ready-mode',log.replace('mode=1 checks=','mode=0 checks=')),
 ('inactive-ready',re.sub(r'(READYWITNESS phase=1 kind=1 arm=1 mode=1 checks=\d+ presented=)\d+( protected=)\d+',r'\g<1>0\g<2>0',log))]:
 try:readyGate(bad)
 except AssertionError:negatives.append(label)
 else:raise AssertionError(label)
report.update(experiment='A39',readyRecords=activation,readyManifestSha256=sha(root/'ready-manifest.json'),
 a38ReaderSha256=sha(a38Path),negativeControls=negatives,
 scope='Protected ready old-job publication with Hybrid3 BOTH arms; preserved source/raster/route, not pure EE cycles')
output=a.output or root/f'ready-{platform}.json';output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(valid=True,platform=platform,images=len(images),controls=len(negatives),activation=activation,
 contrasts=report.get('contrasts')),indent=2))
