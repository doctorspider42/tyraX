from pathlib import Path
import hashlib,json
root=Path(__file__).parent/'quiet-v4/showcase';game=root/'game';header=game/'inc/gate_witness.hpp';assert not header.exists()
header.write_bytes(b'#pragma once\nnamespace GateWitness { inline unsigned crossings[2]={0,0},other=0; inline void crossed(int portal){if(portal>=0&&portal<2)++crossings[portal];else ++other;} }\n')
p=game/'src/gen/game_physics.gen.cpp';s=p.read_text();anchor='      TYRA_LOG("Portal: player crossed ", pi, " to ", *px, " ", *py, " ", *pz);';assert s.count(anchor)==1;s='#include "gate_witness.hpp"\n'+s;s=s.replace(anchor,'      GateWitness::crossed(pi);  // PRIVATE actual successful teleport branch, no hot output\n'+anchor);p.write_bytes(s.encode())
p=game/'src/terrain_game.cpp';s=p.read_text();s='#include "gate_witness.hpp"\n'+s
anchor='playerZ=%08x valid=%u\\n';assert s.count(anchor)==1;s=s.replace(anchor,'playerZ=%08x portal0=%u portal1=%u portalOther=%u valid=%u\\n')
anchor='bits[6],bits[7],bits[8],unsigned(NightSampler::valid)';assert s.count(anchor)==1;s=s.replace(anchor,'bits[6],bits[7],bits[8],GateWitness::crossings[0],GateWitness::crossings[1],GateWitness::other,unsigned(NightSampler::valid)');p.write_bytes(s.encode())
proof=root/'source-review.json';d=json.loads(proof.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for name in ('src/terrain_game.cpp','src/gen/game_physics.gen.cpp','inc/gate_witness.hpp'):d['files']['game/'+name]=sha(game/name)
d['portalWitness']='Two cumulative unsigned counters increment only at existing actual successful player teleport branch; cold-only output; no hot logging/clocks/waits. Rare increments common unpriced observer footprint.'
proof.write_text(json.dumps(d,indent=2)+'\n',encoding='utf-8')
print('PRIVATE_GATE_BRANCH_COLD_COUNTERS_PREPARED')
