"""Private warm-qualified kind29 v2. No build/runtime; frozen v1 unchanged."""
from pathlib import Path
import hashlib,json,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');base=lab/'night-main11-batch-physical-v1';out=lab/'night-main11-batch-physical-v2'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
src=json.loads((base/'target-source-manifest.json').read_text(encoding='utf8'))
assert src['frozen'] and len(src['files'])==501
assert not(out/'target-source-manifest.json').exists()
for n,h in src['files'].items():
    assert sha(base/n)==h,n
    p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/n,p)
for n in ('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n,dirs_exist_ok=True)
def edit(n,a,z):
    p=out/n;s=p.read_text(encoding='utf8');assert s.count(a)==1,(n,a,s.count(a));p.write_bytes(s.replace(a,z).encode())
def replaceAll(n,a,z):
    p=out/n;s=p.read_text(encoding='utf8');assert a in s;(p).write_bytes(s.replace(a,z).encode())
c='game/src/gen/game_collision.gen.cpp';h='game/inc/terrain_game.hpp';a='tyra/engine/inc/debug/night_ablation.hpp'
edit(c,'#include <cstring>','#include <cstring>\n#include "debug/night_sampler.hpp"')
edit(c,'void TerrainGame::prepareNightMainCarriers(){\n if(!NightAblation::producerSelected)return;','void TerrainGame::prepareNightMainCarriers(){\n // PRIVATE v2: initial camera/light/physics settling uses ordinary rendering.\n if(!NightAblation::producerSelected||NightSampler::expectedIndex<600)return;')
edit(c,'bool TerrainGame::nightMainReplace(int object,StaPipBag* original,bool orderSafe){\n if(!NightAblation::producerSelected)return false;','bool TerrainGame::nightMainReplace(int object,StaPipBag* original,bool orderSafe){\n if(!NightAblation::producerSelected||NightSampler::expectedIndex<600)return false;')
edit(h,'    unsigned infoHash=0,lightKey=0;bool spotEnabled=false;','    unsigned infoHash=0,lightKey=0;bool spotEnabled=false;\n    u32 reason=0,firstDiff=~0U;unsigned keyLightEnd=0,keyFogEnd=0;')
edit(a,'inline Stat stat{};inline Group group[3]{};inline Member member[11]{};','struct Why{uint32_t reason=0,firstDiff=~uint32_t(0),keyReady=0,keyAdmitted=0;};\ninline Why why[3]{};\ninline Stat stat{};inline Group group[3]{};inline Member member[11]{};')
edit(a,'inline void reset(){clearPending();stat=Stat{};for(auto& g:group)g=Group{};for(auto& x:member)x=Member{};}','inline void reset(){clearPending();stat=Stat{};for(auto& g:group)g=Group{};for(auto& x:member)x=Member{};for(auto& w:why)w=Why{};}')
edit(a,'inline void emit(unsigned phase,unsigned offset,bool enabled){','inline void emit(unsigned phase,unsigned offset,bool enabled){\n for(unsigned i=0;i<3;++i){const auto& w=why[i];printf("LOG: NIGHTMAINBATCHWHY phase=%u offset=%u enabled=%u group=%u reason=%u firstDiff=%u keyReady=%u keyAdmitted=%u\\n",phase,offset,unsigned(enabled),i,w.reason,w.firstDiff,w.keyReady,w.keyAdmitted);}')
edit(c,'   bool allowed=currentScene==0&&!splitPassActive','   if(currentScene!=0)g.reason|=1;\n   bool allowed=currentScene==0&&!splitPassActive')
edit(c,'if(i<0||i>=SCENE_OBJECT_COUNT){allowed=false;continue;}','if(i<0||i>=SCENE_OBJECT_COUNT){g.reason|=2;allowed=false;continue;}')
edit(c,'{g.demoted=true;if(cold)++NightMainBatch::stat.dirtyDemotions;}','{if(o.dirty){g.reason|=8;if(cold)++NightMainBatch::stat.dirtyDemotions;}else g.reason|=32;g.demoted=true;}')
edit(c,'if(p.vertices.stamp()!=g.posStamp[j]||p.colors.stamp()!=g.colorStamp[j]||!p.bag||p.bag->bboxVersion!=g.bboxStamp[j])g.demoted=true;','if(p.vertices.stamp()!=g.posStamp[j]||p.colors.stamp()!=g.colorStamp[j]||!p.bag||p.bag->bboxVersion!=g.bboxStamp[j]){g.reason|=16;g.demoted=true;}')
replaceAll(c,'if(!allowed)g.demoted=true;','if(!allowed){g.reason|=4;g.demoted=true;}')
edit(c,'gs.count=g.ready?g.owned.bag->count:0;','gs.count=g.ready?g.owned.bag->count:0;\n     NightMainBatch::why[gi]={g.reason,g.firstDiff,unsigned(g.keyInitialized),unsigned(g.keyAdmitted)};')
edit(c,'   const bool firstVisit=!g.checked;g.checked=true;','   if(!orderSafe)g.reason|=1024;\n   if(!g.ready)g.reason|=32;\n   if(ENV_PROBE_REFLECTED||splitPassActive||splitSecondPass||vuprog::ENABLED||vuscript::movesGeometry()||engine->renderer.core.blss.wantsProxies())g.reason|=131072;\n   const bool firstVisit=!g.checked;g.checked=true;')
edit(c,'     key.push_back(rc.fog.enabled);','     const unsigned lightEnd=key.size();\n     key.push_back(rc.fog.enabled);')
edit(c,'     for(unsigned j=0;j<g.members;++j){auto& o=runtimeObjects[g.objects[j]];','     const unsigned fogEnd=key.size();\n     for(unsigned j=0;j<g.members;++j){auto& o=runtimeObjects[g.objects[j]];')
edit(c,'if(og.parts.size()!=1){admitted=false;break;}','if(og.parts.size()!=1){g.reason|=32;admitted=false;break;}')
edit(c,'if(!p.bag||!p.infoBag||!p.colorBag||p.bag->count>p.vertices.size()||p.bag->count>p.colors.size()){admitted=false;break;}','if(!p.bag||!p.infoBag||!p.colorBag||p.bag->count>p.vertices.size()||p.bag->count>p.colors.size()){g.reason|=32;admitted=false;break;}')
edit(c,'       if(!o.active||!o.visible||o.dirty||p.bag.get()!=(member==j?original:p.bag.get())||p.lodHidden||p.translucent||p.bag->lighting||p.bag->texture||p.bag->billboard||p.envBag||p.aoBag||p.emisBag||!nightMainInfoSame(*g.info,*p.infoBag))admitted=false;','       if(!o.active||!o.visible){g.reason|=8192;admitted=false;}\n       if(o.dirty){g.reason|=8;admitted=false;}\n       if(!nightMainInfoSame(*g.info,*p.infoBag)){g.reason|=64;admitted=false;}\n       if(p.bag.get()!=(member==j?original:p.bag.get())||p.lodHidden||p.translucent||p.bag->lighting||p.bag->texture||p.bag->billboard||p.envBag||p.aoBag||p.emisBag){g.reason|=32;admitted=false;}')
edit(c,'       if(coarseObjectOutside(g.objects[j])||occlusionHiddenObject(g.objects[j])||objectMayBlend(g.objects[j]))admitted=false;','       if(coarseObjectOutside(g.objects[j])){g.reason|=16384;admitted=false;}\n       else if(occlusionHiddenObject(g.objects[j])){g.reason|=32768;admitted=false;}\n       else if(objectMayBlend(g.objects[j])){g.reason|=65536;admitted=false;}')
edit(c,'if(key!=g.key){g.demoted=true;admitted=false;}','if(key!=g.key){g.reason|=2048;g.demoted=true;admitted=false;\n       unsigned d=0;while(d<key.size()&&d<g.key.size()&&key[d]==g.key[d])++d;g.firstDiff=d;\n       if(d<16)g.reason|=262144;else if(d<40)g.reason|=524288;else if(d<g.keyLightEnd)g.reason|=1048576;else if(d<g.keyFogEnd)g.reason|=2097152;else if((d-g.keyFogEnd)%37<5)g.reason|=4194304;else g.reason|=8388608;\n     }')
edit(c,'if(box.frustumCheck(planes)!=CoreBBoxFrustum::IN_FRUSTUM){admitted=false;break;}','if(box.frustumCheck(planes)!=CoreBBoxFrustum::IN_FRUSTUM){g.reason|=128;admitted=false;break;}')
edit(c,'if(chosenSet&&(pick!=chosen||on!=active)){admitted=false;break;}','if(chosenSet&&(pick!=chosen||on!=active)){if(pick!=chosen)g.reason|=256;if(on!=active)g.reason|=512;admitted=false;break;}')
edit(c,'g.key=key;g.keyInitialized=true;g.keyAdmitted=admitted;','g.key=key;g.keyInitialized=true;g.keyAdmitted=admitted;g.keyLightEnd=lightEnd;g.keyFogEnd=fogEnd;')
edit(c,'   }else if(firstVisit){g.admitted=false;admitted=false;}','   }else if(firstVisit){if(member!=0)g.reason|=4096;g.admitted=false;admitted=false;}')
edit(c,'     auto& mw=NightMainBatch::member[g.first+member];','     NightMainBatch::why[gi]={g.reason,g.firstDiff,unsigned(g.keyInitialized),unsigned(g.keyAdmitted)};\n     auto& mw=NightMainBatch::member[g.first+member];')
manifest={'frozen':False,'status':'DRAFT_PRIVATE_NIGHT_MAIN11_BATCH_V2','files':{n:sha(out/n) for n in src['files']}}
(out/'draft-source-manifest.json').write_bytes((json.dumps(manifest,indent=2)+'\n').encode())
review={'status':'DRAFT_NOT_FROZEN','sourceFiles':501,'kind':29,'baseManifestSha256':sha(base/'target-source-manifest.json'),'warmup':'Entire private carrier/group preparation and replacement disabled before global NightSampler.expectedIndex600; original rendering used. Delay applies once at startup, not each phase. No guard relaxed.','whyProtocol':['phase','offset','enabled','group','reason','firstDiff','keyReady','keyAdmitted'],'whyMeaning':'Persistent history of refusal/demotion bits; historical keyAdmitted may remain1 after later invalidation. Not exclusive current-refusal classification. firstDiff UINT32_MAX absent key change.','inheritedScope':'Same restricted source-identical carrier copying/light/filter/order/lifetime guards and common unpriced footprint as v1. No game native build, output equivalence or positive activation from preparation.','sourceControlsPending':True}
(out/'draft-review.json').write_bytes((json.dumps(review,indent=2)+'\n').encode())
readme=(base/'README.private-review.md').read_text(encoding='utf8')+'\n## V2 startup settling and sparse reasons\n\nThis separate fixture clones frozen v1; it does not change v1 source, helpers, ELF or runtime proof. Entire private preparation/replacement waits until global sampler frame600; before that, original object rendering continues. Guards are unchanged. WHY rows retain refusal/demotion history (not an exclusive current verdict), first changed exact-key index, and historical captured-key admission. The same candidate ABI, output and native qualifications remain required.\n'
(out/'README.private-review.md').write_bytes(readme.encode())
print('v2 draft prepared501; deferred600; WHY cold only; no build/runtime')
