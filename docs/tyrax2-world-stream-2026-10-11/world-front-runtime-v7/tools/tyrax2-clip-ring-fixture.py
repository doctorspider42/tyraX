"""A29 private runtime integration; correctness/reader gates required before pricing."""
import argparse
import hashlib
import importlib.util
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path, help='Bound A28 reverse fixture')
p.add_argument('shaders', type=Path, help='Qualified A29 host V8 output')
p.add_argument('--native-fallback', action='store_true',
               help='Also borrow unrefined/cache-unavailable strip input; cold admission required')
p.add_argument('--state-once',action='store_true',help='Use qualified hybrid-once images')
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('quiet', repo/'tools/tyrax2-quiet-fixture.py')
q = importlib.util.module_from_spec(spec); spec.loader.exec_module(q)
assert not a.out.exists()
assert not a.state_once or a.native_fallback
binding = json.loads((a.source/'native-binding.json').read_text())
for rel, digest in binding['files'].items():
    assert q.sha(a.source/rel) == digest, rel
host=(a.shaders/'host-result.txt').read_text()
assert ('cases=11520 triangles=264528' if a.state_once else
        'cases=7680 triangles=176352') in host
assert ('rejection-controls=5' if a.state_once else 'rejection-controls=3') in host
if a.state_once:
    assert 'once-cases=3840 empty-first=291' in host
def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game', 'tyra/engine'):
    shutil.copytree(a.source/part, a.out/part, ignore=ignore)
for rel in ('tyra/Makefile.base', 'authored-manifest.json'):
    shutil.copy2(a.source/rel, a.out/rel)

def edit(path, old, new):
    text = path.read_text()
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_text(text.replace(old, new))

engine = a.out/'tyra/engine'
inc = engine/'inc/renderer/3d/pipeline/static/core'
src = engine/'src/renderer/3d/pipeline/static/core'
suffix='ring_hybrid_once' if a.state_once else 'ring_hybrid'
stems=('stapip_clip_c_vu1_'+suffix+'_fold',
       'stapip_clip_tc_vu1_'+suffix+'_fold_paint')
sizes=json.loads((a.shaders/'assembled-sizes.json').read_text())
assert [sizes[s+'.vclpp'] for s in stems]==([394,466] if a.state_once else [392,464])
assert 1178+sum(sizes[s+'.vclpp'] for s in stems)<=2042
for stem in stems:
    shutil.copy2(a.shaders/(stem+'.vclpp'), src/'programs/clip'/(stem+'.vclpp'))
(engine/'inc/debug/clip_ring.hpp').write_text('''#pragma once
namespace ClipRing { inline bool active=false; }
''')
shutil.copy2(repo/'tools/tyrax2-clip-ring-input.hpp',engine/'inc/debug/clip_consumer.hpp')
edit(src/'stapip_vu1_program.cpp', '#include "debug/clip_consumer.hpp"',
     '#include "debug/clip_consumer.hpp"\n#include "debug/clip_capacity.hpp"')
edit(inc/'stapip_vu1_program.hpp', '  bool emitsStateFlag(const bool& emitState) const;', '''
  bool emitsStateFlag(const bool& emitState) const;
  void diagnosticReplaceImage(u32* s, u32* e);
''')
edit(src/'stapip_vu1_program.cpp', 'StaPipVU1Program::~StaPipVU1Program() {}', '''
StaPipVU1Program::~StaPipVU1Program() {}
void StaPipVU1Program::diagnosticReplaceImage(u32* s,u32* e){
  start=s;end=e;programSize=calculateProgramSize();
  packetSize=packet2_utils_get_packet_size_for_program(start,end);
}
''')
edit(src/'stapip_vu1_program.cpp',
     '                                     reglistCount, 0);', '''
                                     reglistCount, 0);
    if(buffer->nativeClipStrip){
      TYRA_ASSERT(isClipProgram(name),"A29 native strip reached non-clip shader");
      reinterpret_cast<u32*>(packet->next)[-1]=2U|(buffer->nativeClipParity&1U);
    }
''')
edit(inc/'stapip_qbuffer.hpp', '  StaPipBag* bag;', '''
  bool nativeClipStrip=false;
  u8 nativeClipParity=0;
  void fillNativeClipStrip(const StaPipBagPackage& pkg,u32 first,u32 triangles);
  StaPipBag* bag;''')
