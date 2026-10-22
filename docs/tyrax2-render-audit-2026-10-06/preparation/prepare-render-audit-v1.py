from pathlib import Path
import shutil,re,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');src=lab/'tyrax2-loop-balance-v1';dst=lab/'tyrax2-render-audit-v1';assert not dst.exists();dst.mkdir()
ignore=shutil.ignore_patterns('obj','__pycache__','.git','*.elf','*.sym','*.log','log.txt','closeout-samples.txt','ps2link.run')
shutil.copytree(src/'tyra',dst/'tyra',ignore=ignore);shutil.copytree(src/'vehicle/game',dst/'vehicle/game',ignore=ignore)
changed=set()
def edit(rel,old,new,count=1):
 p=dst/rel;t=p.read_text(encoding='utf8');assert t.count(old)==count,(rel,old,t.count(old));p.write_bytes(t.replace(old,new).encode());changed.add(rel)
def include(rel):
 p=dst/rel;t=p.read_text(encoding='utf8');p.write_bytes(('#include "debug/render_audit.hpp"\n'+t).encode());changed.add(rel)
def function(rel,name,kind,tag,extra='',expected=1):
 p=dst/rel;t=p.read_text(encoding='utf8');pat=re.compile(r'^[^\n;/]*\b'+re.escape(name)+r'\s*\([^;{}]*?\)(?:\s*const)?\s*\{',re.M);matches=list(pat.finditer(t));assert len(matches)==expected,(rel,name,len(matches))
 for m in reversed(matches):t=t[:m.end()]+'\n  RenderAudit::'+kind+' auditFunction(RenderAudit::'+tag+');\n'+extra+t[m.end():]
 p.write_bytes(t.encode());changed.add(rel)
e='tyra/engine/';game='vehicle/game/'
b=e+'inc/debug/loop_balance.hpp';edit(b,'#include <stdio.h>','#include <stdio.h>\n#include "render_audit.hpp"')
edit(b,'target=ticks()-origin;phase=next;markers|=bit;','U t=ticks();target=t-origin;\n if(bit==2)RenderAudit::begin(current.frame,t);\n if(bit==4)RenderAudit::end(t);\n phase=next;markers|=bit;')
edit(b,'if(outer){outerKind=k;++current.calls[k];start=ticks();}','if(outer){outerKind=k;++current.calls[k];start=ticks();RenderAudit::excludeStart(start);}')
edit(b,'if(outer){U end=ticks();current.waits[owner][kind]+=end-start;}','if(outer){U end=ticks();current.waits[owner][kind]+=end-start;RenderAudit::excludeEnd(end,false);}')
edit(b,'current.pacing[phase]+=e-b;','RenderAudit::excludeStart(existingBegin);RenderAudit::excludeEnd(existingEnd,true);\n current.pacing[phase]+=e-b;')
edit(b,'samples[current.frame%1800-900]=current;active=false;','if(current.enabled)RenderAudit::finish(current.gameEnd-current.renderStart,current.waits[2][0]+current.waits[2][1]+current.waits[2][2],current.pacing[2]);\n else RenderAudit::storeOff(current.frame);\n samples[current.frame%1800-900]=current;active=false;')
c=e+'inc/debug/closeout.hpp';edit(c,'if(mode(p))LoopBalance::write(file,p,i);','if(mode(p)){LoopBalance::write(file,p,i);RenderAudit::write(file,p,i);}')
edit(c,'complete() && LoopBalance::valid','complete() && LoopBalance::valid && RenderAudit::valid')
t=game+'src/terrain_game.cpp'
edit(t,'  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));','  { RenderAudit::Stage audit(RenderAudit::Setup); engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp)); }')
edit(t,'    dayNightTick();','    { RenderAudit::Stage audit(RenderAudit::DayNight); dayNightTick(); }')
edit(t,'    // Depth of field composites right after the 3D scene','    RenderAudit::Stage auditHud(RenderAudit::HudFx);\n    // Depth of field composites right after the 3D scene')
edit(t,'  engine->renderer.endFrame();','  { RenderAudit::Stage audit(RenderAudit::EndFrame); engine->renderer.endFrame(); }')
ph=game+'src/gen/game_physics.gen.cpp';include(ph);function(ph,'TerrainGame::renderScene','Stage','ScenePrep')
edit(ph,'  const u32 costSeq = livedbg::takeRenderCostRequest();','  const u32 costSeq = livedbg::takeRenderCostRequest();\n  RenderAudit::rejectCapture(costSeq);')
starts=[('costSharedEnvStart','Reflection','Reflections_shared_probe',-1),('costSkyStart','Sky','Sky',-1),('costTerrainStart','Terrain','Terrain',-1),('costObjectsStart','Objects','Objects',-1),('costHighlightStart','Highlight','Highlights_and_outlines',-1),('costParticleStart','Particles','Particles',-1)]
for name,tag,label,obj in starts:
 p=dst/ph;txt=p.read_text(encoding='utf8');m=re.search(r'const u32 '+name+r'\s*=\s*costStart\(\);',txt);assert m;edit(ph,m.group(),m.group()+' RenderAudit::Stage audit'+name+'(RenderAudit::'+tag+');')
 pat=r'costEnd\("'+label+r'",\s*-1,\s*'+name+r'\);';m=re.search(pat,(dst/ph).read_text(encoding='utf8'));assert m;edit(ph,m.group(),m.group()+' audit'+name+'.finish();')
