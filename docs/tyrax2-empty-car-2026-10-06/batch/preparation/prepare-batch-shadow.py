from pathlib import Path
import shutil,json,hashlib,subprocess
lab=Path('F:/Projects/tyrax2-lab-20261001');parent=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';assets=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-assets-v2';r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';assert not r.exists();r.mkdir();game=r/'empty-car';ignore=lambda d,n:[x for x in n if x in('bin','obj','__pycache__','.git')or x.endswith(('.elf','.log'))]
shutil.copytree(parent/'empty-car',game,ignore=ignore);shutil.copytree(parent/'tyra',r/'tyra',ignore=ignore);shutil.copytree(assets/'empty-car/bin',game/'bin',ignore=lambda d,n:[x for x in n if x.endswith(('.elf','.sym'))or x in('closeout-samples.txt','log.txt','livedbg.cmd','frame.tga')])
(game/'inc/batch_trial.hpp').write_bytes(b'''#pragma once
#include "debug/closeout.hpp"
#include <vector>
namespace BatchTrial {
inline std::vector<int> originalMembership;
inline unsigned appliedPhase=~0U;
inline bool on=true;
inline bool desired(unsigned frame){unsigned p=frame/1800;if(p>2)p=2;return (p!=1)!=(Closeout::order==1);}
}
''')
p=game/'src/gen/game_collision.gen.cpp';s=p.read_text(encoding='utf8');s='#include "batch_trial.hpp"\n'+s
needle='void TerrainGame::renderStaticBatches() {';assert s.count(needle)==1;s=s.replace(needle,needle+'\n  if(!BatchTrial::on)return;',1);p.write_bytes(s.encode())
p=game/'src/terrain_game.cpp';s=p.read_text(encoding='utf8');s='#include "batch_trial.hpp"\n'+s
needle='  EmptyRoute::apply(engine->pad,NightSampler::frame,vehicles_[0].pos[0],vehicles_[0].pos[2],vehicles_[0].yaw,vehicles_[0].speed);';assert s.count(needle)==1
replacement='''  const unsigned batchPhase=NightSampler::frame/1800;
  if(batchPhase<3&&BatchTrial::appliedPhase!=batchPhase){
    if(BatchTrial::originalMembership.empty())BatchTrial::originalMembership=objectBatchOf;
    BatchTrial::on=BatchTrial::desired(NightSampler::frame);
    if(BatchTrial::on)objectBatchOf=BatchTrial::originalMembership;
    else objectBatchOf.assign(SCENE_OBJECT_COUNT,-1);
    BatchTrial::appliedPhase=batchPhase;
    printf("LOG: BATCHPHASE phase=%u enabled=%u order=%u retainedBatches=%u\\n",batchPhase,unsigned(BatchTrial::on),Closeout::order,unsigned(staticBatches.size()));fflush(stdout);
  }
  Tyra::PadButtons held={},clicked={};engine->pad.setState(held,clicked,127,127,127,127);'''
s=s.replace(needle,replacement).replace('route=10','route=11')
s=s.replace('static float privateLightTime=0;privateLightTime+=g_frameDt;','const float privateLightTime=float(NightSampler::frame%1800)*(1.0F/59.94F);')
needle='  if (Closeout::cold()) {';assert s.count(needle)==1
s=s.replace(needle,needle+'''\n    unsigned mapped=0;for(int owner=5;owner<=54;++owner)if(objectBatchOf[owner]>=0)++mapped;
    if(BatchTrial::on?mapped!=50:mapped!=0)NightSampler::valid=false;
    printf("LOG: BATCHCONTEXT phase=%u offset=%u enabled=%u mappedOwners=%u retainedBatches=%u valid=%u\\n",NightSampler::frame/1800,NightSampler::frame%1800,unsigned(BatchTrial::on),mapped,unsigned(staticBatches.size()),unsigned(NightSampler::valid));fflush(stdout);
''',1);p.write_bytes(s.encode())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();d=json.loads((parent/'source-review.json').read_text());d['files']={n:sha(game/n[5:]if n.startswith('game/')else r/n)for n in d['files']};d['files']['game/inc/batch_trial.hpp']=sha(game/'inc/batch_trial.hpp');d.update(status='PRIVATE_SAME_ELF_STATIC_BATCH_STATIONARY_CONTRAST_SOURCE_READY',head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),game=str(game),engine=str(r/'tyra'),route=11,parentShadowSourceReviewSha256=sha(parent/'source-review.json'),parentShadowNativeProofSha256=sha(parent/'native-proof.json'),parentCorrectedAssetsProofSha256=sha(assets/'bin-assets-source-pins.json'),runtimeReleased=False,batchTrial={'orders':['ON/OFF/ON','OFF/ON/OFF'],'parkedAllPhases':True,'normalBatchBuilderUnchanged':True,'buffersRetainedThroughout':True,'offRoutingMapMinusOne':True,'onOriginalEligibleMembership':True,'pointController':'Same phase-local per-frame two-sine work and path in all arms,59.94 nominal step; replaces inherited continuous real-dt trajectory explicitly.','addedWaits':0,'newClocks':0,'sameGeometryRuntimeQualificationPending':True});(r/'source-review.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
s=(parent/'native-proof.py').read_text(encoding='utf8').replace(parent.name,r.name).replace("requiredGame=['inc/scene_data.hpp'","requiredGame=['inc/batch_trial.hpp','inc/scene_data.hpp'").replace('PASS_PRIVATE_NORMAL_RAVAGER_BLOB_80K_NATIVE_SOURCE_OBJECT_LINK_BINDING','PASS_PRIVATE_STATIONARY_STATIC_BATCH_CONTRAST_NATIVE_BINDING');(r/'native-proof.py').write_bytes(s.encode())
data=json.loads((assets/'bin-assets-source-pins.json').read_text());data['files']={n:sha(game/'bin'/n)for n in data['files']if (game/'bin'/n).is_file()};data['status']='PARENT_REPAIRED_SHADOW_ASSETS_RETAINED_NEW_ELF_PENDING';(r/'bin-assets-source-pins.json').write_bytes((json.dumps(data,indent=2)+'\n').encode());print(r)
