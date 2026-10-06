from pathlib import Path
import hashlib,json,shutil,struct,subprocess
root=Path(__file__).parent;repo=Path('F:/Projects/tyra-editor');game=root/'game';engine=root/'tyra/engine'
assert not(root/'source-review.json').exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def edit(p,old,new,count=1):
 s=p.read_text(encoding='utf8');assert s.count(old)==count,(str(p),s.count(old));p.write_bytes(s.replace(old,new,count).encode())
shutil.copyfile(repo/'vendor/tyra/Makefile.base',root/'tyra/Makefile.base')
shutil.copyfile(root/'closeout.hpp',engine/'inc/debug/closeout.hpp')
shutil.copyfile(Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-geometry-v1/night_geometry_helper_draft.hpp'),game/'inc/closeout_geometry.hpp')
edit(root/'tyra/Makefile.base','CFLAGS      += -D_EE -G0 -Wall -O3','CFLAGS      += -D_EE -G0 -Wall -O3 -DTYRA_FRAME_PROFILE=0 -DTYRA_HARDWARE_TRACE=0')
edit(engine/'src/engine.cpp','#include "engine.hpp"','#include "engine.hpp"\n#include "debug/closeout.hpp"\n#include "file/file_utils.hpp"')
edit(engine/'src/engine.cpp','  game->init();','  Closeout::init(FileUtils::fromCwd("closeout.cfg").c_str(), FileUtils::fromCwd("closeout-samples.txt").c_str());\n  game->init();')
edit(engine/'src/engine.cpp','void Engine::realLoop() {','void Engine::realLoop() {\n  Closeout::begin(renderer.core.getStallTotal());')
edit(engine/'src/engine.cpp','  HardwareTrace::endFrame();','  HardwareTrace::endFrame();\n  Closeout::after(renderer.core.getStallTotal());')
p=engine/'src/renderer/core/renderer_core.cpp';s=p.read_text(encoding='utf8');s='#include "debug/night_sampler.hpp"\n'+s
hooks=[('Pipeline','pipelineSequence','pipelineContext'),('Synchronous','0','recordingContext'),('Synthetic','0','0xffffffffu')]
needle='  stallAccum += t1 - t0;';assert s.count(needle)==3
pos=0
for kind,sequence,context in hooks:
 at=s.index(needle,pos);hook=f'  NightSampler::onPresent(t1, NightSampler::PresentKind::{kind}, {sequence}, {context});\n';s=s[:at]+hook+s[at:];pos=at+len(hook)+len(needle)
p.write_bytes(s.encode())
edit(game/'inc/scene_data.hpp','#define PLAYER_ONLY_DYNAMIC_LIGHTS SCENE_PLAYER_ONLY_DYNAMIC_LIGHTS[g_activeScene]','#define PLAYER_ONLY_DYNAMIC_LIGHTS Closeout::restricted')
edit(game/'inc/scene_data.hpp','#pragma once','#pragma once\n#include "debug/closeout.hpp"')
assert 'inline constexpr bool ANY_PLAYER_ONLY_DYNAMIC_LIGHTS = true;'in(game/'inc/scene_data.hpp').read_text(encoding='utf8')
edit(game/'src/terrain_game.cpp','#include "game_runtime.gen.hpp"','#include "game_runtime.gen.hpp"\n#include "closeout_geometry.hpp"\n#include "scripts/district_data.hpp"')
bits=[0,0x4028b436,0xc2a19b64,0,0x3f9a5e32,0xc2949b64];floats=[struct.unpack('<f',struct.pack('<I',v))[0].hex()+'F'for v in bits]
hook='''  // PRIVATE exact fixed camera, after scripts/camera animation and before light consumers.
  cameraPosition = Vec4(%s, %s, %s);
  cameraLookAt = Vec4(%s, %s, %s);
  cameraUp = Vec4(0.0F, 1.0F, 0.0F);
  static CloseoutNightGeometry::State closeoutGeometry;
  const bool closeoutNight = saveValues[Vehicle_playground::DISTRICT_NIGHT_VALUE] >= 0.5F;
  if (Closeout::kind == 2 && !closeoutGeometry.apply(scriptCtx, SCENE_OBJECT_ID_TABLES[g_activeScene], true,
      closeoutNight && Closeout::restricted && daynight::g_paused && daynight::g_hour == 0.0F, Closeout::geometryHidden())) NightSampler::valid = false;
  if (Closeout::cold()) {
    const auto& settings = engine->renderer.core.getSettings();
    unsigned dt,clock; memcpy(&dt,&g_frameDt,4); memcpy(&clock,&g_vuClock,4);
    if (g_activeScene != 0 || !closeoutNight || !daynight::g_paused || daynight::g_hour != 0.0F || vehicleDriver_ < 0) NightSampler::valid=false;
    printf("LOG: CLOSEOUTCONTEXT phase=%%u offset=%%u players=%%u scene=%%d night=%%u paused=%%u hourZero=%%u vehicleDriver=%%d sceneGeneration=%%u video=%%u display=%%u depth=%%u width=%%u height=%%u refreshMilliHz=%%u requested=%%u dtBits=%%08x clockBits=%%08x positionX=00000000 positionY=4028b436 positionZ=c2a19b64 lookX=00000000 lookY=3f9a5e32 lookZ=c2949b64 geometryMatched=%%u geometryReady=%%u geometryRejected=%%u visibleMask=%%u activeMask=%%u savedMask=%%u valid=%%u\\n",
      NightSampler::phase,NightSampler::frame%%1800,unsigned(Closeout::restricted),g_activeScene,unsigned(closeoutNight),unsigned(daynight::g_paused),unsigned(daynight::g_hour==0.0F),vehicleDriver_,unsigned(scriptCtx.sceneGeneration),unsigned(settings.getVideoMode()),unsigned(settings.getDisplayMode()),unsigned(settings.getColorDepth()),unsigned(settings.getWidth()),unsigned(settings.getHeight()),unsigned(settings.getRefreshRate()*1000.0F+0.5F),unsigned(engine->renderer.core.getFramePipeline()),dt,clock,closeoutGeometry.matched,unsigned(closeoutGeometry.ready),unsigned(closeoutGeometry.rejected),closeoutGeometry.visibleMask(scriptCtx),closeoutGeometry.activeMask(scriptCtx),closeoutGeometry.savedMask(),unsigned(NightSampler::valid));fflush(stdout);
  }
'''%tuple(floats)
edit(game/'src/terrain_game.cpp','  if (!menuOwnsPad && flashlightTogglePressed(engine))',hook+'  if (!menuOwnsPad && flashlightTogglePressed(engine))')
allfiles={str(f.relative_to(root)).replace('\\','/'):sha(f)for base in [game/'src',game/'inc',engine/'src',engine/'inc']for f in base.rglob('*')if f.is_file()}
record=dict(status='DRAFT_PRIVATE_CURRENT_HEAD_CLOSEOUT_SOURCE_REVIEW',head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),sourceFiles=allfiles,frozen=False,nativeBuilt=False,deviceAuthorized=False,samplerOrigin='unchanged inherited night_sampler.hpp only; no experimental engine/game imported',changedEngineFiles=['src/engine.cpp','src/renderer/core/renderer_core.cpp'],changedGameFiles=['inc/scene_data.hpp','src/terrain_game.cpp'],addedHeaders=['engine/inc/debug/night_sampler.hpp','engine/inc/debug/closeout.hpp','game/inc/closeout_geometry.hpp'],privateConfig='order kind; order0/1 kind0receivers/1sampler-tax-All/2geometry-Players',cameraBits=[f'{v:08x}'for v in bits],traceCompiledOutAbsoluteCostUnpriced=True,publicCommonCodeCostUnpriced=True)
(root/'source-review.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');print(record['status'])
