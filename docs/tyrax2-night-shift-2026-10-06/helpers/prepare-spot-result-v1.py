from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');base=b/'object-route-physical-v1';out=b/'spot-result-physical-v1';assert not out.exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text())
for n,d in m['files'].items():
 assert sha(base/n)==d;p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/n,p)
for n in('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n)
def edit(n,old,new):
 p=out/n;s=p.read_text(encoding='utf8');assert s.count(old)==1,(n,old);p.write_bytes(s.replace(old,new).encode())
for n in('night_plan.hpp','night_runtime.hpp'):
 p=out/'tyra/engine/inc/debug'/n;p.write_bytes(p.read_text().replace('kind==22','kind==23').encode())
n='tyra/engine/inc/debug/night_runtime.hpp';edit(n,'NightAblation::producerTiming=NightAblation::producerSelected&&NightAblation::producerEnabled&&index<5400&&o>=800&&o<1120;', 'NightAblation::producerTiming=false;')
for n in('stapip_core.cpp','stapip_qbuffer_renderer.cpp'):
 rel='tyra/engine/src/renderer/3d/pipeline/static/core/'+n;shutil.copyfile(b/'paused-clock-physical-v1'/rel,out/rel)
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';edit(n,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
edit(n,'StaPipClipperSpot buildSpotForBag(const RendererCoreSpotLight& spot,', 'StaPipClipperSpot buildSpotForBagOriginal(const RendererCoreSpotLight& spot,')
wrapper='''// PRIVATE complete numerical result memo; no borrowing or selection changes.
struct SpotResultKey { unsigned v[29]; };
struct SpotResultEntry { bool valid=false;SpotResultKey key{};StaPipClipperSpot result{}; };
inline SpotResultEntry spotResults[32]{};
static bool makeSpotResultKey(SpotResultKey& key,const RendererCoreSpotLight& s,const M4x4* m){
  memcpy(key.v,m->data,64);
  const float f[12]={s.position.x,s.position.y,s.position.z,s.direction.x,s.direction.y,s.direction.z,
    s.color.r,s.color.g,s.color.b,s.range,s.cosCutoff,s.softness};
  memcpy(key.v+16,f,48);key.v[28]=s.point?1:0;
  for(unsigned i=0;i<28;++i)if((key.v[i]&0x7f800000u)==0x7f800000u)return false;
  return s.enabled;
}
static unsigned spotResultSlot(const SpotResultKey& k){
  unsigned h=k.v[0]^(k.v[5]*33u)^(k.v[10]*17u)^(k.v[12]*7u)^(k.v[13]*3u)^k.v[14]^(k.v[16]*5u)^k.v[17]^(k.v[18]*11u)^k.v[25]^k.v[28];
  h^=h>>16;h*=0x7feb352du;h^=h>>15;return h&31u;
}
static unsigned spotResultDifference(const StaPipClipperSpot& a,const StaPipClipperSpot& b){
  float av[14]={a.position.x,a.position.y,a.position.z,a.position.w,a.direction.x,a.direction.y,a.direction.z,a.direction.w,a.color[0],a.color[1],a.color[2],a.invRange2,a.cosCut2,a.invSoft};
  float bv[14]={b.position.x,b.position.y,b.position.z,b.position.w,b.direction.x,b.direction.y,b.direction.z,b.direction.w,b.color[0],b.color[1],b.color[2],b.invRange2,b.cosCut2,b.invSoft};
  unsigned d=a.enabled!=b.enabled;for(unsigned i=0;i<14;++i)if(memcmp(av+i,bv+i,4))++d;return d;
}
StaPipClipperSpot buildSpotForBag(const RendererCoreSpotLight& s,const M4x4* m){
  SpotResultKey key{};const bool eligible=s.enabled&&makeSpotResultKey(key,s,m);
  SpotResultEntry* e=eligible?spotResults+spotResultSlot(key):nullptr;
  const bool reused=NightAblation::producerEnabled&&e&&e->valid&&memcmp(e->key.v,key.v,sizeof(key.v))==0;
  StaPipClipperSpot result=reused?e->result:buildSpotForBagOriginal(s,m);
  if(NightAblation::producerEnabled&&e&&!reused){e->key=key;e->result=result;e->valid=true;}
  if(NightAblation::producerSelected&&NightAblation::collectCounters){
    const auto reference=buildSpotForBagOriginal(s,m);unsigned d=spotResultDifference(result,reference);
    NightAblation::producerCount(0,0);NightAblation::producerCount(0,1,eligible?1:0);
    NightAblation::producerCount(0,2,reused?1:0);NightAblation::producerCount(0,3,15);
    NightAblation::producerCount(0,4,d);if(d)NightAblation::valid=false;
  }
  return result;
}
'''
edit(n,'  return out;\n}\n}  // namespace\n\n// Modified by TyraX: the VU1 clipping uniform chain', '  return out;\n}\n'+wrapper+'}  // namespace\n\n// Modified by TyraX: the VU1 clipping uniform chain')
for n in m['files']:m['files'][n]=sha(out/n)
(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());original=json.loads((b/'corona24-pricing-physical-v1/target-source-manifest.json').read_text());changes=sorted(n for n,d in m['files'].items()if d!=original['files'][n]);(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=changes,baseManifestSha256=sha(b/'corona24-pricing-physical-v1/target-source-manifest.json'),candidate='Complete local spot result memo: 29-word content key,32 fixed direct-mapped entries, enabled finite inputs only; collisions overwrite and never approximate. Compare bool plus14 float fields at cold750/1155. Original selection and influence rejection preserved. No extra clocks or waits.',commonCodeFootprintUnpriced=True,existingWaitsRetained=True),indent=2)+'\n').encode())
for old,new in [('paused-clock-build-native-v1.ps1','spot-result-build-native-v1.ps1'),('compile-paused-clock-abi-root-v1.py','compile-spot-result-abi-root-v1.py'),('audit-paused-clock-native-root-v1.py','audit-spot-result-native-root-v1.py')]:
 s=(b/old).read_text().replace('paused-clock-physical-v1','spot-result-physical-v1').replace('night-ablation-native-v47','night-ablation-native-v50');(b/new).write_bytes(s.encode())
print('Frozen kind23 whole local-light result',len(changes))
