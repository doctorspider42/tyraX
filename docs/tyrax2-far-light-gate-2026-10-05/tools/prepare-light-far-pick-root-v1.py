from pathlib import Path
import json,hashlib,shutil
b=Path('F:/Projects/tyrax2-lab-20261001');base=b/'corona24-pricing-physical-v1';out=b/'light-pick-far-physical-v1';assert not out.exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text(encoding='utf8'))
for n,h in m['files'].items():
 assert sha(base/n)==h;p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(base/n,p)
for n in('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n)
changes=[]
def edit(n,a,z,count=1):
 p=out/n;s=p.read_text(encoding='utf8');assert s.count(a)==count,(n,a,s.count(a));p.write_bytes(s.replace(a,z).encode());changes.append(n)
e='tyra/engine/'
edit(e+'inc/debug/night_ablation.hpp','#include <stdint.h>','#include <stdint.h>\n#include <string.h>')
edit(e+'inc/debug/night_ablation.hpp','inline bool valid=true;', '''inline bool valid=true;
// PRIVATE kind12: wide-margin exponent broad phase. No state cache or score changes.
inline bool farPickSelected=false,farPickEnabled=false;
struct FarPickCounter {uint32_t calls=0,candidates=0,rejected=0,applied=0,compared=0,mismatches=0;};
inline FarPickCounter farPickCounter{};
inline bool farLightCandidate(float dx,float dy,float dz,float range,float radius){
 uint32_t x,y,z,r,w;memcpy(&x,&dx,4);memcpy(&y,&dy,4);memcpy(&z,&dz,4);memcpy(&r,&range,4);memcpy(&w,&radius,4);
 if((r>>31)||r==0||((w>>31)&&(w&0x7fffffff)))return false;
 unsigned re=r>>23,we=(w&0x7fffffff)>>23;
 unsigned se=re>we?re:we;
 x&=0x7fffffff;y&=0x7fffffff;z&=0x7fffffff;
 uint32_t mag=x>y?x:y;mag=mag>z?mag:z;unsigned de=mag>>23;
 // Supported domain keeps dominant squared distance normal and summed squares finite.
 // |axis| >= 2^(se-127+3), while range+radius < 2^(se-127+2).
 // At least a factor-two separation; no boundary candidate uses this rejection.
 return se>=80&&se<=175&&de<=180&&de>=se+3;
}
''')
p=out/(e+'src/renderer/core/renderer_core.cpp');s=p.read_text(encoding='utf8');start=s.index('  // Score = luminance',s.index('RendererCore::pickDynLight('));end=s.index('\n}\n',start);body=s[start:end];a='    float d = Math::sqrtNonNegative(dx * dx + dy * dy + dz * dz) - worldRadius;';assert body.count(a)==1;body=body.replace(a,'''    if(fast && NightAblation::collectCounters)++NightAblation::farPickCounter.candidates;
    if(fast && NightAblation::farLightCandidate(dx,dy,dz,l->range,worldRadius)){
      if(NightAblation::collectCounters){++NightAblation::farPickCounter.rejected;if(NightAblation::farPickEnabled)++NightAblation::farPickCounter.applied;}
      continue;
    }
'''+a)
replacement='  const auto choose=[&](bool fast)->const RendererCoreSpotLight* {\n'+body+'\n  };\n  const auto* result=choose(NightAblation::farPickEnabled);\n  if(NightAblation::farPickSelected&&NightAblation::collectCounters){\n    auto& c=NightAblation::farPickCounter;++c.calls;++c.compared;\n    const auto* other=choose(!NightAblation::farPickEnabled);\n    if(result!=other){++c.mismatches;NightAblation::valid=false;}\n  }\n  return result;'
s=s[:start]+replacement+s[end:];p.write_bytes(s.encode());changes.append(e+'src/renderer/core/renderer_core.cpp')
edit(e+'inc/debug/night_plan.hpp','c.kind==7||c.kind==9','c.kind==7||c.kind==9||c.kind==12',count=2)
edit(e+'inc/debug/night_plan.hpp','return c.kind==9 ||','return c.kind==9 || c.kind==12 ||')
edit(e+'inc/debug/night_runtime.hpp','NightAblation::poolTableSelected=planConfig.kind==7||planConfig.kind==9;','NightAblation::farPickSelected=planConfig.kind==12;NightAblation::farPickEnabled=NightAblation::farPickSelected&&((p==1)!=(planConfig.order==1));printf("LOG: NIGHTFARPICKPHASE phase=%u enabled=%u\\n",p,unsigned(NightAblation::farPickEnabled));NightAblation::poolTableSelected=planConfig.kind==7||planConfig.kind==9||planConfig.kind==12;')
edit(e+'inc/debug/night_runtime.hpp','(planConfig.kind==7||planConfig.kind==9)?1u:0u','(planConfig.kind==7||planConfig.kind==9||planConfig.kind==12)?1u:0u')
edit(e+'inc/debug/night_runtime.hpp','NightAblation::coronaCounter=NightAblation::CoronaCounter{};','NightAblation::coronaCounter=NightAblation::CoronaCounter{};NightAblation::farPickCounter=NightAblation::FarPickCounter{};')
edit(e+'inc/debug/night_runtime.hpp','if(o==750||o==1155){const auto& cc=','if(o==750||o==1155){const auto& fc=NightAblation::farPickCounter;printf("LOG: NIGHTFARPICKGATES phase=%u offset=%u enabled=%u calls=%u candidates=%u rejected=%u applied=%u compared=%u mismatches=%u\\n",p,o,unsigned(NightAblation::farPickEnabled),fc.calls,fc.candidates,fc.rejected,fc.applied,fc.compared,fc.mismatches);const auto& cc=')
for n in m['files']:m['files'][n]=sha(out/n)
m['files']=dict(sorted(m['files'].items()));(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=sorted(set(changes)),baseManifestSha256=sha(base/'target-source-manifest.json'),timedExtraClocks=0),indent=2)+'\n').encode());print(out)
