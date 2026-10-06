from pathlib import Path
import hashlib, json, shutil

LAB=Path('F:/Projects/tyrax2-lab-20261001')
REPO=Path('F:/Projects/tyra-editor')
BASE=LAB/'companion-census-physical-v1'
ORIGINAL=LAB/'object-route-physical-v1'
OUT=LAB/'night-main11-batch-physical-v1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((BASE/'target-source-manifest.json').read_text(encoding='utf8'))
assert m['frozen'] and len(m['files'])==501
assert not (OUT/'target-source-manifest.json').exists(), 'Never overwrite frozen source'
for n,h in m['files'].items():
    assert sha(BASE/n)==h,n
    p=OUT/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(BASE/n,p)
for n in ('res','.res-baked'):
    shutil.copytree(BASE/'game'/n,OUT/'game'/n,dirs_exist_ok=True)

# Restore census-modified generated/debug files to the exact prior frozen
# object-route fixture. Retain production fixed-hour mood and sky retint.
restored=['game/src/gen/game_physics.gen.cpp','game/src/terrain_game.cpp',
          'tyra/engine/inc/debug/night_ablation.hpp',
          'tyra/engine/inc/debug/night_plan.hpp','tyra/engine/inc/debug/night_runtime.hpp']
for n in restored:shutil.copyfile(ORIGINAL/n,OUT/n)
production=[];productionBaseSha={}
for name in ('stapip_core.cpp','stapip_qbuffer_renderer.cpp','stapip_bag_bboxes_cacher.cpp'):
    n='tyra/engine/src/renderer/3d/pipeline/static/core/'+name
    shutil.copyfile(REPO/'vendor'/n,OUT/n);production.append(n);productionBaseSha[n]=sha(OUT/n)
def edit(n,a,z):
    p=OUT/n;s=p.read_text(encoding='utf8');assert s.count(a)==1,(n,a,s.count(a))
    p.write_bytes(s.replace(a,z).encode())
def append(n,z):
    p=OUT/n;p.write_bytes((p.read_text(encoding='utf8')+z).encode())
for n in ('night_plan.hpp','night_runtime.hpp'):
    p=OUT/'tyra/engine/inc/debug'/n
    p.write_bytes(p.read_text(encoding='utf8').replace('kind==22','kind==29').encode())
n='tyra/engine/inc/debug/night_runtime.hpp'
edit(n,'NightAblation::producerTiming=NightAblation::producerSelected&&NightAblation::producerEnabled&&index<5400&&o>=800&&o<1120;','NightAblation::producerTiming=false;')

