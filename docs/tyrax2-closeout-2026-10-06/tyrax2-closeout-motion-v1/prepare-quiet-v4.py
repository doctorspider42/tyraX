from pathlib import Path
import json,shutil,hashlib,subprocess
root=Path(__file__).parent;lab=root.parent;parent=lab/'tyrax2-closeout-receivers-v1';out=root/'quiet-v4';assert not out.exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
freeze=parent/'root-runtime-review-v4-kind0-order0.json';assert json.loads(freeze.read_text())['sourceFrozenByRoot'] is True
manifest=parent/'source-manifest-v4.json'
for name,digest in json.loads(manifest.read_text())['files'].items():assert sha(parent/name)==digest,name
exclude={'obj','__pycache__','.git','logs','screenshots','preview','authoring','.vscode'}
def ignored(d,n):return [x for x in n if x in exclude or x.endswith(('.elf','.sym','.log')) or x in ('log.txt','closeout-samples.txt','livedbg.cmd','frame.tga')]
out.mkdir();shutil.copytree(parent/'tyra',out/'tyra',ignore=ignored)
header=out/'tyra/engine/inc/debug/closeout.hpp';s=header.read_text();assert s.count('order<=1&&kind<=2')==1;s=s.replace('order<=1&&kind<=2','order<=1&&kind<=3');assert s.count('restricted=kind==2||(kind==0&&middle(p));')==1;s=s.replace('restricted=kind==2||(kind==0&&middle(p));','restricted=kind==3||kind==2||(kind==0&&middle(p));');header.write_bytes(s.encode())
shutil.copytree(parent/'game',out/'vehicle/game',ignore=ignored)
# Derive the actual motion block from the previously compiled clone, without touching its active/frozen postimage.
v3=(root/'vehicle-night/game/src/terrain_game.cpp').read_text();a=v3.index('  // PRIVATE cold actual motion endpoints;');b=v3.index('  if (!menuOwnsPad && flashlightTogglePressed(engine))',a);motion=v3[a:b]
p=out/'vehicle/game/src/terrain_game.cpp';s=p.read_text();a=s.index('  // PRIVATE exact fixed camera,');b=s.index('  if (!menuOwnsPad && flashlightTogglePressed(engine))',a);s=s[:a]+motion+s[b:];s=s.replace('#include "closeout_geometry.hpp"\n','');p.write_bytes(s.encode())
subprocess.run(['python',str(root/'apply-route.py'),'--fixture',str(out/'vehicle/game'),'--route','vehicle'],check=True)
(out/'vehicle/game/bin/closeout.cfg').write_bytes(b'0 3\n')
showcase=lab/'player-light-public-runtime-v1/fixtures/showcase-gate-players';assert showcase.is_dir();shutil.copytree(showcase,out/'showcase/game',ignore=ignored)
project=out/'showcase/game';document=next(project.glob('*.tyra'));d=json.loads(document.read_text(encoding='utf-8-sig'))
settings=d['settings'];settings.update(dynamicLightReceivers='players',buildProfile='debug',liveLink=False,liveLogic=False,liveDebug=False,timeMachine=False,remotePad=False,inputRecorder=False,keyboardMouse=False,keyboardMousePs2Link=False,eeCrashHandler=False,framePipeline=True,showFps=False,showMemory=False,showProfiler=False,showAreas=False,showCollision=False,multiplayer='off',p2JoinOnStart=False)
for scene in d['scenes']:scene.setdefault('overrides',{}).pop('dynamicLightReceivers',None)
document.write_text(json.dumps(d,indent=2)+'\n',encoding='utf-8')
editor=Path('F:/Projects/tyra-editor/build/tyrax-editor.exe')
for flag in ('--resave','--refresh-gen'):
 r=subprocess.run([str(editor),flag,str(project)],capture_output=True);(out/'showcase'/(flag[2:]+'.stdout')).write_bytes(r.stdout);(out/'showcase'/(flag[2:]+'.stderr')).write_bytes(r.stderr);assert r.returncode==0
def edit(p,old,new):
 s=p.read_text();assert s.count(old)==1,(p,old,s.count(old));p.write_bytes(s.replace(old,new).encode())
edit(project/'inc/scene_data.hpp','#define PLAYER_ONLY_DYNAMIC_LIGHTS SCENE_PLAYER_ONLY_DYNAMIC_LIGHTS[g_activeScene]','#define PLAYER_ONLY_DYNAMIC_LIGHTS Closeout::restricted')
edit(project/'inc/scene_data.hpp','#pragma once','#pragma once\n#include "debug/closeout.hpp"')
assert 'inline constexpr bool ANY_PLAYER_ONLY_DYNAMIC_LIGHTS = true;' in (project/'inc/scene_data.hpp').read_text()
edit(project/'src/main.cpp','  Tyra::Info::writeLogsToFile = !ps2link;','  Tyra::Info::writeLogsToFile = false;  // PRIVATE quiet diagnostic destination')
edit(project/'src/main.cpp','  options.writeLogsToFile = !ps2link;','  options.writeLogsToFile = false;  // PRIVATE quiet diagnostic destination')
def closing(s,start):
 depth=0
 for i in range(start,len(s)):
  if s[i]=='{':depth+=1
  elif s[i]=='}':
   depth-=1
   if depth==0:return i+1
 raise RuntimeError('unclosed telemetry block')
