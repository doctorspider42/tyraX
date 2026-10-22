from pathlib import Path
import json,difflib,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';parent=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-v1'
p=r/'native-proof.py';s=p.read_text(encoding='utf8').replace("'EmptyWitness::buildingCount'):","'EmptyWitness::buildingCount','EmptyWitness::blobBags','EmptyWitness::blobVertices'):").replace('PASS_PRIVATE_TWO_CARS_LIGHT_PINE_FIFTY_BUILDINGS_80K_NATIVE_SOURCE_OBJECT_LINK_BINDING','PASS_PRIVATE_NORMAL_RAVAGER_BLOB_80K_NATIVE_SOURCE_OBJECT_LINK_BINDING');p.write_bytes(s.encode())
p=r/'analyze-shadow.py';s=p.read_text(encoding='utf8').replace('PASS_COMPLETED_PRIVATE_TWO_CARS_LIGHT_PINE_FIFTY_BUILDINGS_80K_PROTOCOL','PASS_COMPLETED_PRIVATE_NORMAL_RAVAGER_BLOB_80K_PROTOCOL').replace('renderColdWitnesses=renderWitnesses,','blobColdWitnesses=blobWitnesses,renderColdWitnesses=renderWitnesses,');p.write_bytes(s.encode())
p=r/'source-review.json';d=json.loads(p.read_text());proof=[]
for name in('inc/scene_data.hpp','inc/terrain_heights.gen.hpp','src/main.cpp','src/terrain_game.cpp','src/gen/game_vehicles.gen.cpp','src/gen/game_collision.gen.cpp','src/gen/game_scene.gen.cpp'):
 a=(r/'parent-generated-baseline'/name).read_text(encoding='utf8').splitlines(keepends=True);b=(parent/'empty-car'/name).read_text(encoding='utf8').splitlines(keepends=True);proof.append({'file':name,'diagnosticGroups':len(list(difflib.SequenceMatcher(None,a,b,autojunk=False).get_grouped_opcodes(3)))})
d['normalGeneratorDeltaReplay']=proof;p.write_bytes((json.dumps(d,indent=2)+'\n').encode())
print('PROOF_READY')
