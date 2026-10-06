from pathlib import Path
import shutil,json,hashlib,subprocess
root=Path(__file__).parent;parent=root.parent/'tyrax2-closeout-receivers-v1';out=root/'vehicle-night';assert not out.exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
review=parent/'root-runtime-review-kind0-order0.json';r=json.loads(review.read_text(encoding='utf-8-sig'));assert r['sourceFrozenByRoot'] is True
manifest=json.loads((parent/'source-manifest.json').read_text(encoding='utf-8-sig'))
for name,digest in manifest['files'].items():assert sha(parent/name)==digest,name
exclude={'obj','__pycache__','.git'}
def ignored(d,n):return [x for x in n if x in exclude or x.endswith(('.elf','.sym','.log')) or x in ('log.txt','closeout-samples.txt','livedbg.cmd','frame.tga')]
out.mkdir();shutil.copytree(parent/'game',out/'game',ignore=ignored);shutil.copytree(parent/'tyra',out/'tyra',ignore=ignored)
# Both header copies are private. All phases observe the same public Players policy.
header=parent/'closeout.hpp';s=header.read_text(encoding='utf-8');assert 'order<=1&&kind<=2' in s
s=s.replace('order<=1&&kind<=2','order==0&&kind==0').replace('kind==1?unsigned(middle(p)):1u','1u')
anchor='restricted=kind==2||(kind==0&&middle(p));';assert s.count(anchor)==1;s=s.replace(anchor,'restricted=true;')
s=s.replace('namespace Closeout {','namespace Closeout {\n// PRIVATE one-arm motion: Players and sampler ON in every phase; no A/B saving claim.')
(out/'closeout.hpp').write_bytes(s.encode());(out/'tyra/engine/inc/debug/closeout.hpp').write_bytes(s.encode())
p=out/'game/src/terrain_game.cpp';s=p.read_text(encoding='utf-8');start=s.index('  // PRIVATE exact fixed camera,');end=s.index('  if (!menuOwnsPad && flashlightTogglePressed(engine))',start)
# Replace the entire private stationary override/geometry guard, leaving authored camera/physics intact.
replacement='''  // PRIVATE cold actual motion endpoints; no clock reads or hot output.
  if (Closeout::cold()) {
    const bool night = saveValues[Vehicle_playground::DISTRICT_NIGHT_VALUE] >= 0.5F;
    const bool driverGood = vehicleDriver_ >= 0 && vehicleDriver_ < VEHICLE_COUNT;
    if (g_activeScene != 0 || !night || !daynight::g_paused || daynight::g_hour != 0.0F || !driverGood || !Closeout::restricted) NightSampler::valid=false;
    const auto& settings = engine->renderer.core.getSettings();
    unsigned bits[13]{};
    const float values[13] = {cameraPosition.x,cameraPosition.y,cameraPosition.z,cameraLookAt.x,cameraLookAt.y,cameraLookAt.z,
      driverGood?vehicles_[vehicleDriver_].pos[0]:0.0F,driverGood?vehicles_[vehicleDriver_].pos[1]:0.0F,driverGood?vehicles_[vehicleDriver_].pos[2]:0.0F,
      driverGood?vehicles_[vehicleDriver_].speed:0.0F,players[0].x,players[0].y,players[0].z};
    for(unsigned i=0;i<13;++i) memcpy(&bits[i],&values[i],4);
    printf("LOG: CLOSEOUTCONTEXT phase=%u offset=%u players=1 oneArm=1 route=1 scene=%d night=%u paused=%u hourZero=%u vehicleDriver=%d sceneGeneration=%u video=%u display=%u depth=%u width=%u height=%u refreshMilliHz=%u requested=%u positionX=%08x positionY=%08x positionZ=%08x lookX=%08x lookY=%08x lookZ=%08x vehicleX=%08x vehicleY=%08x vehicleZ=%08x vehicleSpeed=%08x playerX=%08x playerY=%08x playerZ=%08x valid=%u\\n",
      unsigned(NightSampler::phase),unsigned(NightSampler::frame%1800),g_activeScene,unsigned(night),unsigned(daynight::g_paused),unsigned(daynight::g_hour==0.0F),vehicleDriver_,unsigned(scriptCtx.sceneGeneration),unsigned(settings.getVideoMode()),unsigned(settings.getDisplayMode()),unsigned(settings.getColorDepth()),unsigned(settings.getWidth()),unsigned(settings.getHeight()),unsigned(settings.getRefreshRate()*1000.0F+0.5F),unsigned(engine->renderer.core.getFramePipeline()),bits[0],bits[1],bits[2],bits[3],bits[4],bits[5],bits[6],bits[7],bits[8],bits[9],bits[10],bits[11],bits[12],unsigned(NightSampler::valid));fflush(stdout);
  }
'''
s=s[:start]+replacement+s[end:];s=s.replace('#include "closeout_geometry.hpp"\n','');p.write_bytes(s.encode())
subprocess.run(['python',str(root/'apply-route.py'),'--fixture',str(out/'game'),'--route','vehicle'],check=True)
(out/'game/bin/closeout.cfg').write_bytes(b'0 0\n')
files={}
for folder in ('game/src','game/inc','tyra/engine/src','tyra/engine/inc'):
 for f in(out/folder).rglob('*'):
  if f.is_file():files[f.relative_to(out).as_posix()]=sha(f)
for name in ('game/vehicle-playground.tyra','game/Makefile','tyra/engine/Makefile','tyra/Makefile.base','game/bin/closeout.cfg'):files[name]=sha(out/name)
proof={'status':'PRIVATE_ONE_ARM_MOTION_SOURCE_PREPARED_NOT_BUILT','parentSourceManifestSha256':sha(parent/'source-manifest.json'),'parentNativeProofSha256':sha(parent/'native-proof.json'),'rootFreezeReviewSha256':sha(review),'files':files,'fixedCameraRemoved':True,'playersAllPhases':True,'samplerAllPhases':True,'geometryAblationDisabled':True,'motionContextActualEndpointBits':True,'addedCountReads':0,'addedWaits':0,'runtimeExecuted':False,'hardwarePerformanceQualified':False,'commonObserverControllerCostUnpriced':True}
(out/'source-review.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(proof['status'])
