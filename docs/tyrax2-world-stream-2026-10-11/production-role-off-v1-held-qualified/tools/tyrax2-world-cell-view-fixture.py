"""Broaden qualified A44 terrain GPU gate to four directions and Hybrid2 display."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--ordinary-only',action='store_true')
p.add_argument('--settle-frames',type=int,choices=(0,32),default=0)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
for phase in range(3):
    report=json.loads((a.source/f'world-cell-terrain-emulator-phase{phase}.json').read_text())
    assert report['valid'] and report['elfSha256']==binding['elfSha256']
    assert report['logSha256']==sha(a.source/'emulator/emulator.log')
    for row in report['images']:
        for rel,digest in zip(row['files'],row['hashes']):assert sha(a.source/rel)==digest,rel
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (n.startswith(('tape-','command-','cell-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-terrain-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
def once(text,old,new):
    assert text.count(old)==1,old
    return text.replace(old,new)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
t=physics.read_text()
start=t.index('void TerrainGame::renderCompleteTerrainCheckpoint(){')
end=t.index('void TerrainGame::renderScene() {',start)
body=t[start:end]
body=once(body,'  for(unsigned arm=0;arm<3;++arm){','''
  const Tyra::Vec4 savedLook=cameraLookAt;
  const float dx=savedLook.x-cameraPosition.x,dz=savedLook.z-cameraPosition.z;
  for(unsigned angle=0;angle<4;++angle){
    const float radians=angle*1.5707963267948966F;
    cameraLookAt.x=cameraPosition.x+dx*cosf(radians)-dz*sinf(radians);
    cameraLookAt.z=cameraPosition.z+dx*sinf(radians)+dz*cosf(radians);
    for(unsigned arm=0;arm<3;++arm){''')
body=once(body,'A44ARM phase=%u arm=%u requested=%u submitted=%u fallback=%u',
    'A44VIEWARM phase=%u angle=%u arm=%u requested=%u submitted=%u fallback=%u')
body=once(body,'      MotionProbe::phase,arm,WorldCellComplete::requested,',
    '      MotionProbe::phase,angle,arm,WorldCellComplete::requested,')
body=once(body,'    for(unsigned env=0;env<2;++env){','    for(unsigned env=0;env<3;++env){')
body=once(body,'      if(env){auto* texture=core.envMap.getTexture()->vramResident;', '''
      if(env==2){
        TYRA_ASSERT(core.gs.getFrameBufferCount()==2 && core.gs.getRecordingContext()==0,
                    "A44 display readback requires original Hybrid2");
        fb=core.gs.getPreviousFrameBuffer();address=fb->address;
        width=fb->width;height=fb->height;psm=fb->psm;
        TYRA_ASSERT(psm==2,"A44 missing Hybrid16 display target");
      }
      if(env==1){auto* texture=core.envMap.getTexture()->vramResident;''')
body=once(body,'      const unsigned stage=MotionProbe::phase*3+arm;',
    '      const unsigned stage=MotionProbe::phase*12+angle*3+arm;')
body=once(body,'stage,env?"env":"main");','stage,env==2?"display":env?"env":"main");')
body=once(body,'  WorldCellComplete::active=false;g_frameDt=savedDt;', '''
  }
  cameraLookAt=savedLook;
  WorldCellComplete::active=false;g_frameDt=savedDt;''')
body=once(body,'A44GPU phase=%u arms=3 target=%u complete=1',
    'A44VIEWGPU phase=%u views=4 arms=3 target=%u complete=1')
if a.settle_frames:
    frame='''      core.synchronizeFrame();
      TYRA_ASSERT(core.gs.selectDiagnosticTarget(uint8_t(target)),"A44 pin target failed");
      beamBatchCall=0;
      engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
      engine->renderer.renderer3D.usePipeline(stapip);
      renderScene();engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();'''
    body=once(body,frame,'      renderHeld();')
    body=once(body,'  const Tyra::Vec4 savedLook=cameraLookAt;',
              '  auto renderHeld=[&](){\n'+frame+'\n  };\n  const Tyra::Vec4 savedLook=cameraLookAt;')
    body=once(body,'    for(unsigned arm=0;arm<3;++arm){', '''
    WorldCellComplete::active=false;
    for(unsigned warm=0;warm<32;++warm)renderHeld();
    for(unsigned arm=0;arm<3;++arm){''')
if a.ordinary_only:
    body=once(body,'    WorldCellComplete::active=arm==1;',
              '    WorldCellComplete::active=false;')
    body=once(body,'    if(arm==1)TYRA_ASSERT(',
              '    if(arm==1 && WorldCellComplete::active)TYRA_ASSERT(')
t=t[:start]+body+t[end:];physics.write_text(t)
manifest={'experiment':'A44-terrain-four-view-display','baseElfSha256':binding['elfSha256'],
          'status':'UNQUALIFIED','views':4,'targets':['main','env','display'],
          'movingCandidate':False,'generatorSha256':sha(Path(__file__)),
          'ordinaryOnly':a.ordinary_only,
          'ordinarySettlingFrames':a.settle_frames,
          'limits':['Same end-of-drive position; four camera directions.',
                    'No moving candidate/physical price or full lifetime qualification.']}
(a.out/'world-cell-view-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED four-view main/env/Hybrid2-display gate')