edit(src/'stapip_qbuffer.cpp', 'void StaPipQBuffer::deallocateDynamicData() {', '''
void StaPipQBuffer::fillNativeClipStrip(const StaPipBagPackage& pkg,u32 first,u32 triangles){
  TYRA_ASSERT(triangles>0 && first+triangles+2<=pkg.size && triangles+2<=72 &&
    triangles+2<=maxVertCount,"A29 native strip range/capacity");
  StaPipBagPackage sub=pkg;
  sub.vertices+=first;
  if(sub.sts)sub.sts+=first;
  if(sub.colors)sub.colors+=first;
  if(sub.normals)sub.normals+=first;
  sub.size=triangles+2;fillByPointer(sub);
  stripped=false;nativeClipStrip=true;nativeClipParity=first&1U;
}
void StaPipQBuffer::deallocateDynamicData() {
  nativeClipStrip=false;nativeClipParity=0;''')
for header in ('stapip_core.hpp', 'stapip_qbuffer_renderer.hpp'):
    edit(inc/header, '  void setVU1Clipping(const bool& enabled);',
         '  void setVU1Clipping(const bool& enabled);\n  void setDiagnosticClipRing(bool enabled);')
edit(src/'stapip_core.cpp', 'void StaPipCore::setVU1Clipping(const bool& enabled) {', '''
void StaPipCore::setDiagnosticClipRing(bool enabled){qbufferRenderer.setDiagnosticClipRing(enabled);}
void StaPipCore::setVU1Clipping(const bool& enabled) {''')
for cpp in ('stapip_core.cpp', 'stapip_qbuffer_renderer.cpp'):
    path = src/cpp
    text = path.read_text(); path.write_text('#include "debug/clip_ring.hpp"\n'+text)
edit(src/'stapip_qbuffer_renderer.cpp',
     'void StaPipQBufferRenderer::setVU1Clipping(const bool& enabled) {', '''
extern "C" {
extern u32 StaPipVU1Clip_C_ring_hybrid_fold_CodeStart,StaPipVU1Clip_C_ring_hybrid_fold_CodeEnd;
extern u32 StaPipVU1Clip_TC_ring_hybrid_fold_paint_CodeStart,StaPipVU1Clip_TC_ring_hybrid_fold_paint_CodeEnd;
}
void StaPipQBufferRenderer::setDiagnosticClipRing(bool enabled){
  if(ClipRing::active==enabled)return;
  TYRA_ASSERT(vu1Clipping && !hasProgramOverrides(),"A29 unsupported clip mode/override");
  setVU1Clipping(false);
  const StaPipProgramName names[]={StaPipClipColor,StaPipClipDirLights,
    StaPipClipTextureColor,StaPipClipTextureDirLights,StaPipClipTextureEnv};
  static u32* original[5][2]{};
  for(unsigned i=0;i<5;++i){
    auto* shader=repository.getProgram(names[i]);
    if(!original[i][0]){original[i][0]=shader->getStart();original[i][1]=shader->getEnd();}
    shader->diagnosticReplaceImage(enabled?(i<2?&StaPipVU1Clip_C_ring_hybrid_fold_CodeStart:
      &StaPipVU1Clip_TC_ring_hybrid_fold_paint_CodeStart):original[i][0],
      enabled?(i<2?&StaPipVU1Clip_C_ring_hybrid_fold_CodeEnd:
      &StaPipVU1Clip_TC_ring_hybrid_fold_paint_CodeEnd):original[i][1]);
  }
  ClipRing::active=enabled;setVU1Clipping(true);
  TYRA_ASSERT(billboardsResident,"A29 displaced resident billboards");
}
void StaPipQBufferRenderer::setVU1Clipping(const bool& enabled) {''')
if a.state_once:
    path=src/'stapip_qbuffer_renderer.cpp';text=path.read_text()
    for ending in ('fold_Code','fold_paint_Code'):
        old='_ring_hybrid_'+ending
        assert text.count(old)==4
        text=text.replace(old,'_ring_hybrid_once_'+ending)
    path.write_text(text)
# Keep original refined classifications/omissions and material order. Only
# adjacent true-clip blocks merge; safe cull blocks retain the original path.
edit(src/'stapip_core.cpp', '            if (route == StaPipStripSubranges::Cull) {', '''
            const bool native=ClipRing::active && route!=StaPipStripSubranges::Cull;
            if (route == StaPipStripSubranges::Cull || native) {''')
edit(src/'stapip_core.cpp', '                if ((n + next) * 3 > maxVertCount) break;', '''
                if(native?(n+next+2>72 || n+next+2>maxVertCount):
                          (n+next)*3>maxVertCount)break;''')