fields='invocations sourceGroups sourceMembers groups members candidateGroups candidateMembers controlMainSubmits candidateMainSubmits reflectionCarriers reflectionUses fallbackGroups demotedGroups eligibilityRefused dirtyDemotions geometryCompared geometryMismatches duplicateMainSubmits verticesCompared colorBytesCompared'.split()
groupfields='ready admitted demoted submitted first count members geometryHash infoHash lightKey spotEnabled'.split()
memberfields='object idLo idHi group ready admitted mainOriginal mainGrouped reflectionUses count positionHash colorHash copiedPositionHash copiedColorHash infoHash lightKey spotEnabled'.split()
defs='''
// PRIVATE kind29: sparse semantic witnesses, no clocks or per-bag IO.
namespace NightMainBatch {
struct Stat { '''+''.join('uint32_t '+x+'=0;' for x in fields)+''' };
struct Group { '''+''.join('uint32_t '+x+'=0;' for x in groupfields)+''' };
struct Member { '''+''.join('uint32_t '+x+'=0;' for x in memberfields)+''' };
inline Stat stat{};inline Group group[3]{};inline Member member[11]{};
inline int pendingGroup=-1,pendingMember=-1;inline bool pendingMerged=false,pendingReflection=false;
inline void clearPending(){pendingGroup=pendingMember=-1;pendingMerged=pendingReflection=false;}
inline void accepted(){
 if(!NightAblation::collectCounters||pendingGroup<0)return;
 auto& g=group[pendingGroup];
 if(pendingReflection){if(pendingMember>=0){++member[pendingMember].reflectionUses;++stat.reflectionUses;}return;}
 if(pendingMerged){if(g.submitted)++stat.duplicateMainSubmits;g.submitted=1;++stat.candidateGroups;stat.candidateMembers+=g.members;++stat.candidateMainSubmits;
  for(unsigned j=0;j<g.members;++j){auto& x=member[g.first+j];if(x.mainOriginal||x.mainGrouped)++stat.duplicateMainSubmits;++x.mainGrouped;}
 }else if(pendingMember>=0){auto& x=member[pendingMember];if(x.mainOriginal||x.mainGrouped)++stat.duplicateMainSubmits;++x.mainOriginal;++stat.controlMainSubmits;}
}
inline void reset(){clearPending();stat=Stat{};for(auto& g:group)g=Group{};for(auto& x:member)x=Member{};}
inline void emit(unsigned phase,unsigned offset,bool enabled){
 for(const auto& g:group){if(g.admitted){++stat.groups;stat.members+=g.members;}else ++stat.fallbackGroups;}
 printf("LOG: NIGHTMAINBATCH phase=%u offset=%u enabled=%u'''+''.join(' '+x+'=%u' for x in fields)+'''\\n",phase,offset,unsigned(enabled),'''+','.join('stat.'+x for x in fields)+''');
 for(unsigned i=0;i<3;++i){const auto& g=group[i];printf("LOG: NIGHTMAINBATCHGROUP phase=%u offset=%u enabled=%u group=%u'''+''.join(' '+x+'=%u' for x in groupfields)+'''\\n",phase,offset,unsigned(enabled),i,'''+','.join('g.'+x for x in groupfields)+''');}
 for(unsigned i=0;i<11;++i){const auto& x=member[i];printf("LOG: NIGHTMAINBATCHMEMBER phase=%u offset=%u enabled=%u ordinal=%u'''+''.join(' '+x+'=%u' for x in memberfields)+'''\\n",phase,offset,unsigned(enabled),i,'''+','.join('x.'+x for x in memberfields)+''');}
}
}
'''
append('tyra/engine/inc/debug/night_ablation.hpp','\n#include <stdio.h>\n'+defs)
edit(n,'NightAblation::collectCounters=p<3&&(o==750||o==1155);','NightAblation::collectCounters=p<3&&(o==750||o==1155);if(NightAblation::collectCounters)NightMainBatch::reset();')
edit(n,' NightAblation::collectCounters=false;NightAblation::producerTiming=false;',' if(o==750||o==1155)NightMainBatch::emit(p,o,NightAblation::producerEnabled);\n NightAblation::collectCounters=false;NightAblation::producerTiming=false;')

h='game/inc/terrain_game.hpp'
header='''
  // PRIVATE kind29. Distinct main-only ownership; original objectBatchOf unchanged.
  struct NightMainGroup {
    StaticBatch owned;
    std::unique_ptr<Tyra::StaPipInfoBag> info;
    Tyra::CoreBBox bounds;
    int objects[4]={-1,-1,-1,-1};
    unsigned first=0,members=0,offset[4]={},count[4]={};
    u32 posStamp[4]={},colorStamp[4]={},bboxStamp[4]={};
    bool ready=false,demoted=false,keyInitialized=false,keyAdmitted=false;
    bool admitted=false,submitted=false,checked=false;
    unsigned infoHash=0,lightKey=0;bool spotEnabled=false;
    std::vector<u32> key,scratchKey;
  };
  std::vector<NightMainGroup> nightMainGroups;
  unsigned nightMainGeneration=~0U;
  void prepareNightMainCarriers();
  bool nightMainReplace(int object,Tyra::StaPipBag* original,bool orderSafe);
  void nightMainReflectUse(int object);
'''
edit(h,'  std::vector<StaticBatch> staticBatches;',header+'  std::vector<StaticBatch> staticBatches;')

