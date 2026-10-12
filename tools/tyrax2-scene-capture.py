"""A02b: nine complete StaPip frame ledgers and actual clipped-input witnesses."""
import importlib.util
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
out,source=map(lambda s:Path(s).resolve(),sys.argv[1:3])
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-road-trace.py'),str(out),str(source)],
               check=True,env={**os.environ,'PYTHONUTF8':'1'})
engine,game=out/'tyra/engine',out/'game'
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)

def edit(p,old,new,count=1):
    text=p.read_text(encoding='utf-8-sig');assert text.count(old)==count,(p,old,text.count(old))
    p.write_bytes(text.replace(old,new).encode())

h=engine/'inc/debug/motion_probe.hpp'
edit(h,'inline constexpr U phaseCount=3;','inline constexpr U phaseCount=6;')
edit(h,'constexpr U modes[]={0,6,0};','constexpr U modes[]={0,6,0,0,6,0};')
edit(h,'inline void add(Counter c,U n=1) {',
     (ROOT/'tools/tyrax2-scene-capture.hpp').read_text()+'\ninline void add(Counter c,U n=1) {\n  SceneCapture::note(c,n);')
edit(h,'if(sample && (c==ProbeCapture || c==Paint || c==CaptureBlocked || c>=EnvEligible)) counters[c]+=n;',
     'if(enabled || (sample && (c==ProbeCapture || c==Paint || c==CaptureBlocked || c>=EnvEligible))) counters[c]+=n;')
edit(h,'  RoadTrace::open();','  RoadTrace::open();SceneCapture::open();')
edit(h,'  RoadTrace::begin();','  RoadTrace::begin();SceneCapture::begin();\n  enabled=sample && SceneCapture::active();')
edit(h,'    RoadTrace::save(size-1);','    RoadTrace::save(size-1);SceneCapture::save();')
edit(h,'    RoadTrace::dump();','    RoadTrace::dump();SceneCapture::dump();')
edit(h,'if(offset==gasStart+719) { finishPending=true; }', '''if(phase<3 ? offset==gasStart+719 : current.z>=108.0F || size==1200) {
      finishPending=true;if(phase>=3 && current.z<108.0F)valid=false;
    }''')
bagh=engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp'
edit(bagh,'  unsigned diagnosticRoadId = 0;', '  unsigned auditOwner=0,auditId=0,auditPass=0;\n  unsigned diagnosticRoadId = 0;')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'  MotionProbe::RoadTrace::Bag roadRecord(',
     '  MotionProbe::SceneCapture::Bag sceneRecord(bag,MotionProbe::RoadTrace::view);\n  MotionProbe::RoadTrace::Bag roadRecord(')
edit(core,'    packager.setClipObjectSpacePlanes(clipObjectSpacePlanes);',
     '    MotionProbe::SceneCapture::planes(clipObjectSpacePlanes);\n    packager.setClipObjectSpacePlanes(clipObjectSpacePlanes);')
q=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
for name in ('Cull','Clip'):
    edit(q,'  MotionProbe::add(MotionProbe::'+name+'Chunks);',
         '  MotionProbe::SceneCapture::dispatch(buffer,'+str(name=='Clip').lower()+');\n  MotionProbe::add(MotionProbe::'+name+'Chunks);')
vehicles=game/'src/gen/game_vehicles.gen.cpp'
edit(vehicles,'inThrottle=0.0F; inBrake=0.0F;',
     'inThrottle=MotionProbe::phase>=3 && MotionProbe::gas()?1.0F:0.0F; inBrake=0.0F;')
edit(vehicles,'inSteer=0.0F; inHand=1; inNos=0;',
     'inSteer=0.0F; inHand=MotionProbe::phase>=3 && MotionProbe::gas()?0:1; inNos=0;')
edit(vehicles,'      if(MotionProbe::gas()) {','      if(MotionProbe::phase<3 && MotionProbe::gas()) {')
edit(vehicles,'    submitHeavy(c.bag.get());',
     '    submitHeavy(MotionProbe::SceneCapture::tag(c.bag.get(),2,unsigned(&c-procChunks.data())+1,1));')
