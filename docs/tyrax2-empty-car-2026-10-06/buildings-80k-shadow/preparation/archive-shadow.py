"""Parent-agent-only append archive. No device operations; no runtime inferred."""
from pathlib import Path
import argparse,json,hashlib,shutil
p=argparse.ArgumentParser();p.add_argument('--physical',action='store_true');a=p.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');out=repo/'docs/tyrax2-empty-car-2026-10-06';prep=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';assetsRoot=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-assets-v2';emu=lab/'tyrax2-closeout-emulator-two-cars-light-buildings-80k-shadow-v2';physical=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-shadow-v2';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
items=json.loads((out/'inventory.json').read_text(encoding='utf8'));assert len(items)==610,'Prior archive changed; root review required';index={e['path']:e for e in items};assert len(index)==len(items)
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
review=json.loads((prep/'root-runtime-review.json').read_text(encoding='utf8'));native=json.loads((prep/'native-proof.json').read_text(encoding='utf8'));assert review['actualELFSha256']==native['actualELFSha256'];assert native['sourceReviewSha256']==h(prep/'source-review.json')
for k in ('rootReviewed','emulatorCompleted5400','threeOwnedImagesReviewed','emulatorOwnedExitConfirmed','allSixColdBlobAccepted54','parent80133Retained'):assert review[k]is True,k
assert a.physical or not review.get('physicalCompleted5400'),'Completed physical evidence must be included'
protocol=json.loads((emu/'protocol-analysis.json').read_text(encoding='utf8'));assert protocol['status']=='PASS_COMPLETED_PRIVATE_NORMAL_RAVAGER_BLOB_80K_PROTOCOL';assert protocol['actualInputTargetMetAtSixColdFrames']is True
assert all(int(r['acceptedVertices'])==54 and int(r['owner'])==1 for phase in protocol['blobColdWitnesses'].values()for r in phase.values())
copies=[]
for name in ('README.md','source-review.json','native-proof.json','native-proof.py','bin-assets-source-pins.json','analyze-shadow.py','parser-controls-v2/proof.json','native-build.log','native-build-v2.log','root-emulator-review.json','root-runtime-review.json','parent-resave.stdout','parent-resave.stderr','parent-refresh-gen.stdout','parent-refresh-gen.stderr','shadow-resave.stdout','shadow-resave.stderr','shadow-refresh-gen.stdout','shadow-refresh-gen.stderr'):
 copies.append((prep/name,'buildings-80k-shadow/preparation/'+name))
for name in ('empty-car/inc/scene_data.hpp','empty-car/src/terrain_game.cpp','empty-car/src/gen/game_lighting.gen.cpp','empty-car/src/gen/scene_objects.gen.cpp','tyra/engine/inc/debug/empty_witness.hpp'):
 copies.append((prep/name,'buildings-80k-shadow/changed-source/'+name))
copies.append((prep/'empty-car/objects/emptyravager0001.json','buildings-80k-shadow/authored-source/emptyravager0001.json'))
for name in ('prepare-shadow.py','resume-shadow.py','repair-shadow-include.py','finish-shadow-prep.py','finalize-shadow-proof.py','test-shadow-controls-v2.py','archive-shadow.py'):
 copies.append((lab/name,'buildings-80k-shadow/preparation/'+name))
for name in ('prepare-shadow-assets-v2.py','review-shadow-assets-v2-root.py','reject-shadow-missing-resource.py','qualify-shadow-emulator-root.py','review-shadow-root.py','finish-shadow-physical.py'):
 copies.append((lab/name,'buildings-80k-shadow/runtime-assets-v2/'+name))
for name in ('README.md','bin-assets-source-pins.json','root-assets-review.json','root-emulator-review.json'):
 copies.append((assetsRoot/name,'buildings-80k-shadow/runtime-assets-v2/'+name))
