from pathlib import Path
import json,hashlib,shutil
r=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1');old=r/'rejected-native-v3';assert not old.exists();old.mkdir()
for n in('native-proof.json','source-review.json','bin-assets-source-pins.json','empty-car/bin/empty-car.elf','empty-car/bin/empty-car.elf.sym','empty-car/src/terrain_game.cpp','empty-car/inc/batch_trial.hpp'):
 q=old/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(r/n,q)
p=r/'empty-car/inc/batch_trial.hpp';s=p.read_text();s=s.replace('namespace BatchTrial {','namespace BatchTrial {\ninline constexpr bool member(short entry){return entry>=0||entry==-2;}');p.write_bytes(s.encode())
p=r/'empty-car/src/terrain_game.cpp';s=p.read_text(encoding='utf8');assert s.count('objectBatchOf[owner]>=0')==2;s=s.replace('objectBatchOf[owner]>=0','BatchTrial::member(objectBatchOf[owner])');p.write_bytes(s.encode())
p=r/'source-review.json';d=json.loads(p.read_text());
for n in('inc/batch_trial.hpp','src/terrain_game.cpp'):d['files']['game/'+n]=hashlib.sha256((r/'empty-car'/n).read_bytes()).hexdigest()
d['sentinelRepair']='Normal objectBatchOf -2 represents a multi-batch member. Yes/no eligibility accepts >=0 or -2; private initial/cold census now follows this exact contract while rejecting unknown negative sentinels. No rendering/routing work changed. Native-v3 source/ELF retained rejected-native-v3.';p.write_bytes((json.dumps(d,indent=2)+'\n').encode());print('MEMBERSHIP_SENTINEL_FIXED')
