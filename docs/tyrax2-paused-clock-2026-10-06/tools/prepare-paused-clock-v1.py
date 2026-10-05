from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');base=b/'sky-retint-physical-v1';out=b/'paused-clock-physical-v1';assert not out.exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text(encoding='utf8'))
for n,d in m['files'].items():
 assert sha(base/n)==d;p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/n,p)
for n in ('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n)
changes=set()
def edit(n,old,new,count=1):
 p=out/n;s=p.read_text(encoding='utf8');assert s.count(old)==count,(n,old);p.write_bytes(s.replace(old,new).encode());changes.add(n)
for n in ('night_plan.hpp','night_runtime.hpp'):edit('tyra/engine/inc/debug/'+n,'kind==19','kind==20',2 if n=='night_plan.hpp' else 3) if False else None
for n in ('night_plan.hpp','night_runtime.hpp'):
 p=out/'tyra/engine/inc/debug'/n;s=p.read_text();assert 'kind==19'in s;s=s.replace('kind==19','kind==20');p.write_bytes(s.encode());changes.add(str(p.relative_to(out)).replace('\\','/'))
n='game/inc/daynight.gen.hpp';edit(n,'inline float g_hour = 12.0F;','inline float g_hour = 12.0F;\ninline bool g_paused = false;')
edit(n,'inline void reset(int scene) {','inline void setHour(float hour) { g_hour = wrap24(hour); }\ninline void setPaused(bool paused) { g_paused = paused; }\ninline void reset(int scene) {\n  g_paused = false;')
edit(n,'  if (len > 0.001F) g_hour = wrap24(g_hour + dt * (24.0F / len));','''  const float before=g_hour;
  if (!g_paused && len > 0.001F) g_hour = wrap24(g_hour + dt * (24.0F / len));
  if(NightAblation::producerSelected&&NightAblation::collectCounters){
    unsigned a,z,d;memcpy(&a,&before,4);memcpy(&z,&g_hour,4);memcpy(&d,&dt,4);
    NightAblation::producerCount(0,0);if(g_paused)NightAblation::producerCount(0,1);if(a!=z)NightAblation::producerCount(0,2);
    if(!(dt>0.0F)||!isfinite(g_hour)){NightAblation::producerCount(0,3);NightAblation::valid=false;}
    printf("LOG: NIGHTCLOCK scene=%d paused=%u before=%08x after=%08x dt=%08x\\n",scene,unsigned(g_paused),a,z,d);fflush(stdout);
  }''')
edit(n,'#include <math.h>','#include <math.h>\n#include <string.h>\n#include "debug/night_ablation.hpp"')
n='game/src/scripts/district_mood.cpp';edit(n,'    daynight::g_hour = night ? 0.0F : 12.0F;','    daynight::setHour(night ? 0.0F : 12.0F);\n    daynight::setPaused(NightAblation::producerEnabled);')
n='game/src/gen/game_physics.gen.cpp';p=out/n;s=p.read_text();start=s.index('    NightAblation::producerCount(0,0);');end=s.index('\n  }\n  // Reflective materials',start);s=s[:start]+'''    NightAblation::producerCount(1,0);
    if(retintSkyDomeColors())NightAblation::producerCount(1,1);
    else{NightAblation::producerCount(1,4);buildSkyDome();}'''+s[end:];p.write_bytes(s.encode());changes.add(n)
for n in m['files']:m['files'][n]=sha(out/n)
(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());baseOriginal=json.loads((b/'corona24-pricing-physical-v1/target-source-manifest.json').read_text(encoding='utf8'));allChanges=sorted(n for n,d in m['files'].items()if d!=baseOriginal['files'][n]);fr=dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=allChanges,baseManifestSha256=sha(b/'corona24-pricing-physical-v1/target-source-manifest.json'),candidate='Explicit paused clock advances only while unpaused; evaluates all effects normally. Promoted sky retint in all phases. No scope clocks.',commonCodeFootprintUnpriced=True,existingWaitsRetained=True);(out/'root-source-freeze.json').write_bytes((json.dumps(fr,indent=2)+'\n').encode())
for name in ('sky-retint-build-native-v1.ps1','compile-sky-retint-abi-root-v1.py','audit-sky-retint-native-root-v1.py'):
 s=(b/name).read_text().replace('sky-retint-physical-v1','paused-clock-physical-v1').replace('night-ablation-native-v45','night-ablation-native-v47');new=name.replace('sky-retint','paused-clock');(b/new).write_bytes(s.encode())
print('Private kind20 clock fixture frozen',len(allChanges))
