from pathlib import Path
import json,hashlib,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001'); base=lab/'corona24-pricing-physical-v1'; out=lab/'player-light-receivers-physical-v1'; assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((base/'target-source-manifest.json').read_text())
for n,h in m['files'].items():
 assert sha(base/n)==h
 p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(base/n,p)
for n in ('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n)
changed=[]
def edit(n,a,b,count=1):
 p=out/n;s=p.read_text();assert s.count(a)==count,(n,a,s.count(a));p.write_bytes(s.replace(a,b).encode());changed.append(n)
e='tyra/engine/'
edit(e+'inc/debug/night_ablation.hpp','inline bool valid=true;', '''inline bool valid=true;
// PRIVATE receiver quality experiment; scoped identity never persists across draws.
inline unsigned receiverMode=0;
inline bool playerReceiver=false;
struct ReceiverCounter {uint32_t bagAllowed=0,bagRejected=0,sampleAllowed=0,sampleRejected=0;};
inline ReceiverCounter receiverCounter{};
struct ReceiverScope {bool old; explicit ReceiverScope(bool player):old(playerReceiver){playerReceiver=player;} ~ReceiverScope(){playerReceiver=old;} ReceiverScope(const ReceiverScope&)=delete;};
inline bool allowReceiver(bool scalar=false){bool yes=receiverMode==0||playerReceiver;if(collectCounters){if(scalar){if(yes)++receiverCounter.sampleAllowed;else ++receiverCounter.sampleRejected;}else{if(yes)++receiverCounter.bagAllowed;else ++receiverCounter.bagRejected;}}return yes;}
''')
edit('game/inc/terrain_game.hpp','  int resolveClipIndex(int objectIndex, const char* clipName) const;','''  int resolveClipIndex(int objectIndex, const char* clipName) const;
  bool experimentalPlayerReceiver(int object) const {
    return object>=0 && (object==players[0].objIndex ||
      (playerTwoActive && object==players[1].objIndex) ||
      (vehicleDriver_>=0 && vehicleDriver_<vehicleCount_ && object==vehicles_[vehicleDriver_].object));
  }
''')
edit(e+'src/renderer/3d/pipeline/static/core/stapip_core.cpp','#include "renderer/3d/pipeline/static/core/stapip_core.hpp"','#include "renderer/3d/pipeline/static/core/stapip_core.hpp"\n#include "debug/night_ablation.hpp"')
edit(e+'src/renderer/3d/pipeline/static/core/stapip_core.cpp','  const bool wantsLightPick = !bag->lighting && bag->info->dynLightPick;','  const bool receiverAllowed=NightAblation::allowReceiver();\n  const bool wantsLightPick = receiverAllowed && !bag->lighting && bag->info->dynLightPick;')
edit(e+'src/renderer/3d/pipeline/static/core/stapip_core.cpp','  if (!bag->info->spotLit) bagLight = &kNoSpotLight;','  if (!receiverAllowed || !bag->info->spotLit) bagLight = &kNoSpotLight;')
edit('game/inc/game_runtime.gen.hpp','  out[0] = out[1] = out[2] = 0.0F;','  out[0] = out[1] = out[2] = 0.0F;\n  if(!NightAblation::allowReceiver(true))return;')
edit('game/src/gen/game_scene.gen.cpp','    float dl[3];\n    dynLightAt','    NightAblation::ReceiverScope lightScope(experimentalPlayerReceiver(i));\n    float dl[3];\n    dynLightAt')
edit('game/src/gen/game_scene.gen.cpp','      float dl[3];\n      dynLightAt','      NightAblation::ReceiverScope lightScope(experimentalPlayerReceiver(i));\n      float dl[3];\n      dynLightAt')
edit('game/src/gen/game_physics.gen.cpp','  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {\n    if (!runtimeObjects[i].active) continue;  // streamed out with its layer','  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {\n    NightAblation::ReceiverScope lightScope(experimentalPlayerReceiver(i));\n    if (!runtimeObjects[i].active) continue;  // streamed out with its layer')
# Separate world/player wheel storage in restricted mode, preserving baseline mode batching.
edit('game/src/gen/game_vehicles.gen.cpp','  for (int drawDef = 0; drawDef < VEHICLE_DEF_COUNT; ++drawDef) {\n  if ((int)wheelBatches_.size() <= drawDef)\n    wheelBatches_.resize((size_t)drawDef + 1);\n  WheelBatch& batch = wheelBatches_[(size_t)drawDef];','''  for (int drawDef = 0; drawDef < VEHICLE_DEF_COUNT; ++drawDef) {
  for(int receiverPass=0;receiverPass<(NightAblation::receiverMode?2:1);++receiverPass){
  const int batchIndex=drawDef+receiverPass*VEHICLE_DEF_COUNT;
  NightAblation::ReceiverScope lightScope(receiverPass==1);
  if ((int)wheelBatches_.size() <= batchIndex)
    wheelBatches_.resize((size_t)batchIndex + 1);
  WheelBatch& batch = wheelBatches_[(size_t)batchIndex];''')
# Unique within renderVehicleWheels slice only.
p=out/'game/src/gen/game_vehicles.gen.cpp';s=p.read_text();start=s.index('void TerrainGame::renderVehicleWheels()');end=s.index('#if TYRA_WHEEL_REBUILD_REPORT\n  // Same 300-frame',start);t=s[start:end]
a='    if (!v.active || v.def != drawDef) continue;';assert t.count(a)==1;t=t.replace(a,a+'\n    if(NightAblation::receiverMode && experimentalPlayerReceiver(v.object)!=(receiverPass==1))continue;')
a='  stapip.core.render(wheelBag_.get());\n  }';assert t.count(a)==1;t=t.replace(a,a+'\n  }');p.write_bytes((s[:start]+t+s[end:]).encode())
# Protocol 10 compares receiver-only; 11 adds scene pool suppression. Corona candidate stays off.
edit(e+'inc/debug/night_plan.hpp','c.kind==7||c.kind==9','c.kind==7||c.kind==9||c.kind==10||c.kind==11',count=2)
edit(e+'inc/debug/night_plan.hpp','if(c.kind!=3)return 0;','if(c.kind==11)return ((p==1)!=(c.order==1))?1u:0u;if(c.kind!=3)return 0;')
edit(e+'inc/debug/night_plan.hpp','return c.kind==9 ||','return c.kind==9 || c.kind==10 || c.kind==11 ||')
edit(e+'inc/debug/night_runtime.hpp','NightAblation::poolTableSelected=planConfig.kind==7||planConfig.kind==9;','NightAblation::receiverMode=(planConfig.kind==10||planConfig.kind==11)&&((p==1)!=(planConfig.order==1))?planConfig.kind-9:0;printf("LOG: NIGHTRECEIVERPHASE phase=%u mode=%u\\n",p,NightAblation::receiverMode);NightAblation::poolTableSelected=planConfig.kind==7||planConfig.kind==9||planConfig.kind==10||planConfig.kind==11;')
edit(e+'inc/debug/night_runtime.hpp','(planConfig.kind==7||planConfig.kind==9)?1u:0u','(planConfig.kind==7||planConfig.kind==9||planConfig.kind==10||planConfig.kind==11)?1u:0u')
edit(e+'inc/debug/night_runtime.hpp','NightAblation::coronaCounter=NightAblation::CoronaCounter{};','NightAblation::coronaCounter=NightAblation::CoronaCounter{};NightAblation::receiverCounter=NightAblation::ReceiverCounter{};')
edit(e+'inc/debug/night_runtime.hpp','if(o==750||o==1155){const auto& cc=','if(o==750||o==1155){const auto& rc=NightAblation::receiverCounter;printf("LOG: NIGHTRECEIVERGATES phase=%u offset=%u mode=%u bagAllowed=%u bagRejected=%u sampleAllowed=%u sampleRejected=%u\\n",p,o,NightAblation::receiverMode,rc.bagAllowed,rc.bagRejected,rc.sampleAllowed,rc.sampleRejected);const auto& cc=')
for n in m['files']:m['files'][n]=sha(out/n)
m['status']='ROOT_DRAFT_PLAYER_RECEIVER_SOURCE_NOT_RELEASED';m['files']=dict(sorted(m['files'].items()));(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode())
(out/'receiver-experiment-design.json').write_bytes((json.dumps(dict(status='DRAFT_NOT_RUNTIME_RELEASED',baseManifestSha256=sha(base/'target-source-manifest.json'),changes=sorted(set(changed)),modes={'0':'full','1':'player_and_driven_vehicle_only_pools_retained','2':'same_receivers_scene_pools_suppressed'},coronaSprite=False,extraTimedClockReads=0,wheelBatchSplitOnlyRestrictedMode=True,remaining=['native build','all render-path audit','strict host dialect','emulator activation and image review','physical comparison']),indent=2)+'\n').encode())
print(out,sha(out/'target-source-manifest.json'))
