from pathlib import Path
import json,hashlib,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');base=lab/'corona24-pricing-physical-v1';out=lab/'night-producer-probe-physical-v2';assert not out.exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text(encoding='utf8'))
for n,h in m['files'].items():
 assert sha(base/n)==h;p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(base/n,p)
for n in('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n)
changes=set()
def edit(n,old,new,count=1):
 p=out/n;s=p.read_text(encoding='utf8');assert s.count(old)==count,(n,old,s.count(old));p.write_bytes(s.replace(old,new).encode());changes.add(n)
e='tyra/engine/inc/debug/'
edit(e+'night_ablation.hpp','inline bool valid=true;', '''inline bool valid=true;
// PRIVATE producer observer. Common branch/code footprint remains unpriced.
inline bool producerSelected=false,producerEnabled=false,producerTiming=false;
struct ProducerStat {uint64_t ticks=0;uint32_t calls=0,reads=0;};
inline ProducerStat producerStats[5]{};
inline uint32_t producerCold[5][5]{};
inline uint32_t producerTick(){uint32_t v;asm volatile("mfc0 %0, $9":"=r"(v)::"memory");return v;}
inline void producerCount(unsigned s,unsigned k,uint32_t n=1){if(producerSelected&&collectCounters)producerCold[s][k]+=n;}
struct ProducerScope {
 unsigned stage;uint32_t start=0;bool active=false;
 explicit ProducerScope(unsigned s):stage(s){producerCount(s,0);active=producerTiming;if(active)start=producerTick();}
 void stop(){if(active){uint32_t end=producerTick();auto& s=producerStats[stage];s.ticks+=uint32_t(end-start);++s.calls;s.reads+=2;active=false;}}
 ~ProducerScope(){stop();}
};
''')
# collectCounters is declared before valid in this frozen header; verify compile.
edit(e+'night_plan.hpp','c.kind==7||c.kind==9','c.kind==7||c.kind==9||c.kind==13',2)
edit(e+'night_plan.hpp','return c.kind==9 ||','return c.kind==9 || c.kind==13 ||')
edit(e+'night_runtime.hpp','NightAblation::poolTableSelected=planConfig.kind==7||planConfig.kind==9;', 'NightAblation::producerSelected=planConfig.kind==13;NightAblation::producerEnabled=NightAblation::producerSelected&&((p==1)!=(planConfig.order==1));for(auto& ps:NightAblation::producerStats)ps=NightAblation::ProducerStat{};printf("LOG: NIGHTPRODPHASE phase=%u enabled=%u\\n",p,unsigned(NightAblation::producerEnabled));NightAblation::poolTableSelected=planConfig.kind==7||planConfig.kind==9||planConfig.kind==13;')
edit(e+'night_runtime.hpp','(planConfig.kind==7||planConfig.kind==9)?1u:0u','(planConfig.kind==7||planConfig.kind==9||planConfig.kind==13)?1u:0u')
edit(e+'night_runtime.hpp','NightSampler::beginLoop(index,pacing);','NightAblation::producerTiming=NightAblation::producerEnabled&&o>=800&&o<1120;if(NightAblation::collectCounters)for(auto& row:NightAblation::producerCold)for(auto& x:row)x=0;NightSampler::beginLoop(index,pacing);')
edit(e+'night_runtime.hpp','NightAblation::collectCounters=false;','NightAblation::collectCounters=false;NightAblation::producerTiming=false;')
edit(e+'night_runtime.hpp','if(o==750||o==1155){const auto& cc=', 'if(o==750||o==1155){for(unsigned s=0;s<5;++s){auto& c=NightAblation::producerCold[s];auto& t=NightAblation::producerStats[s];printf("LOG: NIGHTPROD phase=%u offset=%u stage=%u enabled=%u calls=%u units1=%u units2=%u units3=%u units4=%u timedCalls=%u reads=%u ticks=%llu\\n",p,o,s,unsigned(NightAblation::producerEnabled),c[0],c[1],c[2],c[3],c[4],t.calls,t.reads,(unsigned long long)t.ticks);}const auto& cc=')
g='game/src/gen/game_lighting.gen.cpp'
edit(g,'      {\n        // THE BUDGET IS SHARED','      {\n        NightAblation::ProducerScope producerScope(0);\n        // THE BUDGET IS SHARED')
p=out/g;s=p.read_text(encoding='utf8');a='              const Vec4& a3 = part.vertices[vi];';assert s.count(a)==2;p.write_bytes(s.replace(a,'              NightAblation::producerCount(0,1);\n'+a,1).encode());changes.add(g)
edit(g,'                b.wVerts.push_back(tri3[k3]);','                NightAblation::producerCount(0,2);\n                b.wVerts.push_back(tri3[k3]);')
edit(g,'      if (!onGeometry) {\n        constexpr int kSub = 4;','      if (!onGeometry) {\n        NightAblation::ProducerScope producerScope(1);\n        constexpr int kSub = 4;')
edit(g,'            sub[ia][ic] = groundSurfaceAt','            NightAblation::producerCount(1,1);\n            sub[ia][ic] = groundSurfaceAt')
edit(g,'            const float h00 = sub[ia * kSub][ic * kSub];','            NightAblation::producerCount(1,2);\n            const float h00 = sub[ia * kSub][ic * kSub];')
edit(g,'                const float d = sub[ia * kSub + sa][ic * kSub + sc] - sheet;','                NightAblation::producerCount(1,3);\n                const float d = sub[ia * kSub + sa][ic * kSub + sc] - sheet;')
edit(g,'  kCoronaV.clear();','  NightAblation::ProducerScope scratchScope(2);\n  kCoronaV.clear();')
edit(g,'  if (keep) {\n    // Resizing stays','  NightAblation::producerCount(2,1,keep?kCoronaV.size():bb.coronaVerts.size());\n  NightAblation::producerCount(2,2,keep?kConeV.size():bb.coneVerts.size());\n  scratchScope.stop();\n  if (keep) {\n    // Resizing stays')
edit(g,'    bool cw = keepWrite(bb.coronaVerts, kCoronaV);','    NightAblation::ProducerScope coronaCommit(3);\n    NightAblation::producerCount(3,1,kCoronaV.size()*sizeof(Vec4)+kCoronaS.size()*sizeof(Vec4)+kCoronaC.size()*sizeof(Color));\n    bool cw = keepWrite(bb.coronaVerts, kCoronaV);')
edit(g,'    if (!bb.coronaVerts.empty()) {\n      bb.coronaVerts.bind','    coronaCommit.stop();\n    NightAblation::producerCount(3,2,cw?1:0);\n    if (!bb.coronaVerts.empty()) {\n      bb.coronaVerts.bind')
edit(g,'    bool nw = keepWrite(bb.coneVerts, kConeV);','    NightAblation::ProducerScope coneCommit(4);\n    NightAblation::producerCount(4,1,kConeV.size()*sizeof(Vec4)+kConeC.size()*sizeof(Color));\n    bool nw = keepWrite(bb.coneVerts, kConeV);')
edit(g,'    if (!bb.coneVerts.empty()) {\n      bb.coneVerts.bind','    coneCommit.stop();\n    NightAblation::producerCount(4,2,nw?1:0);\n    if (!bb.coneVerts.empty()) {\n      bb.coneVerts.bind')
for n in m['files']:m['files'][n]=sha(out/n)
m['files']=dict(sorted(m['files'].items()));(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode())
(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=sorted(changes),baseManifestSha256=sha(base/'target-source-manifest.json'),timedExtraClocks='two per activated scoped producer; enabled only800..1119',scopeNames=['projectedReceiver','terrainHull','beamScratch','coronaCommit','coneCommit'],offCodeFootprintUnpriced=True),indent=2)+'\n').encode());print(out)