mapping=json.loads((LAB/'night-main11-source-review-v1/source-pins.json').read_text(encoding='utf8'))['mapping']
ids=[int(x['hash'],16) for x in mapping]
assert len(ids)==11
q=(OUT/production[1]).read_text(encoding='utf8')
start=q.index('void invertAffine(');end=q.index('\n}  // namespace',q.index('StaPipClipperSpot buildSpotForBag(',start))
spot=q[start:end].replace('invertAffine','nightMainInvertAffine').replace('buildSpotForBag','nightMainBuildSpotForBag')
code='''
// PRIVATE kind29 copied source-identical light builder; no engine behavior change.
namespace {
'''+spot+'''
inline u32 nightMainWord(float x){u32 v;memcpy(&v,&x,4);return v;}
inline void nightMainHashWord(u32& h,u32 x){h=(h^x)*16777619U;}
inline u32 nightMainPosHash(const Vec4* p,unsigned n){u32 h=2166136261U;for(unsigned i=0;i<n;++i){nightMainHashWord(h,nightMainWord(p[i].x));nightMainHashWord(h,nightMainWord(p[i].y));nightMainHashWord(h,nightMainWord(p[i].z));nightMainHashWord(h,nightMainWord(p[i].w));}return h;}
inline u32 nightMainColorHash(const Color* p,unsigned n){u32 h=2166136261U;for(unsigned i=0;i<n;++i)for(unsigned k=0;k<4;++k)nightMainHashWord(h,nightMainWord(p[i].rgba[k]));return h;}
inline bool nightMainInfoSame(const StaPipInfoBag& a,const StaPipInfoBag& b){
 return a.model==b.model && a.shadingType==b.shadingType && a.textureMappingType==b.textureMappingType && a.transformationType==b.transformationType && a.blendingEnabled==b.blendingEnabled && a.antiAliasingEnabled==b.antiAliasingEnabled && a.zTestType==b.zTestType && a.frustumCulling==b.frustumCulling && a.fogDisabled==b.fogDisabled && a.additiveBlendFix==b.additiveBlendFix && a.subtractiveBlendFix==b.subtractiveBlendFix && a.dynLightPick==b.dynLightPick && a.dynLightSkipSlot==b.dynLightSkipSlot && a.spotLit==b.spotLit && a.dateLit==b.dateLit && a.blssProxy==b.blssProxy && a.fullClipChecks==b.fullClipChecks;
}
inline void nightMainInfoKey(std::vector<u32>& k,const StaPipInfoBag& a){
 k.push_back(a.shadingType);k.push_back(a.textureMappingType);k.push_back(a.transformationType);k.push_back(a.blendingEnabled);k.push_back(a.antiAliasingEnabled);k.push_back(a.zTestType);k.push_back(a.frustumCulling);k.push_back(a.fogDisabled);k.push_back(a.additiveBlendFix);k.push_back(a.subtractiveBlendFix);k.push_back(a.dynLightPick);k.push_back(a.dynLightSkipSlot);k.push_back(a.spotLit);k.push_back(a.dateLit);k.push_back(a.blssProxy);k.push_back(a.fullClipChecks);
 for(unsigned i=0;i<16;++i)k.push_back(nightMainWord(a.model->data[i]));
}
inline void nightMainLightKey(std::vector<u32>& k,const RendererCoreSpotLight& l){
 k.push_back(l.enabled);k.push_back(l.point);
 k.push_back(nightMainWord(l.position.x));k.push_back(nightMainWord(l.position.y));k.push_back(nightMainWord(l.position.z));
 k.push_back(nightMainWord(l.direction.x));k.push_back(nightMainWord(l.direction.y));k.push_back(nightMainWord(l.direction.z));
 k.push_back(nightMainWord(l.color.r));k.push_back(nightMainWord(l.color.g));k.push_back(nightMainWord(l.color.b));
 k.push_back(nightMainWord(l.range));k.push_back(nightMainWord(l.cosCutoff));k.push_back(nightMainWord(l.softness));
}
constexpr unsigned long long nightMainIds[11]={'''+','.join(hex(x)+'ULL' for x in ids)+'''};
}

void TerrainGame::prepareNightMainCarriers(){
 if(!NightAblation::producerSelected)return;
 const bool cold=NightAblation::collectCounters;
 if(cold)++NightMainBatch::stat.invocations;
 if(nightMainGeneration!=sceneGeneration){
   nightMainGroups.clear();nightMainGroups.resize(3);nightMainGeneration=sceneGeneration;
   for(unsigned gi=0;gi<3;++gi){auto& g=nightMainGroups[gi];g.first=gi==0?0:gi==1?4:8;g.members=gi<2?4:3;g.scratchKey.reserve(512);
     for(unsigned j=0;j<g.members;++j){const unsigned ord=g.first+j;
       for(int i=0;i<SCENE_OBJECT_COUNT;++i)if(SCENE_OBJECT_ID_TABLES[currentScene][i]==nightMainIds[ord]){g.objects[j]=i;break;}
     }
   }
 }
 for(unsigned gi=0;gi<3;++gi){auto& g=nightMainGroups[gi];g.admitted=false;g.submitted=false;g.checked=false;
   bool allowed=currentScene==0&&!splitPassActive&&!splitSecondPass&&!vuprog::ENABLED&&!vuscript::movesGeometry()&&!engine->renderer.core.blss.wantsProxies();
   for(unsigned j=0;j<g.members;++j){int i=g.objects[j];if(i<0||i>=SCENE_OBJECT_COUNT){allowed=false;continue;}
     auto& o=runtimeObjects[i];const auto& d=o.data;
     if(d.type!=0||d.model!=-1||d.collision!=2||d.physics||d.dynLit||d.drawDistance!=0||!d.reflected||d.usable||o.wantsMatrixPath||objectBatchOf[i]!=-1||d.material!=(gi<2?2:(j<2?3:4)))allowed=false;
     if(i!=111+int(g.first+j))allowed=false; // exact contiguous reviewed runs
     if(g.ready&&(o.dirty||objectGeometry[i].matrixMode||objectGeometry[i].parts.size()!=1)){g.demoted=true;if(cold)++NightMainBatch::stat.dirtyDemotions;}
   }
   // Reconcile before any reflection can consume dirty. Demotion never frees
   // merged storage; it remains owned until original scene-generation reset.
   if(g.ready)for(unsigned j=0;j<g.members;++j){int i=g.objects[j];if(i<0||objectGeometry[i].parts.size()!=1)continue;const GeoPart& p=objectGeometry[i].parts[0];
     if(p.vertices.stamp()!=g.posStamp[j]||p.colors.stamp()!=g.colorStamp[j]||!p.bag||p.bag->bboxVersion!=g.bboxStamp[j])g.demoted=true;
   }
   if(!allowed)g.demoted=true;
   if(!g.ready&&!g.demoted){
     // Both runtime arms prepare the same original individually owned carriers.
     for(unsigned j=0;j<g.members;++j){int i=g.objects[j];if(runtimeObjects[i].dirty||objectGeometry[i].parts.empty())rebuildObjectGeometry(i);}
     for(unsigned j=0;j<g.members;++j){const auto& og=objectGeometry[g.objects[j]];if(og.matrixMode||og.parts.size()!=1){allowed=false;break;}const GeoPart& p=og.parts[0];
       if(!p.bag||!p.infoBag||!p.colorBag||p.translucent||p.lodHidden||p.stripRun||p.envBag||p.aoBag||p.emisBag||p.bag->lighting||p.bag->texture||p.bag->billboard||p.infoBag->model!=&model||p.infoBag->transformationType!=TyraMVP||p.infoBag->zTestType!=PipelineZTest_Standard||p.infoBag->additiveBlendFix||p.infoBag->subtractiveBlendFix||p.infoBag->dateLit||p.bag->count==0||p.bag->count%3||p.vertices.size()!=p.bag->count||p.colors.size()!=p.bag->count||p.colorBag->many!=p.colors.data()){allowed=false;break;}
       for(unsigned v=0;v<p.colors.size();++v)if(p.colors[v].a!=128.0F)allowed=false;
       if(j&&!nightMainInfoSame(*objectGeometry[g.objects[0]].parts[0].infoBag,*p.infoBag))allowed=false;
       for(unsigned k=0;k<16;++k)if(model.data[k]!=(k%5==0?1.0F:0.0F))allowed=false;
     }
     if(!allowed)g.demoted=true;
     else{g.info=std::make_unique<StaPipInfoBag>(*objectGeometry[g.objects[0]].parts[0].infoBag);g.owned.colorBag=std::make_unique<StaPipColorBag>();g.owned.bag=std::make_unique<StaPipBag>();
       for(unsigned j=0;j<g.members;++j){const GeoPart& p=objectGeometry[g.objects[j]].parts[0];g.offset[j]=g.owned.vertices.size();g.count[j]=p.bag->count;g.posStamp[j]=p.vertices.stamp();g.colorStamp[j]=p.colors.stamp();g.bboxStamp[j]=p.bag->bboxVersion;
         g.owned.vertices.insert(g.owned.vertices.end(),p.vertices.begin(),p.vertices.end());g.owned.colors.insert(g.owned.colors.end(),p.colors.begin(),p.colors.end());
       }
       g.owned.bag->info=g.info.get();g.owned.bag->color=g.owned.colorBag.get();g.owned.bag->texture=nullptr;g.owned.bag->lighting=nullptr;
       g.owned.vertices.bind(g.owned.bag);g.owned.colors.bind(g.owned.colorBag);g.owned.bag->bboxVersion=++g_bboxStamp;
       pinPackageSize({g.owned.bag.get()},0);g.bounds=CoreBBox(g.owned.vertices.data(),g.owned.bag->count);g.ready=true;
     }
   }
   if(cold){auto& gs=NightMainBatch::group[gi];gs.first=g.first;gs.members=g.members;gs.ready=g.ready;gs.demoted=g.demoted;gs.count=g.ready?g.owned.bag->count:0;
     ++NightMainBatch::stat.sourceGroups;NightMainBatch::stat.sourceMembers+=g.members;if(g.demoted)++NightMainBatch::stat.demotedGroups;
     for(unsigned j=0;j<g.members;++j){unsigned ord=g.first+j;auto& w=NightMainBatch::member[ord];w.object=g.objects[j]<0?~0U:g.objects[j];w.idLo=u32(nightMainIds[ord]);w.idHi=u32(nightMainIds[ord]>>32);w.group=gi;
       int i=g.objects[j];if(i<0||objectGeometry[i].parts.size()!=1)continue;const GeoPart& p=objectGeometry[i].parts[0];w.ready=p.bag&&p.bag->count<=p.vertices.size()&&p.bag->count<=p.colors.size();if(!w.ready)continue;++NightMainBatch::stat.reflectionCarriers;w.count=p.bag->count;
       w.positionHash=nightMainPosHash(p.vertices.data(),w.count);w.colorHash=nightMainColorHash(p.colors.data(),w.count);
       if(g.ready){w.copiedPositionHash=nightMainPosHash(g.owned.vertices.data()+g.offset[j],g.count[j]);w.copiedColorHash=nightMainColorHash(g.owned.colors.data()+g.offset[j],g.count[j]);
         const auto& cv=static_cast<const BagArray<Vec4>&>(g.owned.vertices);const auto& cc=static_cast<const BagArray<Color>&>(g.owned.colors);
         if(g.count[j]!=w.count)++NightMainBatch::stat.geometryMismatches;
         for(unsigned v=0;v<g.count[j]&&v<w.count;++v){bool mismatch=false;const Vec4& a=p.vertices[v];const Vec4& b=cv[g.offset[j]+v];
           mismatch=nightMainWord(a.x)!=nightMainWord(b.x)||nightMainWord(a.y)!=nightMainWord(b.y)||nightMainWord(a.z)!=nightMainWord(b.z)||nightMainWord(a.w)!=nightMainWord(b.w);
           for(unsigned k=0;k<4;++k)mismatch|=nightMainWord(p.colors[v].rgba[k])!=nightMainWord(cc[g.offset[j]+v].rgba[k]);
           ++NightMainBatch::stat.geometryCompared;++NightMainBatch::stat.verticesCompared;NightMainBatch::stat.colorBytesCompared+=16;if(mismatch)++NightMainBatch::stat.geometryMismatches;
         }
       }
     }
     if(g.ready)gs.geometryHash=nightMainPosHash(g.owned.vertices.data(),gs.count)^nightMainColorHash(g.owned.colors.data(),gs.count);
   }
 }
}

void TerrainGame::nightMainReflectUse(int object){
 if(!NightAblation::collectCounters)return;
 NightMainBatch::clearPending();
 for(unsigned gi=0;gi<nightMainGroups.size();++gi){const auto& g=nightMainGroups[gi];for(unsigned j=0;j<g.members;++j)if(g.objects[j]==object){NightMainBatch::pendingGroup=gi;NightMainBatch::pendingMember=g.first+j;NightMainBatch::pendingReflection=true;}}
}

bool TerrainGame::nightMainReplace(int object,StaPipBag* original,bool orderSafe){
 if(!NightAblation::producerSelected)return false;
 const bool cold=NightAblation::collectCounters;
 for(unsigned gi=0;gi<nightMainGroups.size();++gi){auto& g=nightMainGroups[gi];unsigned member=0;while(member<g.members&&g.objects[member]!=object)++member;if(member==g.members)continue;
   bool admitted=g.ready&&!g.demoted&&orderSafe&&!ENV_PROBE_REFLECTED&&!splitPassActive&&!splitSecondPass&&!vuprog::ENABLED&&!vuscript::movesGeometry()&&!engine->renderer.core.blss.wantsProxies();
   const bool firstVisit=!g.checked;g.checked=true;
   if(admitted&&firstVisit&&member==0){
     auto& key=g.scratchKey;key.clear();auto& rc=engine->renderer.core;
     // The first main view owns the definitive main-pass info after shadow
     // setup; secondary carrier preparation may have preceded that setup.
     if(!g.keyInitialized&&objectGeometry[g.objects[0]].parts.size()==1&&objectGeometry[g.objects[0]].parts[0].infoBag)*g.info=*objectGeometry[g.objects[0]].parts[0].infoBag;
     const auto& vp=rc.renderer3D.getViewProj();for(unsigned k=0;k<16;++k)key.push_back(nightMainWord(vp.data[k]));
     const auto* planes=rc.renderer3D.frustumPlanes.getAll();for(unsigned k=0;k<6;++k){key.push_back(nightMainWord(planes[k].normal.x));key.push_back(nightMainWord(planes[k].normal.y));key.push_back(nightMainWord(planes[k].normal.z));key.push_back(nightMainWord(planes[k].distance));}
     key.push_back(rc.dynLightCount);nightMainLightKey(key,rc.spot);for(unsigned k=0;k<rc.dynLightCount&&k<RendererCore::DYN_LIGHTS_MAX;++k)nightMainLightKey(key,rc.dynLights[k]);
     key.push_back(rc.fog.enabled);key.push_back(nightMainWord(rc.fog.color.r));key.push_back(nightMainWord(rc.fog.color.g));key.push_back(nightMainWord(rc.fog.color.b));key.push_back(nightMainWord(rc.fog.start));key.push_back(nightMainWord(rc.fog.end));key.push_back(nightMainWord(rc.fog.scale));key.push_back(nightMainWord(rc.fog.offset));
     for(unsigned j=0;j<g.members;++j){auto& o=runtimeObjects[g.objects[j]];const auto& og=objectGeometry[g.objects[j]];if(og.parts.size()!=1){admitted=false;break;}const GeoPart& p=og.parts[0];
       if(!p.bag||!p.infoBag||!p.colorBag||p.bag->count>p.vertices.size()||p.bag->count>p.colors.size()){admitted=false;break;}
       if(!o.active||!o.visible||o.dirty||p.bag.get()!=(member==j?original:p.bag.get())||p.lodHidden||p.translucent||p.bag->lighting||p.bag->texture||p.bag->billboard||p.envBag||p.aoBag||p.emisBag||!nightMainInfoSame(*g.info,*p.infoBag))admitted=false;
       key.push_back(p.vertices.stamp());key.push_back(p.colors.stamp());key.push_back(p.bag->bboxVersion);key.push_back(p.bag->count);key.push_back(p.bag->packageSize);nightMainInfoKey(key,*p.infoBag);
       if(coarseObjectOutside(g.objects[j])||occlusionHiddenObject(g.objects[j])||objectMayBlend(g.objects[j]))admitted=false;
     }
     if(g.keyInitialized){if(key!=g.key){g.demoted=true;admitted=false;}else admitted=admitted&&g.keyAdmitted;}
     else if(admitted){
       // Prequalify exact stock sphere selection and effective spot filter.
       // The copied builder preserves numerical operation order; inactive
       // light fields are never inspected as a shader-uniform key.
       const RendererCoreSpotLight* chosen=nullptr;bool chosenSet=false;bool active=false;u32 lightKey=0;
       for(unsigned j=0;j<=g.members;++j){const CoreBBox& box=j==g.members?g.bounds:objectGeometry[g.objects[j]].coarseBox;
         const auto& info=j==g.members?*g.info:*objectGeometry[g.objects[j]].parts[0].infoBag;
         if(box.frustumCheck(planes)!=CoreBBoxFrustum::IN_FRUSTUM){admitted=false;break;}
         const Vec4& lo=box[0];const Vec4& hi=box[7];const Vec4 mid((lo.x+hi.x)*.5F,(lo.y+hi.y)*.5F,(lo.z+hi.z)*.5F,1.0F);
         const Vec4 wc=(*info.model)*mid;const float ex=hi.x-lo.x,ey=hi.y-lo.y,ez=hi.z-lo.z;
         const float scale=Math::sqrtNonNegative(info.model->data[0]*info.model->data[0]+info.model->data[1]*info.model->data[1]+info.model->data[2]*info.model->data[2]);
         const float radius=.5F*Math::sqrtNonNegative(ex*ex+ey*ey+ez*ez)*scale;
         const auto* pick=info.dynLightPick?rc.pickDynLight(wc,radius,info.dynLightSkipSlot):nullptr;
         bool on=false;if(info.spotLit){auto s=nightMainBuildSpotForBag(pick?*pick:rc.spot,info.model);on=s.enabled&&!stapipSpotHasNoInfluence(s,box);}
         if(chosenSet&&(pick!=chosen||on!=active)){admitted=false;break;}
         chosen=pick;active=on;chosenSet=true;lightKey=pick==&rc.spot?9:0;for(unsigned li=0;li<rc.dynLightCount;++li)if(pick==&rc.dynLights[li])lightKey=li+1;
       }
       g.key=key;g.keyInitialized=true;g.keyAdmitted=admitted;g.lightKey=lightKey;g.spotEnabled=active;
       u32 ih=2166136261U;std::vector<u32> ik;nightMainInfoKey(ik,*g.info);for(u32 v:ik)nightMainHashWord(ih,v);g.infoHash=ih;
     }
     g.admitted=admitted;
   }else if(firstVisit){g.admitted=false;admitted=false;}
   else admitted=admitted&&g.admitted;
   if(cold){auto& gs=NightMainBatch::group[gi];gs.admitted=g.admitted;gs.demoted=g.demoted;gs.infoHash=g.infoHash;gs.lightKey=g.lightKey;gs.spotEnabled=g.spotEnabled;
     auto& mw=NightMainBatch::member[g.first+member];mw.admitted=admitted;mw.infoHash=g.infoHash;mw.lightKey=g.lightKey;mw.spotEnabled=g.spotEnabled;
   }
   if(admitted&&NightAblation::producerEnabled){
     if(!g.submitted){if(cold){NightMainBatch::pendingGroup=gi;NightMainBatch::pendingMerged=true;}stapip.core.render(g.owned.bag.get());NightMainBatch::clearPending();g.submitted=true;}
     return true;
   }
   if(cold){if(!admitted)++NightMainBatch::stat.eligibilityRefused;NightMainBatch::pendingGroup=gi;NightMainBatch::pendingMember=g.first+member;NightMainBatch::pendingMerged=false;}
   return false;
 }
 return false;
}
'''
c='game/src/gen/game_collision.gen.cpp'
edit(c,'#include "game_runtime.gen.hpp"','#include "game_runtime.gen.hpp"\n#include "renderer/3d/pipeline/static/core/stapip_spot_bounds.hpp"\n#include <cstring>')
edit(c,'namespace Vehicle_playground {','namespace Vehicle_playground {\n'+code)

