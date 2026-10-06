from pathlib import Path
import json,hashlib,shutil
r=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1');old=r/'rejected-native-v5';assert not old.exists();old.mkdir()
for n in('native-proof.json','source-review.json','bin-assets-source-pins.json','empty-car/bin/empty-car.elf','empty-car/bin/empty-car.elf.sym','empty-car/src/gen/game_collision.gen.cpp'):
 q=old/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(r/n,q)
p=r/'empty-car/src/gen/game_collision.gen.cpp';s=p.read_text(encoding='utf8');start=s.index('  if(!BatchTrial::soloPrepared){');end=s.index('  if(!BatchTrial::on)return;',start)
new='''  if(!BatchTrial::soloPrepared){
    unsigned errors=0,matrix=0,impostor=0,dirty=0,unmapped=0,empty=0;
    if(!BatchTrial::captured)errors|=1;
    if(BatchTrial::originalMembership.size()!=size_t(SCENE_OBJECT_COUNT)||runtimeObjects.size()<size_t(SCENE_OBJECT_COUNT)||objectGeometry.size()<size_t(SCENE_OBJECT_COUNT))errors|=2;
    if(!(errors&2))for(int owner=5;owner<=54;++owner){
      if(!BatchTrial::member(BatchTrial::originalMembership[owner])){++unmapped;errors|=4;}
      if(runtimeObjects[owner].dirty){++dirty;errors|=8;}
      if(objectGeometry[owner].parts.empty()&&!runtimeObjects[owner].dirty)rebuildObjectGeometry(owner);
      const ObjectGeometry& geometry=objectGeometry[owner];
      if(geometry.matrixMode||runtimeObjects[owner].onMatrixPath){++matrix;errors|=16;}
      if(geometry.impostor){++impostor;errors|=32;}
      for(const GeoPart& part:geometry.parts){
        if(!part.bag||part.vertices.empty()){++empty;errors|=64;continue;}
        ++BatchTrial::soloParts;BatchTrial::soloVertices+=part.bag->count;
      }
    }
    if(BatchTrial::soloParts!=300||BatchTrial::soloVertices!=70800)errors|=128;
    if(errors)NightSampler::valid=false;
    BatchTrial::soloPrepared=true;
    printf("LOG: BATCHPREP owners=50 parts=%u vertices=%u matrix=%u impostor=%u dirty=%u unmapped=%u empty=%u errors=%u authored=%u mapSize=%u runtimePool=%u geometryPool=%u valid=%u\\n",BatchTrial::soloParts,BatchTrial::soloVertices,matrix,impostor,dirty,unmapped,empty,errors,unsigned(SCENE_OBJECT_COUNT),unsigned(BatchTrial::originalMembership.size()),unsigned(runtimeObjects.size()),unsigned(objectGeometry.size()),unsigned(NightSampler::valid));fflush(stdout);
  }
'''
s=s[:start]+new+s[end:];p.write_bytes(s.encode());p=r/'source-review.json';d=json.loads(p.read_text());d['files']['game/src/gen/game_collision.gen.cpp']=hashlib.sha256((r/'empty-car/src/gen/game_collision.gen.cpp').read_bytes()).hexdigest();d['poolGuardRepair']='Actual normal setupScene allocates runtimeObjects/objectGeometry authored count + MAX_SPAWNED_OBJECTS; map is authored count only. Require map==SCENE_OBJECT_COUNT and both pools>=authored count. BATCHPREP prints actual map/pool/matrix/impostor/dirty/unmapped/empty values and error bits. No solo/batch rendering work changed; v5 preserved rejected.';p.write_bytes((json.dumps(d,indent=2)+'\n').encode());print('ACTUAL_POOL_GUARD_FIXED')
