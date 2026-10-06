"""Root-executed append-only archive after bounded emulator qualification. No devices."""
from pathlib import Path
import argparse,hashlib,json,shutil
p=argparse.ArgumentParser();p.add_argument('--runtime-review',type=Path,required=True);p.add_argument('--emu',type=Path,default=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-emulator-two-cars-light-buildings-v1'));a=p.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');out=repo/'docs/tyrax2-empty-car-2026-10-06';prep=lab/'tyrax2-closeout-road-two-cars-light-buildings-v1';emu=a.emu;h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
items=json.loads((out/'inventory.json').read_text(encoding='utf8'));assert len(items)==244,'Prior immutable inventory changed; root must review new baseline rather than bypass this guard'
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
priorInventorySha256=h(out/'inventory.json')
review=json.loads(a.runtime_review.read_text(encoding='utf8'))
for key in ('rootReviewed','emulatorCompleted5400','threeOwnedImagesReviewed','emulatorOwnedExitConfirmed','allEightBuildingOwnersAcceptedAtAllSixColdFrames','bothVehiclesAndMovingLightRetained'):assert review.get(key)is True,key
assert review.get('hardwareTimingQualified')is False
assert review.get('physicalRunPending')is True
# Root review supplies actual owned-process/exit evidence; this script never invents PID absence.
protocol=json.loads((emu/'protocol-analysis.json').read_text(encoding='utf8'));assert protocol['status']=='PASS_COMPLETED_PRIVATE_TWO_CARS_LIGHT_PINE_EIGHT_BUILDINGS_PROTOCOL'
stop=json.loads((emu/'owned-stop.json').read_text(encoding='utf8'));launch=json.loads((emu/'owned-launch.json').read_text(encoding='utf8'))
assert isinstance(stop,dict)and isinstance(launch,dict)
native=json.loads((prep/'native-proof.json').read_text(encoding='utf8'));source=json.loads((prep/'source-review.json').read_text(encoding='utf8'));assert native['status']=='PASS_PRIVATE_TWO_CARS_LIGHT_PINE_BUILDINGS_NATIVE_SOURCE_OBJECT_LINK_BINDING';assert native['sourceReviewSha256']==h(prep/'source-review.json');assert review.get('actualELFSha256')==native['actualELFSha256']
controls=json.loads((prep/'parser-controls-v1/proof.json').read_text(encoding='utf8'));assert controls['caseCount']==48 and controls['syntheticOnly']is True
copies=[]
for name in ('README.md','source-review.json','source-content-census.json','native-proof.json','native-proof.py','building-assets-proof.json','bin-assets-source-pins.json','preparation-proof.json','analyze-buildings.py','test-parser.py','root-emulator-review.json'):
 copies.append((prep/name,'buildings/preparation/'+name))
copies.append((a.runtime_review,'buildings/preparation/root-runtime-review.json'))
for name in ('empty-car/src/terrain_game.cpp','empty-car/src/gen/game_scene.gen.cpp','empty-car/src/gen/game_collision.gen.cpp','empty-car/src/gen/scene_objects.gen.cpp','empty-car/inc/model_data.gen.hpp','empty-car/inc/scene_data.hpp','empty-car/empty-car.tyra','tyra/engine/inc/debug/empty_witness.hpp'):
 copies.append((prep/name,'buildings/changed-source/'+name))
for i in range(8):
 name=f'empty-car/objects/onlybuilding{i:03}.json';copies.append((prep/name,'buildings/authored-source/'+name))
assets=json.loads((prep/'building-assets-proof.json').read_text(encoding='utf8'));runtime=set()
for name,model in assets['models'].items():
 runtime.add('models/kenney/'+name+'.tmdl')
 for part in model['parts']:
  if part['texture']:runtime.add(part['texture'])
for name in sorted(runtime):copies.append((prep/'empty-car/bin'/name,'buildings/runtime-assets/'+name))
for name in ('facade-north.obj','facade-west.obj','kenney-shared.mtl'):
 copies.append((prep/'empty-car/res/models/kenney'/name,'buildings/authored-assets/'+name))
for f in sorted((prep/'parser-controls-v1').rglob('*')):
 if f.is_file():copies.append((f,'buildings/preparation/parser-controls-v1/'+f.relative_to(prep/'parser-controls-v1').as_posix()))
for name in ('owned-launch.json','owned-stop.json','protocol-analysis.json','runtime/emulator.log','capture-start/frame.png','capture-start/observe-proof.json','capture-drive/frame.png','capture-drive/observe-proof.json','capture-final/frame.png','capture-final/observe-proof.json','capture-final/bin-closeout-samples.txt'):
 copies.append((emu/name,'buildings/emulator/'+name))
for helper in ('review-buildings-root.py','prepare-light-buildings.py','archive-buildings-preparation.py','finish-buildings-physical.py'):
 copies.append((lab/helper,'buildings/preparation/'+helper))
# Preflight EVERY source and destination before the first repository mutation.
assert len({rel for src,rel in copies})==len(copies)
for src,rel in copies:assert src.is_file()and not(out/rel).exists(),(src,rel)
appendRows=[]
for src,rel in copies:
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);appendRows.append({'path':rel,'sha256':h(dst)})
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
items+=appendRows;(out/'inventory.json').write_text(json.dumps(items,indent=2)+'\n',encoding='utf8')
note="""
## Eight buildings on the same road

The next private sibling adds four public north and four west facades at x-6.6/+6.6, z12/20/28/36, yaw90, without collision, shadows, extra lights or effects. Both cars, the moving point, pine, short route and ordinary sampler remain. Normal static batching stays enabled. Actual public baked per-part budgets total11328 vertices/3776 triangles for these instances; this is a nominal imported budget, not an accepted-frame or postclip emitted count.

Native source/object/ELF/asset binding,48 synthetic parser controls and5400 emulator loops pass. All six cold frames contain accepted building carriers representing all eight shown owners, with exact road+cars+pine+building input totals. Shared carriers are counted once; owner refs prove accepted-carrier membership rather than independent pixel visibility. Both vehicle body/wheel light paths remain positive. Root reviewed three owned captures and confirmed the emulator stopped; image interpretation is bounded by the supplied runtime review (the initial shot's pine is not separately distinguishable).

Physical measurement awaits reset-ready console start. Emulator cadence is not PS2 pricing or a new60fps result. Common observer footprint remains unpriced; extra objects/materials/textures/batching belong to the workload, so a triangle-only causal explanation is not established.
"""
q=out/'README.md';q.write_text(q.read_text(encoding='utf8').rstrip()+'\n'+note,encoding='utf8')
q=repo/'docs/tyrax2-closeout.md';q.write_text(q.read_text(encoding='utf8').rstrip()+'\n\nThe [eight-building continuation](tyrax2-empty-car-2026-10-06/README.md#eight-buildings-on-the-same-road) retains both cars, moving point, pine and public static batching. Native binding,48 parser controls and5400 emulator loops qualify eight owner memberships in accepted carriers at all six cold frames; imported11328 vertices are separate from actual accepted counts. Physical measurement awaits reset-ready start; emulator is not hardware pricing.\n',encoding='utf8')
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
print('PASS append-only buildings preparation/emulator archive;',len(items),'immutable pins; prior',priorInventorySha256)