mapping={'renderPortalView':'Portal','buildOcclusionBuffer':'Occlusion','renderStaticBatches':'StaticBatches','renderRoadChunks':'Roads','renderProcChunks':'Procedural','renderVehicleWheels':'WheelsAnim','updateAndRenderAnimObjects':'WheelsAnim','renderShadowDecals':'Shadows','renderMirrors':'Reflection','renderPortals':'Portal','updateAndRenderLightPools':'LightPools','renderProjShadows':'Shadows','updateAndRenderBlobShadows':'Shadows','updateAndRenderLightBeams':'Particles','renderVehicleGlow':'Particles','renderVehicleLampGlow':'Particles'}
for fn,tag in mapping.items():
 old='{ const u32 ct=costStart(); '+fn+'();';edit(ph,old,'{ RenderAudit::Stage audit(RenderAudit::'+tag+'); const u32 ct=costStart(); '+fn+'();')
edit(ph,'  renderCameraFeed();','  { RenderAudit::Stage audit(RenderAudit::Reflection); renderCameraFeed(); }')
for fn in ['TerrainGame::renderObjectProbe','TerrainGame::renderRtMirror']:
 function(ph,fn,'Stage','Reflection')
edit(ph,'  renderVehicleDebris();','  { RenderAudit::Stage audit(RenderAudit::WheelsAnim); renderVehicleDebris(); }')
core=e+'src/renderer/3d/pipeline/static/core/stapip_core.cpp';include(core)
function(core,'StaPipCore::render','Work','CorePrep','  RenderAudit::count(RenderAudit::Bags); RenderAudit::count(RenderAudit::BagVertices,bag->count);\n')
edit(core,'HardwareTrace::Scope traceBounds("Bounds", HardwareTrace::Kind::Span, true);','HardwareTrace::Scope traceBounds("Bounds", HardwareTrace::Kind::Span, true);\n  RenderAudit::Work auditBounds(RenderAudit::Bounds);')
edit(core,'traceBounds.finish();','traceBounds.finish(); auditBounds.finish();',2)
edit(core,'  if (directRoute) {\n    // The whole-bag bbox','  RenderAudit::Work auditDispatch(RenderAudit::Packages);\n  if (directRoute) {\n    // The whole-bag bbox')
edit(core,'  retainCurrentBag = false;\n  const u32 dispatchEnd','  retainCurrentBag = false;\n  auditDispatch.finish();\n  const u32 dispatchEnd')
q=e+'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';include(q)
for fn,tag in [('sendObjectData','ObjectState'),('addBuffersDataToPacket','Packet'),('sendPacket','Submit'),('replayWholeBakedBag','Packet'),('endBakedBag','Packet')]:function(q,'StaPipQBufferRenderer::'+fn,'Work',tag,expected=2 if fn in ('replayWholeBakedBag','endBakedBag') else 1)
for fn in ['cull','clip']:function(q,'StaPipQBufferRenderer::'+fn,'Work','Packages','  RenderAudit::count(RenderAudit::PackagesSeen); RenderAudit::count(RenderAudit::ClassifiedVertices,buffer->size);\n')
edit(q,'FlushCache(0);','{ RenderAudit::Work auditCache(RenderAudit::Cache); FlushCache(0); }')
buf=e+'src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp';include(buf)
for fn in ['fillByCopyMax','fillByCopy1By2','fillByCopy1By3','fillByStripExpand']:function(buf,'StaPipQBuffer::'+fn,'Work','GeometryCopy','  RenderAudit::count(RenderAudit::CopyCalls);\n')
cl=e+'src/renderer/3d/pipeline/static/core/stapip_clipper.cpp';include(cl)
function(cl,'StaPipClipper::clipToPool','Work','Clip','  RenderAudit::count(RenderAudit::EeClipInputs,buffer->size);\n')
function(cl,'StaPipClipper::writeChunk','Work','Clip')
tex=e+'src/renderer/core/texture/renderer_core_texture.cpp';include(tex);function(tex,'RendererCoreTexture::useTexture','Work','Texture')
p3=e+'src/renderer/core/paths/path3/path3.cpp';include(p3);function(p3,'Path3::sendTexture','Work','Texture','  RenderAudit::count(RenderAudit::TextureUploads);\n')
edit(p3,'  frameSendPacket(texturePacket, DMA_CHANNEL_GIF, true);','  RenderAudit::count(RenderAudit::TextureChainQw,packet2_get_qw_count(texturePacket));\n  frameSendPacket(texturePacket, DMA_CHANNEL_GIF, true);')
v=e+'src/renderer/core/paths/path1/vif1_queue.cpp';include(v)
for fn in ['submit','flushRecording']:function(v,'Vif1Queue::'+fn,'Work','Submit')
function(v,'Vif1Queue::recordGif','Work','Packet')
edit(v,'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {','u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {\n  RenderAudit::count(RenderAudit::SubmitSourceQw,qwords);')
edit(v,'FlushCache(0);','{ RenderAudit::Work auditCache(RenderAudit::Cache); FlushCache(0); }',3)
for name in ['prove-native.py','review-coverage.py','root-release.py','analyze-day-drive.py','analyze-balance.py','parser-controls.py','freeze-physical.py','qualify-physical.py']:
 t=(src/name).read_text(encoding='utf8').replace('tyrax2-loop-balance-v1','tyrax2-render-audit-v1').replace('tyrax2-closeout-ps2-loop-balance-v1','tyrax2-closeout-ps2-render-audit-v1');(dst/name).write_bytes(t.encode())
(dst/'patched-files.json').write_text(json.dumps(sorted(changed),indent=2))
print(dst,len(changed))