copies.append((assetsRoot/'empty-car/bin/hud/flare-glow.png','buildings-80k-shadow/runtime-assets-v2/hud/flare-glow.png'))
originalEmu=lab/'tyrax2-closeout-emulator-two-cars-light-buildings-80k-shadow-v1';originalPhysical=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-shadow-v1'
assert json.loads((originalPhysical/'rejected-runtime.json').read_text(encoding='utf8'))['sourceLogSha256']==h(originalPhysical/'completed-stdout.raw')
assert json.loads((originalEmu/'rejected-runtime.json').read_text(encoding='utf8'))['sourceLogSha256']==h(originalEmu/'runtime/emulator.log')
for name in ('owned-launch.json','owned-stop.json','rejected-runtime.json','runtime/emulator.log','capture-start/frame.png','capture-drive/frame.png','capture-final/frame.png'):
 copies.append((originalEmu/name,'buildings-80k-shadow/rejected-emulator-v1/'+name))
for name in ('launch-inputs.json','owned-launch.json','rejected-runtime.json','completed-stdout.raw','stdout.utf8.log'):
 copies.append((originalPhysical/name,'buildings-80k-shadow/rejected-physical-v1/'+name))
for name in ('owned-launch.json','owned-stop.json','protocol-analysis.json','runtime/emulator.log','capture-start/frame.png','capture-start/observe-proof.json','capture-drive/frame.png','capture-drive/observe-proof.json','capture-final/frame.png','capture-final/observe-proof.json','capture-final/bin-closeout-samples.txt'):
 copies.append((emu/name,'buildings-80k-shadow/emulator/'+name))
assets=json.loads((prep/'bin-assets-source-pins.json').read_text(encoding='utf8'));references=[]
repaired=json.loads((assetsRoot/'bin-assets-source-pins.json').read_text(encoding='utf8'));assert repaired['actualELFSha256']==native['actualELFSha256'] and repaired['sourceReviewSha256']==h(prep/'source-review.json') and repaired['nativeProofSha256']==h(prep/'native-proof.json')
for path,digest in repaired['files'].items():assert h(assetsRoot/'empty-car/bin'/path)==digest,path
for path,digest in assets['files'].items():
 assert h(prep/'empty-car/bin'/path)==digest,path
 matches=[e['path']for e in items if e['sha256']==digest]
 # Parent asset closure is complete; reference existing bytes when available, otherwise hash-only.
 references.append({'runtimePath':path,'sha256':digest,'existingArchivePaths':matches,'representation':'prior immutable bytes'if matches else'parent retained asset hash only'})
assert assets['activeBlobTexture']=='vehicles/veh-ravager000001-shadow.png'
phaseNote='Physical measurement pending; emulator is not hardware pricing.'
if a.physical:
 pr=json.loads((physical/'physical-review.json').read_text(encoding='utf8'));pd=json.loads((physical/'protocol-analysis.json').read_text(encoding='utf8'));cadence=json.loads((physical/'root-cadence-review.json').read_text(encoding='utf8'));assert pd['status']=='PASS_COMPLETED_PRIVATE_NORMAL_RAVAGER_BLOB_80K_PROTOCOL';assert cadence['protocolPassed']is True and cadence['loops']==5400
 assert pr['rawSha256']==h(physical/'completed-stdout.raw')and pr['sampleSha256']==h(physical/'bin/closeout-samples.txt');assert review['physicalCompleted5400']is True
 for name in ('launch-inputs.json','owned-launch.json','completed-stdout.raw','stdout.utf8.log','physical-review.json','protocol-analysis.json','root-cadence-review.json','bin/closeout-samples.txt'):
  copies.append((physical/name,'buildings-80k-shadow/physical/'+name))
 copies.append((lab/'finish-shadow-physical.py','buildings-80k-shadow/preparation/finish-shadow-physical.py'))
 phases=[pd['byPhase'][str(i)]for i in range(3)];work='/'.join(f"{q['chunkMeanNonPacingMs']:.4f}"for q in phases);period='/'.join(f"{q['chunkMeanEntryMs']:.4f}"for q in phases)
 phaseNote=f'Fresh physical run completed5400 loops: nonpacing {work}ms, entry periods {period}ms. All six cold observations retain80133 parent inputs and accept one54-input blob, total80187. Archived stdout is the immutable completed prefix, not a terminal-run claim; the serving client may remain active. Common observer footprint remains unpriced and nonpacing is not pureEE time. TV validation status is recorded in the root runtime review rather than inferred from emulator images.'
