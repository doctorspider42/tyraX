"""Private producer/view attribution, preserving deferred submission ownership."""
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
subprocess.run([sys.executable, str(ROOT/'tools/tyrax2-motion-probe.py'),
                str(out), '--geometry-reuse'], check=True)
engine, game = out/'tyra/engine', out/'game'
spec = importlib.util.spec_from_file_location('quiet', ROOT/'tools/tyrax2-quiet-fixture.py')
quiet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quiet)

def edit(path, old, new, count=1):
    text = path.read_text(encoding='utf-8-sig')
    assert text.count(old) == count, (path, old, text.count(old))
    path.write_bytes(text.replace(old, new).encode())

def include(path):
    if '#include "debug/motion_probe.hpp"' not in path.read_text():
        path.write_bytes(b'#include "debug/motion_probe.hpp"\n'+path.read_bytes())

sites = []
def uncomment(text):
    return re.sub(r'/\*[\s\S]*?\*/|//[^\n]*', lambda m: ' '*len(m[0]), text)

def method(name, code, at_end=False):
    hits = [p for p in (game/'src').rglob('*.cpp')
            if re.search(r'\b(?:void|bool)\s+TerrainGame::'+name+r'\s*\(', uncomment(p.read_text()))]
    assert len(hits) == 1, (name, hits)
    p = hits[0]
    include(p)
    text = p.read_text()
    first, last = quiet.body_span(text, 'TerrainGame::'+name)
    index = last-1 if at_end else first+1
    p.write_bytes((text[:index]+'\n'+code+'\n'+text[index:]).encode())
    sites.append(dict(file=str(p.relative_to(out)), method=name, code=code))

(engine/'inc/debug/motion_probe.hpp').write_bytes((ROOT/'tools/tyrax2-producer-probe.hpp').read_bytes())
for owner, methods in {
    'Terrain': ['renderTerrain'], 'Roads': ['renderRoadChunks'],
    'Batches': ['renderStaticBatches'], 'Procedural': ['renderProcChunks'],
    'Sky': ['renderSkyBodies'],
    'Vehicles': ['renderVehicleWheels', 'renderVehicleGlass'],
    'Effects': ['renderVehicleSmoke', 'renderVehicleSkids', 'renderVehicleGlow',
                'renderVehicleDebris', 'renderVehicleLampGlow', 'renderHighlightHull', 'renderOutlineShells'],
}.items():
    for name in methods:
        method(name, '  MotionProbe::Owner auditOwner(MotionProbe::'+owner+');')
for view, methods in {'Reflection': ['renderObjectProbe', 'renderRtMirror'],
                      'Secondary': ['renderCameraFeed', 'renderPortalView']}.items():
    for name in methods:
        method(name, '  MotionProbe::Camera auditCamera(MotionProbe::'+view+');')
method('renderObjectBag', '''  unsigned auditKind=MotionProbe::Objects;
  for(int i=0;i<VEHICLE_COUNT;++i) if(VEHICLES[i].object==owner) auditKind=MotionProbe::Vehicles;
  if(MotionProbe::producer==MotionProbe::Effects) auditKind=MotionProbe::Effects;
  MotionProbe::Owner auditOwner(auditKind);''')
method('setupVehicles', '''  if(!MotionProbe::driving()) {
    for(int i=0;i<vehicleCount_;++i) {
      runtimeObjects[vehicles_[i].object].active=false;
      runtimeObjects[vehicles_[i].object].visible=false;
      vehicles_[i].active=false;
    }
    vehicleCount_=0; vehicleDriver_=-1;
  }''', at_end=True)
p = game/'inc/terrain_game.hpp'
edit(p, 'std::vector<Tyra::StaPipBag*> heavyBags;',
     'std::vector<Tyra::StaPipBag*> heavyBags;\n  std::vector<unsigned> auditHeavyTags;')
p = game/'src/gen/game_vehicles.gen.cpp'
edit(p, '  if (heavyCollect)\n    heavyBags.push_back(bag);', '''  if (heavyCollect) {
    heavyBags.push_back(bag); auditHeavyTags.push_back(MotionProbe::tag());
    MotionProbe::add(MotionProbe::DeferredPush);
  }''')
