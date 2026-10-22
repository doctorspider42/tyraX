"""Root-only append archive; no device operations. --physical requires completed immutable prefix."""
from pathlib import Path
import argparse,json,hashlib,shutil
p=argparse.ArgumentParser();p.add_argument('--physical',action='store_true');a=p.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');out=repo/'docs/tyrax2-empty-car-2026-10-06';prep=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-v1';emu=lab/'tyrax2-closeout-emulator-two-cars-light-buildings-80k-v1';failed=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-v1';physical=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-v2';reset=lab/'tyrax2-reset-without-usb-v1';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
items=json.loads((out/'inventory.json').read_text(encoding='utf8'));assert len(items)==503,'Prior baseline changed; root must independently review';index={e['path']:e for e in items};assert len(index)==len(items)
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
review=json.loads((prep/'root-runtime-review.json').read_text(encoding='utf8'));native=json.loads((prep/'native-proof.json').read_text(encoding='utf8'));assert review['actualELFSha256']==native['actualELFSha256'];assert native['sourceReviewSha256']==h(prep/'source-review.json')
for k in ('rootReviewed','emulatorCompleted5400','threeOwnedImagesReviewed','emulatorOwnedExitConfirmed','allFiftyBuildingOwnersAcceptedAtAllSixColdFrames','bothVehiclesAndMovingLightRetained'):assert review[k]is True,k
assert a.physical or not review.get('physicalCompleted5400'),'Physical is already complete; include --physical so docs cannot regress to pending'
protocol=json.loads((emu/'protocol-analysis.json').read_text(encoding='utf8'));assert protocol['status']=='PASS_COMPLETED_PRIVATE_TWO_CARS_LIGHT_PINE_FIFTY_BUILDINGS_80K_PROTOCOL';assert protocol['actualInputTargetMetAtSixColdFrames']is True
copies=[]
for name in ('README.md','source-review.json','source-content-census.json','native-proof.json','native-proof.py','building-assets-proof.json','bin-assets-source-pins.json','preparation-proof.json','analyze-80k.py','test-parser.py','parser-controls-v1/proof.json','run-mask-host-controls.py','mask-host-controls-v1/proof.json','mask-host-controls-v1/controls.cpp','mask-host-controls-v1/stub/debug/night_sampler.hpp','root-emulator-review.json','root-runtime-review.json'):
 copies.append((prep/name,'buildings-80k/preparation/'+name))
for name in ('empty-car/src/terrain_game.cpp','empty-car/src/gen/game_collision.gen.cpp','empty-car/src/gen/game_scene.gen.cpp','empty-car/src/gen/scene_objects.gen.cpp','empty-car/empty-car.tyra','tyra/engine/inc/debug/empty_witness.hpp'):
 copies.append((prep/name,'buildings-80k/changed-source/'+name))
for i in range(50):
 name=f'empty-car/objects/onlybuilding{i:03}.json';copies.append((prep/name,'buildings-80k/authored-source/'+name))
# Same public runtime assets: refer to existing immutable archive pins, never duplicate binaries.
assets=json.loads((prep/'building-assets-proof.json').read_text(encoding='utf8'));runtime=set()
for name,m in assets['models'].items():
 runtime.add('models/kenney/'+name+'.tmdl')
 for q in m['parts']:
  if q['texture']:runtime.add(q['texture'])
references=[]
for name in sorted(runtime):
 rel='buildings/runtime-assets/'+name;assert rel in index;assert h(prep/'empty-car/bin'/name)==index[rel]['sha256'];references.append({'runtimePath':name,'existingArchivePath':rel,'sha256':index[rel]['sha256']})
for name in ('owned-launch.json','owned-stop.json','protocol-analysis.json','runtime/emulator.log','capture-start/frame.png','capture-start/observe-proof.json','capture-drive/frame.png','capture-drive/observe-proof.json','capture-final/frame.png','capture-final/observe-proof.json','capture-final/bin-closeout-samples.txt'):
 copies.append((emu/name,'buildings-80k/emulator/'+name))
for name in ('launch-inputs.json','owned-launch.json','ps2client.stdout','ps2client.stderr','rejected-launch.json'):
 copies.append((failed/name,'buildings-80k/rejected-physical-v1/'+name))
for name in ('attempt.json','reset.stdout','reset.stderr'):copies.append((reset/name,'buildings-80k/reset-without-usb/'+name))
for name in ('review-buildings-80k-root.py','prepare-buildings-80k.py','finish-buildings-80k-physical.py','qualify-80k-physical-root.py','try-reset-without-usb.ps1','archive80k.py'):
 copies.append((lab/name,'buildings-80k/preparation/'+name))