assert len({rel for src,rel in copies})==len(copies)
refRel='buildings-80k-shadow/preparation/referenced-runtime-assets.json';assert not(out/refRel).exists()
for src,rel in copies:assert src.is_file()and not(out/rel).exists(),(src,rel)
for src,rel in copies:
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);items.append({'path':rel,'sha256':h(dst)})
ref=out/refRel;ref.write_bytes((json.dumps({'status':'VERIFIED_PARENT_ASSETS_RETAINED_PUBLIC_BLOB_MASK_ACTIVE','references':references,'blobTexture':assets['activeBlobTexture']},indent=2)+'\n').encode());items.append({'path':refRel,'sha256':h(ref)})
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
(out/'inventory.json').write_bytes((json.dumps(items,indent=2)+'\n').encode())
note='\n## Normal Ravager blob shadow at actual80187 inputs\n\nThe same actual80133-input corridor now adds only the driver Ravager normal blob shadow through public shadowMode2. castShadow remains false (its baked-AO meaning is separate), global blob preference remains false, Pica and all other owners remain unchanged. Normal cached and rebuilt shadow submissions use the public Ravager mask,TestOnly depth,precise/full clipping and no dynamic-light pick. Separate cold carrier56 prevents its54 inputs from being mislabeled as car or building geometry. Every cold observation must accept one54-input blob and retain the complete parent partition, total80187. Eight compact parser controls reject absent/zero/wrong-owner/wrong-size/invalid blobs, high-owner loss and partition mismatch; original parent controls remain archived. The initial native compilation failed solely because the observer header lacked an explicit include in the generated lighting translation unit; that failed log and corrected successful v2 build are retained as build provenance, not runtime regressions.\n\n'+phaseNote+'\n\nRoot reviewed three owned emulator captures with both cars and long facades retained, including a visible soft black shadow under Ravager. Pine and all50 owners are not independently distinguished by pixels; acceptance witnesses have their narrower meaning. Root confirmed exact owned emulator exit. This one-arm continuation is workload qualification, not causal shadow-only milliseconds subtraction or proof that an arbitrary night scene reaches60FPS. Normal shadow geometry refresh and ground sampling are included in the workload.\n'
note+='\nThe first shadow emulator and physical runs completed protocol loops but were rejected for missing hud/flare-glow.png: the normal blob startup also loads this fallback texture, even when the vehicle-specific mask loaded successfully. Neither rejected run is priced or qualified. Runtime-assets-v2 adds the exact public vehicle-playground glow resource and retains the identical source/native/ELF and every prior asset byte. Only the subsequent v2 runtime evidence above can qualify this continuation; original logs and rejection records remain archived.\n'
q=out/'README.md';q.write_bytes((q.read_text(encoding='utf8').rstrip()+'\n'+note).encode())
q=repo/'docs/tyrax2-closeout.md';q.write_bytes((q.read_text(encoding='utf8').rstrip()+'\n\nThe [normal Ravager blob continuation](tyrax2-empty-car-2026-10-06/README.md#normal-ravager-blob-shadow-at-actual80187-inputs) retains the parent80133 accepted inputs plus54 separately classified normal blob inputs. '+phaseNote+'\n').encode())
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
print('PASS append-only shadow archive;',len(items),'immutable pins; physical=',a.physical)
