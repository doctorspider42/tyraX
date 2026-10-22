from pathlib import Path
import shutil,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');parent=lab/'tyrax2-city-day-drive-v1';out=lab/'tyrax2-city-physics-v2';assert not out.exists()
def ignored(d,n):return [x for x in n if x in {'obj','__pycache__','.git','logs','screenshots','preview','authoring'}or x.endswith(('.elf','.sym','.log'))or x in {'log.txt','closeout-samples.txt','ps2link.run'}]
out.mkdir();shutil.copytree(parent/'tyra',out/'tyra',ignore=ignored);shutil.copytree(parent/'vehicle/game',out/'vehicle/game',ignore=ignored)
game=out/'vehicle/game';engine=out/'tyra/engine';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
header='''#pragma once
// PRIVATE scope: ordinary CPU elapsed time, no drain/wait, cold export only.
#include "night_sampler.hpp"
#include <string.h>
namespace VehicleProbe {
struct Sample {unsigned frame,dtBits,planned,executed,enabled,elapsed;};
inline Sample samples[128]{};
inline unsigned captured[3]{},start=0,index=0;
inline bool inside=false;
inline bool timer(unsigned phase,unsigned i){return phase==0 || (phase==1?(i>=40&&i<88):(i<40||i>=88));}
inline void begin(float dt){
 const unsigned frame=NightSampler::frame,phase=frame/1800,offset=frame%1800;
 inside=phase<3&&offset>=900&&offset<1028;if(!inside)return;
 index=offset-900;
 if(samples[index].frame==frame&&frame!=0)NightSampler::valid=false;
 unsigned bits;memcpy(&bits,&dt,4);
 samples[index]={frame,bits,0,0,unsigned(timer(phase,index)),0};
 if(samples[index].enabled)start=NightSampler::ticks();
}
inline void planned(unsigned n){if(inside)samples[index].planned=n;}
inline void executed(){if(inside)++samples[index].executed;}
inline void end(){
 if(!inside)return;
 auto& s=samples[index];
 if(s.enabled){s.elapsed=NightSampler::ticks()-start;if(!NightSampler::deltaValid(s.elapsed))NightSampler::valid=false;}
 if(s.planned<1||s.planned>4||s.executed!=s.planned||s.frame!=NightSampler::frame)NightSampler::valid=false;
 ++captured[s.frame/1800];inside=false;
}
}
'''
(engine/'inc/debug/vehicle_probe.hpp').write_bytes(header.encode())
def edit(p,a,b):
 s=p.read_text();assert s.count(a)==1,(p,a,s.count(a));p.write_bytes(s.replace(a,b).encode())
edit(game/'src/gen/game_vehicles.gen.cpp','void TerrainGame::stepVehicles(float dt) {','void TerrainGame::stepVehicles(float dt) {\n  VehicleProbe::begin(dt);')
edit(game/'src/gen/game_vehicles.gen.cpp','  const float h = dt / (float)n;','  VehicleProbe::planned((unsigned)n);\n  const float h = dt / (float)n;')
edit(game/'src/gen/game_vehicles.gen.cpp','    updateVehicles(h);','    VehicleProbe::executed();\n    updateVehicles(h);')
edit(game/'src/gen/game_vehicles.gen.cpp','  updateVehicleTutorial(dt);\n}','  updateVehicleTutorial(dt);\n  VehicleProbe::end();\n}')
edit(game/'src/gen/game_vehicles.gen.cpp','#include "game_runtime.gen.hpp"','#include "game_runtime.gen.hpp"\n#include "debug/vehicle_probe.hpp"')
edit(engine/'inc/debug/closeout.hpp','#include "night_sampler.hpp"','#include "night_sampler.hpp"\n#include "vehicle_probe.hpp"')
export='''  if(mode(p)){
   const auto& v=VehicleProbe::samples[i];
   if(v.frame!=p*1800+900+i||v.planned!=v.executed||VehicleProbe::captured[p]!=128)valid=false;
   fprintf(file,"LOG: CLOSEOUTVEH phase=%u i=%u frame=%u dtBits=%08x planned=%u executed=%u enabled=%u elapsed=%u addedReads=%u captured=%u\\n",p,i,v.frame,v.dtBits,v.planned,v.executed,v.enabled,v.elapsed,v.enabled*2,VehicleProbe::captured[p]);
  }
'''
edit(engine/'inc/debug/closeout.hpp','  if(ferror(file))valid=false;',export+'  if(ferror(file))valid=false;')
files={}
for base,prefix in ((game/'src','game/src'),(game/'inc','game/inc'),(engine/'src','tyra/engine/src'),(engine/'inc','tyra/engine/inc')):
 for f in base.rglob('*'):
  if f.is_file():files[prefix+'/'+f.relative_to(base).as_posix()]=sha(f)
for name in ('Makefile',next(game.glob('*.tyra')).name,'bin/closeout.cfg'):files['game/'+name]=sha(game/name)
for name in ('tyra/Makefile.base','tyra/engine/Makefile'):files[name]=sha(out/name)
(out/'vehicle/source-review.json').write_text(json.dumps(dict(status='PRIVATE_PHYSICS_PROBE_SOURCE_FROZEN',files=files,route=1,parentSourceReviewSha256=sha(parent/'vehicle/source-review.json'),noAddedWaits=True,frameProfile=0,hardwareTrace=0,addedCountReads=512,scope='stepVehicles through updateVehicleTutorial',commonDisabledMetadataFootprintUnpriced=True),indent=2)+'\n')
(out/'README.md').write_text('Private day-drive parent clone. Actual planned/executed substeps and dt are recorded for all384 raw loops. Two extra Count reads surround stepVehicles only when enabled: all128 driving loops; parked phase1 Off40/On48/Off40, phase2 On40/Off48/On40. Total512 extra reads, separate from ordinary sampler262/phase. No new wait, per-frame log or gameplay change. Scope includes stepVehicles cache invalidation, all vehicle update invocations and tutorial. Metadata/counter/array footprint common and unpriced. Parked calibration prices net active clock/elapsed-write path in these short windows only, with tiny car creep; do not uniformly subtract it or transfer emulator timings. Correlation is not a causal two-substep price because world/view/dt differ.\n')
print(out)
