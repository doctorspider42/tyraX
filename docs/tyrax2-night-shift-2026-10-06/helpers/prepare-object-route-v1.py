from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');base=b/'paused-clock-physical-v1';out=b/'object-route-physical-v1';assert not (out/'target-source-manifest.json').exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text())
for n,d in m['files'].items():
 assert sha(base/n)==d;p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/n,p)
for n in('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n,dirs_exist_ok=True)
def edit(n,old,new):
 p=out/n;s=p.read_text(encoding='utf8');assert s.count(old)==1,(n,old);p.write_bytes(s.replace(old,new).encode())
for n in('night_plan.hpp','night_runtime.hpp'):
 p=out/'tyra/engine/inc/debug'/n;p.write_bytes(p.read_text().replace('kind==20','kind==22').encode())
n='tyra/engine/inc/debug/night_runtime.hpp';edit(n,'NightAblation::producerTiming=false;if', 'NightAblation::producerTiming=NightAblation::producerSelected&&NightAblation::producerEnabled&&index<5400&&o>=800&&o<1120;if')
shutil.copyfile(b/'paused-clock-production-vehicle-v1/game/inc/daynight.gen.hpp',out/'game/inc/daynight.gen.hpp');shutil.copyfile(Path('F:/Projects/tyra-editor/examples/vehicle-playground/src/scripts/district_mood.cpp'),out/'game/src/scripts/district_mood.cpp')
n='game/src/gen/game_physics.gen.cpp';edit(n,'    NightAblation::producerCount(1,0);\n    if(retintSkyDomeColors())NightAblation::producerCount(1,1);\n    else{NightAblation::producerCount(1,4);buildSkyDome();}','    if(!retintSkyDomeColors())buildSkyDome();')
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(n,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
edit(n,'  auto* objectDataPacket = packets[context];','''  auto* objectDataPacket = packets[context];
  NightAblation::ProducerScope routeHead(0);
  const u32 routeHeadQw=NightAblation::collectCounters?packet2_get_qw_count(objectDataPacket):0;
  NightAblation::producerCount(0,1,bag->count);
  NightAblation::producerCount(0,2,bag->lighting?1:0);
  NightAblation::producerCount(0,3,objectDataPending?1:0);''')
edit(n,'  bool spotActive = false;','''  if(NightAblation::collectCounters)NightAblation::producerCount(0,4,packet2_get_qw_count(objectDataPacket));
  routeHead.stop();
  NightAblation::ProducerScope routeSpot(1);
  const u32 routeSpotQw=NightAblation::collectCounters?packet2_get_qw_count(objectDataPacket):0;
  NightAblation::producerCount(1,1,bag->lighting?0:1);
  bool spotActive = false;''')
edit(n,'  const bool mayClip = bagMayClip;','''  NightAblation::producerCount(1,2,spotActive?1:0);
  NightAblation::producerCount(1,3,vuCustomEnabled?1:0);
  if(NightAblation::collectCounters)NightAblation::producerCount(1,4,packet2_get_qw_count(objectDataPacket)-routeSpotQw);
  routeSpot.stop();
  NightAblation::ProducerScope routeTail(2);
  const u32 routeTailQw=NightAblation::collectCounters?packet2_get_qw_count(objectDataPacket):0;
  const bool mayClip = bagMayClip;
  NightAblation::producerCount(2,1,mayClip?1:0);
  NightAblation::producerCount(2,2,vu1Clipping&&mayClip?1:0);''')
edit(n,'        frameWaitVif();\n        clipBlockVifQw', '        NightAblation::producerCount(2,4);\n        frameWaitVif();\n        clipBlockVifQw')
edit(n,'  objectDataPending = true;\n}', '  objectDataPending = true;\n  if(NightAblation::collectCounters)NightAblation::producerCount(2,3,packet2_get_qw_count(objectDataPacket)-routeTailQw);\n  routeTail.stop();\n}')
# routeHeadQw is retained only to make packet reset observable in cold records;
# its initial count may exceed the reset count, so do not subtract unsigned counts.
edit(n,'  const u32 routeHeadQw=NightAblation::collectCounters?packet2_get_qw_count(objectDataPacket):0;\n','')
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp';edit(n,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
edit(n,'  qbufferRenderer.setClipperMVP(&mvp);','  NightAblation::ProducerScope routeInfo(3);\n  NightAblation::producerCount(3,1,bag->count);\n  qbufferRenderer.setClipperMVP(&mvp);')
edit(n,'  qbufferRenderer.setInfo(bag->info);\n  TYRA_ATTRIB_ADD', '  qbufferRenderer.setInfo(bag->info);\n  routeInfo.stop();\n  NightAblation::ProducerScope routeFacts(4);\n  TYRA_ATTRIB_ADD')
edit(n,'  TYRA_ATTRIB_MARK(attribReplayStart);','  NightAblation::producerCount(4,1,directRoute?1:0);\n  NightAblation::producerCount(4,2,bag->info->fullClipChecks?1:0);\n  routeFacts.stop();\n  TYRA_ATTRIB_MARK(attribReplayStart);')
for n in m['files']:m['files'][n]=sha(out/n)
(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());original=json.loads((b/'corona24-pricing-physical-v1/target-source-manifest.json').read_text());changes=sorted(n for n,d in m['files'].items()if d!=original['files'][n]);(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=changes,baseManifestSha256=sha(b/'corona24-pricing-physical-v1/target-source-manifest.json'),scopeNames=['uniformHead','spotLocalAndUpload','uniformTail','clipperAndInfo','routeFacts'],candidate='Five disjoint observer scopes, no candidate or added wait. Production fixed-hour script and paused API common. Count only800..1119, cold counters750/1155.',commonCodeFootprintUnpriced=True,existingWaitsRetained=True),indent=2)+'\n').encode())
for old,new in [('paused-clock-build-native-v1.ps1','object-route-build-native-v1.ps1'),('compile-paused-clock-abi-root-v1.py','compile-object-route-abi-root-v1.py'),('audit-paused-clock-native-root-v1.py','audit-object-route-native-root-v1.py')]:
 s=(b/old).read_text().replace('paused-clock-physical-v1','object-route-physical-v1').replace('night-ablation-native-v47','night-ablation-native-v49');(b/new).write_bytes(s.encode())
print('Frozen kind22 object route partition',len(changes))