edit(p, '  heavyBags.clear();', '  heavyBags.clear(); auditHeavyTags.clear();')
edit(p, '  for (size_t k = 0; k < want; ++k) renderWorldBag(heavyBags[heavyNext++]);', '''  for (size_t k=0;k<want;++k) {
    TYRA_ASSERT(auditHeavyTags.size()==heavyBags.size(), "Attribution queue mismatch");
    MotionProbe::Restore auditRestore(auditHeavyTags[heavyNext]);
    MotionProbe::add(MotionProbe::DeferredPop);
    renderWorldBag(heavyBags[heavyNext++]);
  }''')
capture_hits = 0
for p in (game/'src').rglob('*.cpp'):
    text = p.read_text()
    needle = 'const u32 costSharedEnvStart = costStart();'
    if needle in text:
        capture_hits += text.count(needle)
        text = text.replace(needle, 'MotionProbe::Camera auditReflection(MotionProbe::Reflection);\n    '+needle)
    needle = 'renderWorldBag(skyDome.bag.get());'
    if needle in text:
        text = text.replace(needle, '{ MotionProbe::Owner auditSky(MotionProbe::Sky); '+needle+' }')
    if text != p.read_text():
        p.write_bytes(text.encode())
        include(p)
assert capture_hits == 1, capture_hits
p = game/'src/terrain_game.cpp'
edit(p, '  if(vehicleDriver_>=0 && vehicleDriver_<vehicleCount_) {', '''  if(!MotionProbe::driving()) {
    const float a=float(MotionProbe::angle())*0.01745329252F;
    cameraPosition.set(0.0F,2.2F,-74.3035F);
    cameraLookAt.set(35.0F*sinf(a),2.2F,-74.3035F+35.0F*cosf(a));
    MotionProbe::pose({0,2.2F,-74.3035F,0,a,g_frameDt,0,cameraPosition.x,cameraPosition.y,cameraPosition.z,cameraLookAt.x,cameraLookAt.y,cameraLookAt.z,-1,vehicleCount_});
  }
  scriptCtx.hudSuppressed=true;
  if(vehicleDriver_>=0 && vehicleDriver_<vehicleCount_) {''')
edit(p, '  MotionProbe::renderBegin();', '''  const int auditEnvUsers=g_dynamicEnvUsers;
  if(!MotionProbe::driving()) g_dynamicEnvUsers=0;
  MotionProbe::renderBegin();''')
edit(p, '  engine->renderer.endFrame();',
     '  engine->renderer.endFrame();\n  g_dynamicEnvUsers=auditEnvUsers;')
core = engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core, '  MotionProbe::add(MotionProbe::Bags);',
     '  MotionProbe::CoreScope auditCore;\n  MotionProbe::add(MotionProbe::Bags);')
edit(core, '    if(replayedWhole) MotionProbe::add(MotionProbe::Replay);', '''    if(replayedWhole) {
      MotionProbe::add(MotionProbe::Replay);
      MotionProbe::add(MotionProbe::CullChunks,(bag->count+maxVertCount-1)/maxVertCount);
      MotionProbe::add(MotionProbe::CullVerts,bag->count);
    }''')
p = engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
for name, prefix in [('cull', 'Cull'), ('clip', 'Clip')]:
    text = p.read_text()
    first, last = quiet.body_span(text, 'StaPipQBufferRenderer::'+name)
    body = text[first:last]
    needle = 'if (buffer->size == 0) {\n    return;\n  }'
    assert body.count(needle) == 1, (name, body[:200])
    body = body.replace(needle, needle+'\n  MotionProbe::add(MotionProbe::'+prefix+'Chunks);\n  MotionProbe::add(MotionProbe::'+prefix+'Verts,buffer->size);')
    p.write_bytes((text[:first]+body+text[last:]).encode())
# Reuse exactly the audited wait-site list, without enabling its incompatible trial.
base = (ROOT/'tools/tyrax2-motion-probe.py').read_text()
start = base.index('    wait_sites=[]', base.index('if scope_trial:', base.index('edit(core,')))
end = base.index('\nif breakdown_trial:', start)
exec('\n'.join(line[4:] if line.startswith('    ') else line for line in base[start:end].splitlines()))
(out/'producer-sites.json').write_text(json.dumps(sites, indent=2)+'\n')
(out/'source-manifest.json').write_text(json.dumps({'experiment': 'producer-attribution',
    'files': quiet.source_inputs(out, 'vehicle-playground.tyra')}, indent=2)+'\n')
print(out)
