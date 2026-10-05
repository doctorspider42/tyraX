from pathlib import Path
import json,hashlib,subprocess
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');review=json.loads((b/'player-light-receivers-native-root-review-v2-retry1/proof.json').read_text());nm=Path('/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ee/bin/mips64r5900el-ps2-elf-nm');result={}
for name in ('game_physics','game_scene','game_vehicles'):
 objects=[Path(p)for p in review['objectPins']if p.endswith('/obj/gen/'+name+'.gen.o')];assert len(objects)==1;obj=objects[0];assert hashlib.sha256(obj.read_bytes()).hexdigest()==review['objectPins'][str(obj)]
 text=subprocess.check_output([str(nm),'-C','--defined-only',str(obj)],text=True)
 wanted=['TerrainGame::renderScene()','TerrainGame::renderVehicleWheels()','TerrainGame::renderVehicleGlass()','TerrainGame::fillDynLitColors(int)','TerrainGame::updateAnimObjects()']
 rows=[x for x in text.splitlines()if any(w in x for w in wanted)and ' T 'in x];result[name]=dict(object=str(obj),sha256=review['objectPins'][str(obj)],definedMethods=rows)
flat=[x for v in result.values()for x in v['definedMethods']]
for name in ('renderVehicleWheels()','renderVehicleGlass()','fillDynLitColors(int)'):assert sum(name in x for x in flat)==1,name
p=b/'player-receiver-symbol-paths-v2.json';assert not p.exists();p.write_text(json.dumps(dict(status='PASS_ACTUAL_NATIVE_RENDER_METHOD_OBJECT_PATHS',objects=result,nmSha256=hashlib.sha256(nm.read_bytes()).hexdigest()),indent=2)+'\n');print(flat)