p='game/src/gen/game_physics.gen.cpp'
edit(p,'  // Dynamic env map ("@sky" materials): render the sky dome into the','  prepareNightMainCarriers(); // before secondary consumers can clear dirty\n\n  // Dynamic env map ("@sky" materials): render the sky dome into the')
edit(p,'        stapip.core.render(part.bag.get());\n        const u32 lpB = lp ? profTicks() : 0;','        if(!nightMainReplace(i,part.bag.get(),!heavyActive||heavyNext>=heavyBags.size()))stapip.core.render(part.bag.get());\n        NightMainBatch::clearPending();\n        const u32 lpB = lp ? profTicks() : 0;')
source=(OUT/p).read_text(encoding='utf8')
old='if (part.bag && !part.lodHidden) stapip.core.render(part.bag.get());'
import re
pattern=r'(for \(GeoPart& part : objectGeometry\[ri\].parts\)\n\s*)'+re.escape(old)
source,nchanged=re.subn(pattern,r'\1if (part.bag && !part.lodHidden) {nightMainReflectUse(ri);stapip.core.render(part.bag.get());NightMainBatch::clearPending();}',source)
assert nchanged==2,nchanged
(OUT/p).write_bytes(source.encode())
core=production[0]
edit(core,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
edit(core,'  const u32 prepareStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;','  // PRIVATE kind29 cold actual producer acceptance, after original main cull.\n  NightMainBatch::accepted();\n  const u32 prepareStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;')

for n in m['files']:m['files'][n]=sha(OUT/n)
m['frozen']=False
(OUT/'draft-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode())
review={'status':'DRAFT_NOT_FROZEN','kind':29,'sourceFiles':501,'baseManifestSha256':sha(BASE/'target-source-manifest.json'),'restoredObjectRouteFiles':restored,'productionRestoredFiles':production,'productionBaseSha256':productionBaseSha,'draftEngineSourceSha256':{n:sha(OUT/n) for n in production},'sourceProtocol':{'aggregate':fields,'group':groupfields,'member':memberfields},'baseChoices':'Clone frozen companion source/assets; restore prior object-route generated/debug files to remove kind24 census. Restore current production core/qrenderer/bbox cacher; Core adds only cold accepted annotation. Preserve fixed-hour mood/sky retint, original waits/programs. No engine class fields changed; generated game class grows and target ABI remains unqualified. No TEX1 owned candidate combined.','candidateScope':'Private separately owned main-only groups, raw original carriers copied; original objectBatchOf unchanged. No new Count reads/waits. Group arrays retained until original scene generation reset. Full night allsampler1/tableOn; producerTiming false. Runtime admissions conditional.','unqualified':['Native compile/ABI','Output/lighting/clip equivalence','Actual positive admissions','Parent independent source review','Lifetime across runtime scene/split changes'],'commonFootprintUnpriced':True,'commonSetupReflectionKeyDifference':'Initial early carrier preparation consumes dirty before shared-env content key. Both arms pay same preparation; no claim initial production key/cadence is byte-identical. Steady state contrast requires setup outside priced windows.'}
(OUT/'draft-review.json').write_bytes((json.dumps(review,indent=2)+'\n').encode())
print('kind29 draft prepared; 501 files; unfrozen; no build/runtime')
