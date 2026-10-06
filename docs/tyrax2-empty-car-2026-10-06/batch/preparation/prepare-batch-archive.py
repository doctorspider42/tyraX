"""Creates only a LAB staging plan; no repository mutation or devices."""
from pathlib import Path
import argparse,json,hashlib,shutil
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--completed-run',type=Path,action='append',default=[]);a=p.parse_args();assert not a.out.exists();lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');archive=repo/'docs/tyrax2-empty-car-2026-10-06';prep=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();prior=json.loads((archive/'inventory.json').read_text(encoding='utf8'));assert len(prior)==688
for q in prior:assert h(archive/q['path'])==q['sha256']
native=json.loads((prep/'native-proof.json').read_text());assert native['sourceReviewSha256']==h(prep/'source-review.json');assets=json.loads((prep/'bin-assets-source-pins.json').read_text());assert assets['nativeProofSha256']==h(prep/'native-proof.json')and assets['actualELFSha256']==native['actualELFSha256']
files=[]
for n in('README.md','source-review.json','native-proof.json','native-proof.py','bin-assets-source-pins.json','native-build.log','native-build-v2.log','native-build-v3.log','native-build-v4.log','native-build-v5.log','native-build-v6.log','analyze-batch.py','parser-controls-v5/proof.json','postboot-source-controls.json','sentinel-controls-v2/proof.json','sentinel-controls-v2/controls.cpp','sentinel-controls-v2/stub/debug/closeout.hpp','root-emulator-review.json','order0.cfg','order1.cfg'):
 files.append((prep/n,'batch/preparation/'+n))
for n in('empty-car/inc/batch_trial.hpp','empty-car/src/terrain_game.cpp','empty-car/src/gen/game_collision.gen.cpp'):files.append((prep/n,'batch/changed-source/'+n))
for n in('prepare-batch-shadow.py','repair-batch-membership.py','repair-batch-afterboot.py','repair-batch-sentinel.py','repair-batch-solo-residence.py','repair-batch-runtime-pools.py','finalize-batch-native-v4.py','test-batch-sentinel-v2.py','prepare-batch-parser.py','finalize-batch-parser.py','finish-batch-postboot-controls.py','finalize-batch-solo-parser.py','finalize-batch-pool-parser.py','test-batch-controls-v5.py','finalize-batch-assets.py','finish-batch-physical.py','analyze-batch-physical-root.py','prepare-batch-archive.py'):files.append((lab/n,'batch/preparation/'+n))
for revision in('v2','v3','v4'):
 for n in('native-proof.json','source-review.json','bin-assets-source-pins.json','empty-car/src/terrain_game.cpp','empty-car/inc/batch_trial.hpp'):
  files.append((prep/('rejected-native-'+revision)/n,'batch/rejected-native-'+revision+'/'+n))
files.append((prep/'rejected-native-v3/log-binding-clarification.json','batch/rejected-native-v3/log-binding-clarification.json'))
files.append((prep/'rejected-native-v4/empty-car/src/gen/game_collision.gen.cpp','batch/rejected-native-v4/empty-car/src/gen/game_collision.gen.cpp'))
for n in('native-proof.json','source-review.json','bin-assets-source-pins.json','empty-car/src/gen/game_collision.gen.cpp'):
 files.append((prep/'rejected-native-v5'/n,'batch/rejected-native-v5/'+n))
for revision in('v1','v2','v3','v4'):
 reject=lab/('tyrax2-closeout-emulator-two-cars-light-buildings-80k-shadow-batch-'+revision)
 for n in('owned-launch.json','owned-stop.json','rejected-runtime.json','runtime/emulator.log'):
  files.append((reject/n,'batch/rejected-emulator-'+revision+'/'+n))
for run in a.completed_run:
 d=json.loads((run/'protocol-analysis.json').read_text());assert d['status']=='PASS_COMPLETED_PRIVATE_STATIONARY_STATIC_BATCH_CONTRAST_PROTOCOL'
 for n in('protocol-analysis.json','physical-review.json','root-cadence-review.json','completed-stdout.raw','stdout.utf8.log','launch-inputs.json','owned-launch.json','bin/closeout-samples.txt'):
  files.append((run/n,'batch/'+run.name+'/'+n))
for src,dest in files:assert src.is_file(),src
assert len({d for s,d in files})==len(files);a.out.mkdir(parents=True)
records=[]
for src,dest in files:
 q=a.out/dest;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,q);records.append({'path':dest,'sha256':h(q),'originalSource':str(src)})
(a.out/'staging-plan.json').write_bytes((json.dumps({'status':'LAB_ONLY_STAGE_NOT_REPOSITORY_ARCHIVE','priorImmutablePins':688,'priorInventorySha256':h(archive/'inventory.json'),'records':records,'binaryPolicy':'No ELF,symbols,build caches or all synthetic log cases copied; asset postimages referenced by prior immutable pins.','pendingEvidence':'Completed owned emulator logs/protocol,ON/OFF captures,ownership stop/root review plus both physical order runs and explicit image authority remain root responsibility unless supplied.','interpretation':'Stationary representation comparison with matched sparse contexts,not an exact shader pixel oracle or pure packet price.'},indent=2)+'\n').encode());print('LAB staging plan ready',a.out)