phaseNote='Physical measurement pending; emulator is not hardware pricing.'
if a.physical:
 pr=json.loads((physical/'physical-review.json').read_text(encoding='utf8'));pd=json.loads((physical/'protocol-analysis.json').read_text(encoding='utf8'));cadence=json.loads((physical/'root-cadence-review.json').read_text(encoding='utf8'));assert pd['status']=='PASS_COMPLETED_PRIVATE_TWO_CARS_LIGHT_PINE_FIFTY_BUILDINGS_80K_PROTOCOL';assert cadence['protocolPassed']is True and cadence['loops']==5400
 assert pr['rawSha256']==h(physical/'completed-stdout.raw') and pr['sampleSha256']==h(physical/'bin/closeout-samples.txt');assert review['physicalCompleted5400']is True
 for name in ('launch-inputs.json','owned-launch.json','completed-stdout.raw','stdout.utf8.log','physical-review.json','protocol-analysis.json','root-cadence-review.json','bin/closeout-samples.txt'):
  copies.append((physical/name,'buildings-80k/physical-v2/'+name))
 phases=[pd['byPhase'][str(i)]for i in range(3)];work='/'.join(f"{q['chunkMeanNonPacingMs']:.4f}"for q in phases);period='/'.join(f"{q['chunkMeanEntryMs']:.4f}"for q in phases)
 phaseNote=f'Fresh physical v2 completed5400 loops: nonpacing {work}ms, entry periods {period}ms, about59.94Hz in these bounded windows. All six cold frames accept80133 input vertices and all50 owner refs. User confirmed the current80k TV image looks good; this does not retroactively qualify the earlier8-building TV. The serving client remains active; archived stdout is the immutable completed prefix, not a terminal-run claim. Common sampler/observer footprint remains unpriced and nonpacing is not pureEE time.'
# Whole preflight before mutation; no ELF/symbol/cache/control binary copied.
assert len({rel for src,rel in copies})==len(copies)
for src,rel in copies:assert src.is_file()and not(out/rel).exists(),(src,rel)
refRel='buildings-80k/preparation/referenced-runtime-assets.json';assert not(out/refRel).exists()
for src,rel in copies:
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);items.append({'path':rel,'sha256':h(dst)})
ref=out/refRel;ref.write_bytes((json.dumps({'status':'VERIFIED_PRIOR_IMMUTABLE_RUNTIME_ASSETS_IDENTICAL','references':references},indent=2)+'\n').encode());items.append({'path':refRel,'sha256':h(ref)})
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
(out/'inventory.json').write_bytes((json.dumps(items,indent=2)+'\n').encode())
note='\n## Fifty buildings and actual80133 accepted inputs\n\nThe same corridor now has25 north/west pairs at x-6.6/+6.6 and z12..204. Camera, short controller, road, both cars, moving point, pine and public static batching remain. Fifty checked64-bit owner refs and bounded512-carrier registration prevent high-bit loss;52 parser controls and actual-header host mask/capacity controls pass. Public building budget70800 was separately qualified as actual accepted input: all six emulator cold frames contain24 building carriers/70800 vertices and36 total carriers/80133 vertices. Owner refs mean shown membership represented in accepted carrier input, not50 independent pixel images or postclip triangles. Root reviewed3owned captures of both complete cars and long facades; pine is not separately distinguished visually. Exactowned emulator stop and PID absence were confirmed; Xvfb remains available.\n\n'+phaseNote+'\n\nThe first physical launch after one bounded standard network reset without USB had no freshload and was rejected, not measured. Reset stdout was empty; this is not evidence of a renderer failure or a fixed reset/USB cause. The later user physical restart allowed freshv2 loading. No triangle-only causal performance explanation or generic60fps guarantee follows from this ladder.\n'
q=out/'README.md';q.write_bytes((q.read_text(encoding='utf8').rstrip()+'\n'+note).encode())
q=repo/'docs/tyrax2-closeout.md';q.write_bytes((q.read_text(encoding='utf8').rstrip()+'\n\nThe [actual80133-input continuation](tyrax2-empty-car-2026-10-06/README.md#fifty-buildings-and-actual80133-accepted-inputs) qualifies50 building owners with checked64-bit masks,70800 accepted building vertices and80133 total at six cold frames. '+phaseNote+'\n').encode())
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
print('PASS append-only80k archive;',len(items),'immutable pins; physical=',a.physical)
