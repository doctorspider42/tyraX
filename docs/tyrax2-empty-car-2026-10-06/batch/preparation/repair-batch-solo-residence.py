from pathlib import Path
import json,hashlib,shutil
r=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1');old=r/'rejected-native-v4';assert not old.exists();old.mkdir()
for n in('native-proof.json','source-review.json','bin-assets-source-pins.json','empty-car/bin/empty-car.elf','empty-car/bin/empty-car.elf.sym','empty-car/src/terrain_game.cpp','empty-car/src/gen/game_collision.gen.cpp','empty-car/inc/batch_trial.hpp'):
 q=old/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(r/n,q)
p=r/'empty-car/inc/batch_trial.hpp';s=p.read_text().replace('inline bool on=true,captured=false;','inline bool on=true,captured=false,soloPrepared=false;\ninline unsigned soloParts=0,soloVertices=0;');p.write_bytes(s.encode())
p=r/'empty-car/src/gen/game_collision.gen.cpp';s=p.read_text(encoding='utf8');needle='void TerrainGame::renderStaticBatches() {\n  if(!BatchTrial::on)return;';assert s.count(needle)==1
prep='''void TerrainGame::renderStaticBatches() {
  if(!BatchTrial::soloPrepared){
    if(!BatchTrial::captured||BatchTrial::originalMembership.size()!=runtimeObjects.size())NightSampler::valid=false;
    for(int owner=5;owner<=54;++owner){
      if(!BatchTrial::member(BatchTrial::originalMembership[owner])||runtimeObjects[owner].dirty)NightSampler::valid=false;
      if(objectGeometry[owner].parts.empty()&&!runtimeObjects[owner].dirty)rebuildObjectGeometry(owner);
      const ObjectGeometry& geometry=objectGeometry[owner];
      if(geometry.matrixMode||geometry.impostor||runtimeObjects[owner].onMatrixPath)NightSampler::valid=false;
      for(const GeoPart& part:geometry.parts){
        if(!part.bag||part.vertices.empty()){NightSampler::valid=false;continue;}
        ++BatchTrial::soloParts;BatchTrial::soloVertices+=part.bag->count;
      }
    }
    if(BatchTrial::soloParts!=300||BatchTrial::soloVertices!=70800)NightSampler::valid=false;
    BatchTrial::soloPrepared=true;
    printf("LOG: BATCHPREP owners=50 parts=%u vertices=%u matrix=0 impostor=0 valid=%u\\n",BatchTrial::soloParts,BatchTrial::soloVertices,unsigned(NightSampler::valid));fflush(stdout);
  }
  if(!BatchTrial::on)return;'''
s=s.replace(needle,prep,1);p.write_bytes(s.encode())
p=r/'source-review.json';d=json.loads(p.read_text());
for n in('inc/batch_trial.hpp','src/gen/game_collision.gen.cpp'):d['files']['game/'+n]=hashlib.sha256((r/'empty-car'/n).read_bytes()).hexdigest()
d['soloResidenceRepair']='At renderStaticBatches entry after dayNightTick/current light setup, before OFF return: prepare normal missing solo ObjectGeometry once via rebuildObjectGeometry, require immutable clean eligible world-space non-impostor members; retain300 normal material parts/70800 source inputs alongside batch buffers. No geometry rebuild/free at phase switches. Added dual-resident heap/startup work common to both arms and unpriced; third runtime rejection lacked solo bags, not an engine failure.';p.write_bytes((json.dumps(d,indent=2)+'\n').encode());print('SOLO_RESIDENCE_READY')
