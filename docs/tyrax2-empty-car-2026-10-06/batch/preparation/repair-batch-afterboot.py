from pathlib import Path
import json,hashlib,shutil
r=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1');old=r/'rejected-native-v2';assert not old.exists();old.mkdir()
for n in('native-proof.json','source-review.json','bin-assets-source-pins.json','empty-car/bin/empty-car.elf','empty-car/bin/empty-car.elf.sym','empty-car/src/terrain_game.cpp','empty-car/inc/batch_trial.hpp'):
 q=old/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(r/n,q)
p=r/'empty-car/inc/batch_trial.hpp';s=p.read_text().replace('inline bool on=true;','inline bool on=true,captured=false;');p.write_bytes(s.encode())
p=r/'empty-car/src/terrain_game.cpp';s=p.read_text(encoding='utf8');start=s.index('  const unsigned batchPhase=NightSampler::frame/1800;');end=s.index('  Tyra::PadButtons held=',start);block=s[start:end];s=s[:start]+s[end:]
block=block.replace('if(BatchTrial::originalMembership.empty())BatchTrial::originalMembership=objectBatchOf;','''if(!BatchTrial::captured){
      unsigned initialOwners=0;for(int owner=5;owner<=54;++owner)if(objectBatchOf[owner]>=0)++initialOwners;
      if(bootPhase!=2||staticBatches.size()!=24||initialOwners!=50)NightSampler::valid=false;
      BatchTrial::originalMembership=objectBatchOf;BatchTrial::captured=true;
    }''')
needle='  // Credits roll (Tools > Credits Editor, docs/credits.md):';assert s.count(needle)==1;s=s.replace(needle,block+'\n'+needle,1);p.write_bytes(s.encode())
p=r/'source-review.json';d=json.loads(p.read_text());
for n in('inc/batch_trial.hpp','src/terrain_game.cpp'):d['files']['game/'+n]=hashlib.sha256((r/'empty-car'/n).read_bytes()).hexdigest()
d['snapshotRepair']='Switch moved after completed bootPhase block/bootFirstScene. Explicit captured flag; one-time verifies24 batches and50 eligible owner map entries before original snapshot. No empty-vector readiness heuristic or per-frame50 scan. Original native-v2/ELF retained in rejected-native-v2.';p.write_bytes((json.dumps(d,indent=2)+'\n').encode());print('POSTBOOT_SNAPSHOT_REPAIRED')
