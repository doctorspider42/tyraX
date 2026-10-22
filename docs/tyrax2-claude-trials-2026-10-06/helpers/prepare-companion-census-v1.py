from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');base=b/'object-route-physical-v1';out=b/'companion-census-physical-v1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text(encoding='utf8'));assert not(out/'target-source-manifest.json').exists()
for n,d in m['files'].items():
 assert sha(base/n)==d
 p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/n,p)
for n in ('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n,dirs_exist_ok=True)
def edit(n,a,z):
 p=out/n;s=p.read_text(encoding='utf8');assert s.count(a)==1,(n,a,s.count(a));p.write_bytes(s.replace(a,z).encode())
for name in ('stapip_core.cpp','stapip_qbuffer_renderer.cpp'):
 n='tyra/engine/src/renderer/3d/pipeline/static/core/'+name;shutil.copyfile(repo/'vendor'/n,out/n)
for n in ('night_plan.hpp','night_runtime.hpp'):
 p=out/'tyra/engine/inc/debug'/n;p.write_bytes(p.read_text(encoding='utf8').replace('kind==22','kind==24').encode())
n='tyra/engine/inc/debug/night_runtime.hpp';edit(n,'NightAblation::producerTiming=NightAblation::producerSelected&&NightAblation::producerEnabled&&index<5400&&o>=800&&o<1120;','NightAblation::producerTiming=false;')
# Count protocol lives in existing frozen header; source count remains 501.
p=out/'tyra/engine/inc/debug/night_ablation.hpp'
header=r'''
#include <string.h>
#include <stdio.h>
// Private kind24 cold-only activation census. No clock reads or per-bag IO.
namespace CompanionCensus {
enum Field { Entered,Accepted,Culled,SameGeometry,SameModelPtr,SameModelBits,SameVP,SamePlanes,SamePartition,EligibleAdjacent,BboxHit,BboxRecalc,BboxFresh,VerticesRescanned,RetainedHit,RetainedBuild,RetainedResize,RetainedInvalidate,BakedHit,BakedCapture,AcceptedAdjacent,RetainedAcquireHit,RetainedAcquireRebuild,RetainedFresh,RetainedRefused,Fields };
inline unsigned long long rows[5][Fields]{};
inline unsigned role=4;inline unsigned slotRoles[64]{};inline bool slotKnown[64]{};inline bool havePrevious=false;
struct Identity { const void* vertices;unsigned count,stamp,size,clipSize,mode,transform,clip,stripped,billboard;const void* model;float modelBits[16],vp[16],planes[24]; };
inline Identity previous{};inline bool previousAccepted=false,currentPreviousAccepted=false,eligibleCurrent=false;
inline bool active(){return NightAblation::collectCounters;}
inline void add(unsigned field,unsigned n=1){if(active())rows[role<5?role:4][field]+=n;}
inline void setRole(unsigned r){role=r;}
struct SlotScope {unsigned saved;SlotScope(unsigned i):saved(role){if(active())role=i<64&&slotKnown[i]?slotRoles[i]:4;}~SlotScope(){role=saved;}};
inline void noteSlot(unsigned i){if(active()&&i<64){slotRoles[i]=role;slotKnown[i]=true;}}
inline void reset(){memset(rows,0,sizeof(rows));memset(slotKnown,0,sizeof(slotKnown));role=4;havePrevious=false;previousAccepted=false;currentPreviousAccepted=false;eligibleCurrent=false;}
inline void enter(const void* v,unsigned count,unsigned stamp,unsigned size,unsigned clipSize,const void* mp,const float* model,const float* vp,const float* planes,unsigned mode,unsigned transform,unsigned clip,unsigned stripped,unsigned billboard){
 if(!active())return;currentPreviousAccepted=previousAccepted;previousAccepted=false;eligibleCurrent=false;add(Entered);Identity now{};now.vertices=v;now.count=count;now.stamp=stamp;now.size=size;now.clipSize=clipSize;now.model=mp;now.mode=mode;now.transform=transform;now.clip=clip;now.stripped=stripped;now.billboard=billboard;
 memcpy(now.modelBits,model,64);memcpy(now.vp,vp,64);memcpy(now.planes,planes,96);
 if(havePrevious){bool g=now.vertices==previous.vertices&&now.count==previous.count&&now.stamp==previous.stamp;bool mb=memcmp(now.modelBits,previous.modelBits,64)==0;bool vb=memcmp(now.vp,previous.vp,64)==0;bool pb=memcmp(now.planes,previous.planes,96)==0;bool part=now.size==previous.size&&now.clipSize==previous.clipSize&&now.mode==previous.mode&&now.transform==previous.transform&&now.clip==previous.clip&&now.stripped==previous.stripped&&now.billboard==previous.billboard;
 add(SameGeometry,g);add(SameModelPtr,now.model==previous.model);add(SameModelBits,mb);add(SameVP,vb);add(SamePlanes,pb);add(SamePartition,part);eligibleCurrent=g&&mb&&vb&&pb&&part;add(EligibleAdjacent,eligibleCurrent);}
 previous=now;havePrevious=true;
}
inline void accept(){if(!active())return;add(Accepted);add(AcceptedAdjacent,eligibleCurrent&&currentPreviousAccepted);previousAccepted=true;}
inline void emit(unsigned phase,unsigned offset,unsigned night){if(!active())return;for(unsigned r=0;r<5;++r){printf("LOG: CENSUS phase=%u offset=%u night=%u role=%u",phase,offset,night,r);for(unsigned f=0;f<Fields;++f)printf(" f%u=%llu",f,rows[r][f]);printf("\n");}fflush(stdout);}
}
'''
p.write_bytes(p.read_bytes()+header.encode())
# Begin cold-only records before update; choose mood before scripts execute.
n='game/src/terrain_game.cpp';edit(n,'  updateFrameClock();  // real dt: frame drops slow the picture, not the game','  updateFrameClock();  // real dt: frame drops slow the picture, not the game\n  if(NightRuntime::planConfig.kind==24){saveValues[0]=NightAblation::producerEnabled?1.0F:0.0F;}\n  if(CompanionCensus::active())CompanionCensus::reset();')
# emit before collectCounters cleared; NIGHTCONTEXT witnesses are already phase actual.
n='tyra/engine/inc/debug/night_runtime.hpp';edit(n,' NightAblation::collectCounters=false;NightAblation::producerTiming=false;',' CompanionCensus::emit(p,o,NightAblation::producerEnabled?1u:0u);\n NightAblation::collectCounters=false;NightAblation::producerTiming=false;')
# Owned submits scoped around actual call; unrelated callers remain other.
n='game/src/gen/game_physics.gen.cpp'
for call,r in [('stapip.core.render(part.bag.get());',0),('stapip.core.render(part.aoBag.get());',1),('stapip.core.render(part.emisBag.get());',2),('stapip.core.render(part.envBag.get());',3)]:
 p=out/n;s=p.read_text(encoding='utf8');anchor=(call+'\n        const u32 lpB') if r==0 else call
 assert s.count(anchor)==1,(anchor,s.count(anchor));s=s.replace(anchor,'{ CompanionCensus::setRole('+str(r)+'); '+call+' CompanionCensus::setRole(4); }'+('\n        const u32 lpB' if r==0 else ''));p.write_bytes(s.encode())
# Core census uses exact view/frustum raw fields, inserted after resolved max size.
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp';edit(n,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
edit(n,'  setMaxVertCount(maxVertCount);','  setMaxVertCount(maxVertCount);\n  if(CompanionCensus::active()){float censusPlanes[24];const auto* fp=rendererCore->renderer3D.frustumPlanes.getAll();for(unsigned j=0;j<6;++j){censusPlanes[j*4]=fp[j].normal.x;censusPlanes[j*4+1]=fp[j].normal.y;censusPlanes[j*4+2]=fp[j].normal.z;censusPlanes[j*4+3]=fp[j].distance;}CompanionCensus::enter(bag->vertices,bag->count,bag->bboxVersion,maxVertCount,clipPackageSize(),bag->info->model,bag->info->model->data,rendererCore->renderer3D.getViewProj().data,censusPlanes,unsigned(bag->info->frustumCulling),unsigned(bag->info->transformationType),unsigned(bag->info->fullClipChecks)|(unsigned(qbufferRenderer.isVU1ClippingEnabled())<<1),unsigned(bag->stripped),unsigned(bag->billboard!=nullptr));}')
edit(n,'      recordOutsideBag(bag);','      CompanionCensus::add(CompanionCensus::Culled);\n      recordOutsideBag(bag);')
edit(n,'  const u32 prepareStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;','  CompanionCensus::accept();\n  const u32 prepareStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;')
# Actual cache events, count only; no TYRA_STAPIP_ATTRIB enabling.
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_bag_bboxes_cacher.cpp';edit(n,'#include <algorithm>','#include <algorithm>\n#include "debug/night_ablation.hpp"')
edit(n,'        TYRA_CACHER_INC(recalcs);','        CompanionCensus::add(CompanionCensus::BboxRecalc);CompanionCensus::add(CompanionCensus::VerticesRescanned,count);\n        TYRA_CACHER_INC(recalcs);')
edit(n,'        TYRA_CACHER_INC(fresh);','        CompanionCensus::add(CompanionCensus::BboxFresh);CompanionCensus::add(CompanionCensus::VerticesRescanned,count);\n        TYRA_CACHER_INC(fresh);')
edit(n,'      TYRA_CACHER_INC(hits);','      CompanionCensus::add(CompanionCensus::BboxHit);\n      TYRA_CACHER_INC(hits);')
edit(n,'\n  TYRA_CACHER_INC(fresh);','\n  CompanionCensus::add(CompanionCensus::BboxFresh);CompanionCensus::add(CompanionCensus::VerticesRescanned,count);\n  TYRA_CACHER_INC(fresh);')
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';edit(n,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
p=out/n;s=p.read_text(encoding='utf8');
for call,field in [('retained.countHit();','RetainedHit'),('retained.countBuild();','RetainedBuild'),('baked.countHit();','BakedHit'),('baked.countBuild();','BakedCapture')]:s=s.replace(call,'CompanionCensus::add(CompanionCensus::'+field+'); '+call)
s=s.replace('      if (item.packages != key.packages) {','      CompanionCensus::add(CompanionCensus::RetainedInvalidate);\n      if (item.packages != key.packages) {',1)
s=s.replace('        item.data = std::unique_ptr<qword_t[]>(new qword_t[wants]);','        CompanionCensus::add(CompanionCensus::RetainedResize);\n        item.data = std::unique_ptr<qword_t[]>(new qword_t[wants]);',1);p.write_bytes(s.encode())
edit(n,'  auto* result = buffers[currentBufferIndex];','  auto* result = buffers[currentBufferIndex];\n  CompanionCensus::noteSlot(currentBufferIndex);')
edit(n,'    if (!buffers[i]->any()) continue;','    if (!buffers[i]->any()) continue;\n    CompanionCensus::SlotScope censusSlot(i);')
# Retained acquisition accounting distinguishes key hit from captures/rebuilds.
p=out/n;s=p.read_text(encoding='utf8');s=s.replace('      if (keyMatches(item, key)) return &item;','      if (keyMatches(item, key)){CompanionCensus::add(CompanionCensus::RetainedAcquireHit);return &item;}',1);p.write_bytes(s.encode())
edit(n,'      CompanionCensus::add(CompanionCensus::RetainedInvalidate);','      CompanionCensus::add(CompanionCensus::RetainedAcquireRebuild);CompanionCensus::add(CompanionCensus::RetainedInvalidate);')
edit(n,'        if (key.packages == 0 || usedQwords - had + wants > kMaxQwords)\n          return nullptr;','        if (key.packages == 0 || usedQwords - had + wants > kMaxQwords){CompanionCensus::add(CompanionCensus::RetainedRefused);return nullptr;}')
edit(n,'  if (key.packages == 0 || wanted > kMaxQwords) return nullptr;','  if (key.packages == 0 || wanted > kMaxQwords){CompanionCensus::add(CompanionCensus::RetainedRefused);return nullptr;}')
edit(n,'  if (usedQwords + wanted > kMaxQwords && !evictFor(wanted)) return nullptr;','  if (usedQwords + wanted > kMaxQwords && !evictFor(wanted)){CompanionCensus::add(CompanionCensus::RetainedRefused);return nullptr;}')
edit(n,'  StaPipRetainedEntry entry;','  CompanionCensus::add(CompanionCensus::RetainedFresh);\n  StaPipRetainedEntry entry;')
for n in m['files']:m['files'][n]=sha(out/n)
m['frozen']=False
(out/'draft-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());(out/'draft-review.json').write_bytes((json.dumps(dict(status='DRAFT_NOT_FROZEN',adjacencySemantics='Consecutive nonempty entered bags; empty-return invocations are absent.',identityCaptureOrder='Immediately after original setMaxVertCount(maxVertCount), before main-frustum return.',baseManifestSha256=sha(base/'target-source-manifest.json'),sourceFiles=len(m['files']),fields=['entered','accepted','culled','sameGeometry','sameModelPtr','sameModelBits','sameVP','samePlanes','samePartition','eligibleAdjacent','bboxHit','bboxRecalc','bboxFresh','verticesRescanned','retainedHit','retainedBuild','retainedResize','retainedInvalidate','bakedHit','bakedCapture','acceptedAdjacent','retainedAcquireHit','retainedAcquireRebuild','retainedFresh','retainedRefused'],commonBranchFootprintUnpriced=True,dayNightIsNotObserverTax=True),indent=2)+'\n').encode())
print('Draft prepared',out)