edit(src/'stapip_core.cpp', '''            if (!(TYRA_STAPIP_EXPANSION_CACHE &&
                  qbufferRenderer.fillCachedStrip(buffer, pkg, first, n)))''', '''
            if(native)buffer->fillNativeClipStrip(pkg,first,n);
            else if (!(TYRA_STAPIP_EXPANSION_CACHE &&
                  qbufferRenderer.fillCachedStrip(buffer, pkg, first, n)))''')
if a.native_fallback:
    path=src/'stapip_core.cpp'
    edit(path, '#include "debug/clip_ring.hpp"',
         '#include "debug/clip_ring.hpp"\n#include "debug/clip_consumer.hpp"')
    edit(path, '      for (u32 t = 0; t < triangles; t += trisPerChunk) {', '''
      if(ClipRing::active){
        const u32 nativeCapacity=std::min(u32(72),maxVertCount);
        TYRA_ASSERT(nativeCapacity>2,"A29 native fallback capacity");
        const u32 step=nativeCapacity-2;
        for(u32 t=0;t<triangles;t+=step){
          const u32 n=std::min(step,triangles-t);
          auto* buffer=qbufferRenderer.getBuffer();
          buffer->fillNativeClipStrip(pkg,t,n);
          ClipConsumer::noteFallback(n);
          qbufferRenderer.clip(buffer);
        }
        continue;
      }
      for (u32 t = 0; t < triangles; t += trisPerChunk) {''')
game = a.out/'game/src/terrain_game.cpp'
edit(game, 'FastJoin::active=false;ClipTrial::mode=MotionProbe::variant();', '''
FastJoin::active=false;ClipTrial::mode=MotionProbe::variant();
  stapip.core.setDiagnosticClipRing(ClipTrial::mode==7);''')
physics = a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics, 'stableTerrainFocus=ClipTrial::mode!=7 || TerrainFocus::checkpoint;',
     'stableTerrainFocus=true;')
edit(physics, '    ClipTrial::mode=arm==1?7:0;', '''
    ClipTrial::mode=arm==1?7:0;
    stapip.core.setDiagnosticClipRing(ClipTrial::mode==7);''')
edit(physics, '    printf("HEADERORACLE', '''
    TYRA_ASSERT(arm==1?ClipConsumer::nativeCalls>0:ClipConsumer::nativeCalls==0,
      "A29 native strip activation mismatch");
    printf("RINGCONTRACT phase=%u arm=%u calls=%u triangles=%u\\n",MotionProbe::phase,arm,
      ClipConsumer::nativeCalls,ClipConsumer::nativeTriangles);fflush(stdout);
    printf("HEADERORACLE''')
if a.native_fallback:
    edit(physics, '    printf("RINGCONTRACT', '''
    TYRA_ASSERT(arm==1?ClipConsumer::fallbackCalls>0:ClipConsumer::fallbackCalls==0,
      "A29 native fallback activation mismatch");
    printf("RINGFALLBACK phase=%u arm=%u calls=%u triangles=%u\\n",MotionProbe::phase,arm,
      ClipConsumer::fallbackCalls,ClipConsumer::fallbackTriangles);fflush(stdout);
    printf("RINGCONTRACT''')
edit(physics, '  TerrainFocus::checkpoint=false;FastJoin::checkpoint=false;ClipTrial::mode=active;g_frameDt=dt;', '''
  TerrainFocus::checkpoint=false;FastJoin::checkpoint=false;ClipTrial::mode=active;g_frameDt=dt;
  stapip.core.setDiagnosticClipRing(active==7);''')
plan = json.loads((a.source/'clip-capacity-manifest.json').read_text())
plan.update(experiment='A29',reverse=False,clipRing=True,terrainFocus=True,
            nativeFallback=a.native_fallback,
            ringStateOnce=a.state_once,
            preparationSha256=q.sha(src/'stapip_core.cpp'),
            qbufferSha256=q.sha(src/'stapip_qbuffer.cpp'),
            consumerSha256=q.sha(engine/'inc/debug/clip_consumer.hpp'),
            status='UNQUALIFIED: native compile and canonical-input/raster gates pending')
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
(a.out/'ring-shader-pins.json').write_text(json.dumps({str(s.relative_to(src)):
    hashlib.sha256(s.read_bytes()).hexdigest() for s in (src/'programs/clip').glob('*_ring*.vclpp')},indent=2)+'\n')
print(a.out, 'UNQUALIFIED runtime prototype; do not price/deploy as accepted')