collision=game/'src/gen/game_collision.gen.cpp'
edit(collision,'    submitHeavy(b.bag.get());',
     '    submitHeavy(MotionProbe::SceneCapture::tag(b.bag.get(),3,unsigned(&b-staticBatches.data())+1,1));')
physics=game/'src/gen/game_physics.gen.cpp'
for expr,ps in [('ch.bag',1),('lp.bag',2),('ch.aoBag',3),('ch.gsBag',4),('ch.emisBag',5)]:
    edit(physics,'renderWorldBag('+expr+'.get());',
         'renderWorldBag(MotionProbe::SceneCapture::tag('+expr+'.get(),1,unsigned(ch.cz*65536+ch.cx),'+str(ps)+'));')
# Sky and procedural/effect bags retain explicit method ownership where no stable chunk ID is available.
for p in (game/'src').rglob('*.cpp'):
    text=p.read_text(encoding='utf-8-sig')
    text=text.replace('renderWorldBag(skyDome.bag.get());','renderWorldBag(MotionProbe::SceneCapture::tag(skyDome.bag.get(),7,0,1));')
    if text!=p.read_text(encoding='utf-8-sig'):p.write_bytes(text.encode())
scene=game/'src/gen/game_scene.gen.cpp'
if '#include "debug/motion_probe.hpp"' not in scene.read_text():
    scene.write_bytes(b'#include "debug/motion_probe.hpp"\n'+scene.read_bytes())
edit(scene,'void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {', '''void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {
  if(MotionProbe::SceneCapture::active()) {
    unsigned kind=4,pass=0;
    for(int k=0;k<VEHICLE_COUNT;++k)if(VEHICLES[k].object==owner)kind=5;
    if(owner>=0 && owner<int(objectGeometry.size()))for(auto& p:objectGeometry[owner].parts){
      if(p.bag.get()==bag)pass=1;else if(p.aoBag.get()==bag)pass=3;
      else if(p.emisBag.get()==bag)pass=5;else if(p.envBag.get()==bag)pass=6;
    }
    MotionProbe::SceneCapture::tag(bag,kind,unsigned(owner),pass);
  }''')
for method,owner,ps in [('renderProcChunks',8,1),('renderSkyBodies',7,10),('renderStarField',7,10),
    ('renderReflectionProxy',4,11),('renderVehicleWheels',5,8),('renderVehicleGlass',5,9),
    ('renderVehicleSmoke',6,13),('renderVehicleSkids',6,13),('renderVehicleGlow',6,13),
    ('renderVehicleDebris',6,13),('renderVehicleLampGlow',6,13),
    ('renderHighlightHull',6,13),('renderOutlineShells',6,13),
    ('renderShadowDecals',9,7),('updateAndRenderBlobShadows',9,7),('renderProjShadows',9,7),
    ('updateAndRenderLightPools',10,12),('updateAndRenderLightBeams',10,12)]:
    hits=[p for p in (game/'src').rglob('*.cpp') if ('void TerrainGame::'+method+'(') in
          re.sub(r'/\*[\s\S]*?\*/|//[^\n]*','',p.read_text())]
    assert len(hits)==1,(method,hits)
    p=hits[0];text=p.read_text();first,last=quiet.body_span(text,'TerrainGame::'+method)
    text=text[:first+1]+'\n  MotionProbe::SceneCapture::OwnerScope sceneOwner('+str(owner)+','+str(ps)+');'+text[first+1:]
    if '#include "debug/motion_probe.hpp"' not in text:text='#include "debug/motion_probe.hpp"\n'+text
    p.write_bytes(text.encode())
(out/'scene-capture-manifest.json').write_text(json.dumps(dict(
    experiment='A02b-scene-lineage',phases=['turn-off','turn-on','turn-off','drive-off','drive-on','drive-off'],
    windows={'1':[1300,1336,1337,1576,1577],'4':[1380,1381,1500,1501]},
    scope='StaPip, actual clip inputs; raw bag/source addresses are phase-local identities; no production change',
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
