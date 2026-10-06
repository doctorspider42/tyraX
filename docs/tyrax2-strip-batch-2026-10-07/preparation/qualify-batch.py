from pathlib import Path
import json,hashlib,re
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'strip-batch-trial-v2';run=lab/'tyrax2-closeout-ps2-strip-batch-v2';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=json.loads((r/'native-proof.json').read_text());review=json.loads((r/'root-review.json').read_text());launch=json.loads((run/'launch-inputs.json').read_text());result=json.loads((run/'strict-result.json').read_text())
assert native['status']=='PASS_STRIP_BATCH_NATIVE_SOURCE_OBJECT_BINDING'and result['status']=='PASS_STRIP_BATCH_STATIONARY_COMPARISON'
assert native['sourceReviewSha256']==sha(r/'source-review.json')
def local(p):
 if p.startswith('/mnt/'):return Path(p[5].upper()+':/'+p[7:])
 if p.startswith('/home/'):return Path('//wsl.localhost/Ubuntu'+p)
 return Path(p)
for rel,v in native['sources'].items():
 f=r/'vehicle/game'/rel[5:]if rel.startswith('game/')else r/rel
 assert sha(f)==v['sha256']and sha(local(v['actualMirror']))==v['sha256'],rel
for rel,v in native['objects'].items():assert sha(local(v['actualObject']))==v['sha256'],rel
for x in review['inputs']:assert sha(local(x['path']))==x['sha256']
sourcebin=r/'vehicle/game/bin'
for x in launch['files']:
 f=run/'bin'/x['name'];assert sha(f)==x['sha256']
 if x['name'] not in('ps2link.run','closeout.cfg'):assert sha(sourcebin/x['name'])==x['sha256']
assert sha(run/'bin/vehicle-playground.elf')==native['elfSha256']
raw=(run/'completed.stdout').read_bytes();assert b'loadelf: fname host:vehicle-playground.elf secname all'in raw
normalized=raw.replace(b'\r\n',b'\n').replace(b'\r',b'').decode('latin1');assert normalized.encode()==(run/'completed.log').read_bytes()
lines=normalized[normalized.index('LOG: STRIPPACK'):].splitlines()
assert all(not line or re.fullmatch(r'LOG: (?:STRIPPACK|CLOSEOUTCONTEXT|CLOSEOUTPHASE|CLOSEOUTDONE) .*',line)for line in lines),'unexpected timed-prefix log family'
assert all(abs(x['renderedHz']-59.94)<.01 for x in result['arms'])
proof=dict(status='PASS_ROOT_NATIVE_EMULATOR_PHYSICAL_STRIP_BATCH_COMPARISON',elfSha256=native['elfSha256'],nativeProofSha256=sha(r/'native-proof.json'),rootReviewSha256=sha(r/'root-review.json'),launchInputsSha256=sha(run/'launch-inputs.json'),rawPrefixSha256=sha(run/'completed.stdout'),normalizedLogSha256=sha(run/'completed.log'),sampleSha256=sha(run/'completed-samples.txt'),strictResultSha256=sha(run/'strict-result.json'),sourceHead='28628d8d7',physicalLoops=5400,rawRows=384,coarseLoopsPerArm=320,sourceAndObjectMirrorsVerified=True,normalization='Bounded raw bytes through completion retained. Latin1 byte-to-codepoint decoding preserves non-UTF8 loader bytes; only CR transport removed before UTF8 normalized log. Typed diagnostic rows are ASCII.',commonObserverCostUnpriced=True,productionOptimizationShipped=False,physicalImageQualified=False,physicalAudioQualified=False,limitations=result['limitations'])
(run/'root-qualification.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status'])