for name in ('game_vehicles.gen.cpp','game_collision.gen.cpp'):
 p=project/'src/gen'/name;s=p.read_text();marker='      static int vehLog = 0;'
 if marker not in s:
  assert 'CONTACT TELEMETRY' not in s and 'LOG: VEH' not in s,name
  continue # Zero-vehicle showcase emits neither telemetry block.
 assert s.count(marker)==1;start=s.index(marker);body=s.index('{',s.index('if (++vehLog >= 25)',start));end=closing(s,body);s=s[:start]+'#if !TYRAX_PRIVATE_QUIET_LOGS\n'+s[start:end]+'\n#endif'+s[end:]
 marker='    // CONTACT TELEMETRY';assert s.count(marker)==1;start=s.index('\n    {',s.index(marker))+1;body=s.index('{',start);end=closing(s,body);s=s[:start]+'#if !TYRAX_PRIVATE_QUIET_LOGS\n'+s[start:end]+'\n#endif'+s[end:];p.write_bytes(s.encode())
# Ordinary authored showcase camera. No vehicle admission or forced day/night context.
gate='''  // PRIVATE actual showcase motion endpoints; no added clocks or waits.
  if (Closeout::cold()) {
    if (g_activeScene != 0 || !Closeout::restricted) NightSampler::valid=false;
    const auto& settings = engine->renderer.core.getSettings();
    unsigned bits[9]{};const float values[9]={cameraPosition.x,cameraPosition.y,cameraPosition.z,cameraLookAt.x,cameraLookAt.y,cameraLookAt.z,players[0].x,players[0].y,players[0].z};
    for(unsigned i=0;i<9;++i) memcpy(&bits[i],&values[i],4);
    printf("LOG: CLOSEOUTCONTEXT phase=%u offset=%u players=1 oneArm=1 route=2 scene=%d vehicleDriver=%d sceneGeneration=%u video=%u display=%u depth=%u width=%u height=%u refreshMilliHz=%u requested=%u positionX=%08x positionY=%08x positionZ=%08x lookX=%08x lookY=%08x lookZ=%08x playerX=%08x playerY=%08x playerZ=%08x valid=%u\\n",
      unsigned(NightSampler::phase),unsigned(NightSampler::frame%1800),g_activeScene,-1,unsigned(scriptCtx.sceneGeneration),unsigned(settings.getVideoMode()),unsigned(settings.getDisplayMode()),unsigned(settings.getColorDepth()),unsigned(settings.getWidth()),unsigned(settings.getHeight()),unsigned(settings.getRefreshRate()*1000.0F+0.5F),unsigned(engine->renderer.core.getFramePipeline()),bits[0],bits[1],bits[2],bits[3],bits[4],bits[5],bits[6],bits[7],bits[8],unsigned(NightSampler::valid));fflush(stdout);
  }
'''
edit(project/'src/terrain_game.cpp','  if (!menuOwnsPad && flashlightTogglePressed(engine))',gate+'  if (!menuOwnsPad && flashlightTogglePressed(engine))')
subprocess.run(['python',str(root/'apply-route.py'),'--fixture',str(project),'--route','gate'],check=True)
(project/'bin').mkdir(exist_ok=True);(project/'bin/closeout.cfg').write_bytes(b'0 3\n')
for label in ('vehicle','showcase'):
 pkg=out/label;files={}
 for base,prefix in ((pkg/'game/src','game/src'),(pkg/'game/inc','game/inc'),(out/'tyra/engine/src','tyra/engine/src'),(out/'tyra/engine/inc','tyra/engine/inc')):
  for f in base.rglob('*'):
   if f.is_file():files[prefix+'/'+f.relative_to(base).as_posix()]=sha(f)
 for name in ('Makefile',next((pkg/'game').glob('*.tyra')).name,'bin/closeout.cfg'):files['game/'+name]=sha(pkg/'game'/name)
 files['tyra/Makefile.base']=sha(out/'tyra/Makefile.base');files['tyra/engine/Makefile']=sha(out/'tyra/engine/Makefile')
 proof={'status':'PRIVATE_QUIET_V4_ONE_ARM_SOURCE_READY_NOT_BUILT','parentManifestSha256':sha(manifest),'parentNativeProofSha256':sha(parent/'native-proof-v4.json'),'parentRootFreezeSha256':sha(freeze),'files':files,'sharedEngine':str(out/'tyra'),'kind':3,'route':1 if label=='vehicle' else 2,'playersAllPhases':True,'samplerAllPhases':True,'authoredCameraRetained':True,'noAddedWaits':True,'noAddedCountReadsBeyondInheritedSampler':True,'hardwarePerformanceQualified':False,'runtimeReleased':False,'commonScaffoldingCostUnpriced':True}
 (pkg/'source-review.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
print('PRIVATE_QUIET_V4_VEHICLE_AND_SHOWCASE_SOURCE_READY_NOT_BUILT')
