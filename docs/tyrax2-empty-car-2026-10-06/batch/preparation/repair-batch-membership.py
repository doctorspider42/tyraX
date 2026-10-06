from pathlib import Path
import json,hashlib
r=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1');p=r/'empty-car/inc/batch_trial.hpp';s=p.read_text().replace('std::vector<int>','std::vector<short>');p.write_bytes(s.encode());p=r/'empty-car/src/terrain_game.cpp';s=p.read_text(encoding='utf8').replace('NightSampler::frame/1800,NightSampler::frame%1800,unsigned(BatchTrial::on)','unsigned(NightSampler::frame/1800),unsigned(NightSampler::frame%1800),unsigned(BatchTrial::on)');p.write_bytes(s.encode());p=r/'source-review.json';d=json.loads(p.read_text());
for n in ('inc/batch_trial.hpp','src/terrain_game.cpp'):d['files']['game/'+n]=hashlib.sha256((r/'empty-car'/n).read_bytes()).hexdigest()
d['buildRepair']='Initial compile exposed actual objectBatchOf vector<short> rather than vector<int>; snapshot corrected to exact same short element type, initial log retained.';p.write_bytes((json.dumps(d,indent=2)+'\n').encode())
