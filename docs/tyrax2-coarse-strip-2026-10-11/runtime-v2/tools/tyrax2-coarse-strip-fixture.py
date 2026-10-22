"""A30 private coarse EE route; same qualified A29 shader in both arms."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('source',type=Path,help='Fully qualified A29 native-fallback fixture')
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert q.sha(a.source/rel)==h,rel
base=json.loads((a.source/'clip-capacity-manifest.json').read_text())
assert base['experiment']=='A29' and base['nativeFallback'] and not base.get('ringStateOnce',False)
for platform in ('emulator','physical'):
    report=json.loads((a.source/f'clip-ring-{platform}.json').read_text())
    assert report['valid'] and report['nativeActive'] and report['groupRasterEqual']
    assert report['consumerInputEqual'] and len(report['images'])==12

def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):
    shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','ring-shader-pins.json'):
    shutil.copy2(a.source/rel,a.out/rel)

def edit(path,old,new):
    text=path.read_text();assert text.count(old)==1,(path,old,text.count(old))
    path.write_text(text.replace(old,new))

engine=a.out/'tyra/engine'
inc=engine/'inc/renderer/3d/pipeline/static/core'
src=engine/'src/renderer/3d/pipeline/static/core'
shutil.copy2(repo/'tools/tyrax2-coarse-strip-input.hpp',engine/'inc/debug/coarse_strip.hpp')
consumer=engine/'inc/debug/clip_consumer.hpp'
consumer.write_text(consumer.read_text()+'\n#include "debug/coarse_strip.hpp"\n')
edit(inc/'stapip_qbuffer.hpp','  bool nativeClipStrip=false;',
     '  bool coarseClipStrip=false,partialStripOrigin=false;\n  bool nativeClipStrip=false;')
edit(src/'stapip_qbuffer.cpp','  nativeClipStrip=false;nativeClipParity=0;',
     '  coarseClipStrip=false;partialStripOrigin=false;\n  nativeClipStrip=false;nativeClipParity=0;')
core=src/'stapip_core.cpp'
edit(core,'      const auto& pkg = packages[i];', '''      const auto& pkg = packages[i];
      const bool sourceEligible=qbufferRenderer.isVU1ClippingEnabled() &&
        !pkg.bag->billboard && !qbufferRenderer.hasProgramOverrides();
      if(sourceEligible)CoarseStrip::expect(pkg);
      if(sourceEligible && CoarseStrip::active){
        TYRA_ASSERT(ClipRing::active,"A30 coarse route needs qualified rings");
        const u32 capacity=std::min(u32(72),maxVertCount);
        TYRA_ASSERT(capacity>2,"A30 native source capacity");
        const u32 step=capacity-2;
        for(u32 t=0;t<triangles;t+=step){
          auto* buffer=qbufferRenderer.getBuffer();
          buffer->fillNativeClipStrip(pkg,t,std::min(step,triangles-t));
          buffer->coarseClipStrip=true;buffer->partialStripOrigin=true;
          qbufferRenderer.clip(buffer);
        }
        continue;
      }''')
edit(core,'            if (route == StaPipStripSubranges::Cull) qbufferRenderer.cull(buffer);',
     '            buffer->partialStripOrigin=sourceEligible;\n            if (route == StaPipStripSubranges::Cull) qbufferRenderer.cull(buffer);')
edit(core,'          ClipConsumer::noteFallback(n);',
     '          buffer->partialStripOrigin=sourceEligible;\n          ClipConsumer::noteFallback(n);')
edit(core,'          buffer->fillByStripExpand(packages[i], t, n);',
     '          buffer->fillByStripExpand(packages[i], t, n);\n        buffer->partialStripOrigin=sourceEligible;')
edit(src/'stapip_qbuffer_renderer.cpp','  ClipConsumer::input(buffer);', '''  ClipConsumer::input(buffer);
  if(ClipConsumer::observe && buffer->coarseClipStrip)
    TYRA_ASSERT(buffer->nativeClipStrip && buffer->partialStripOrigin,
      "A30 invalid coarse shader/source admission");
  CoarseStrip::input(*buffer);''')

game=a.out/'game/src/terrain_game.cpp'
edit(game,'#include "debug/motion_probe.hpp"',
     '#include "debug/motion_probe.hpp"\n#include "debug/clip_consumer.hpp"')
edit(game,'  stapip.core.setDiagnosticClipRing(ClipTrial::mode==7);',
     '  if(CoarseStrip::active!=(ClipTrial::mode==7))stapip.core.setDiagnosticClipRing(false);\n  stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=ClipTrial::mode==7;')
physics=a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'    stapip.core.setDiagnosticClipRing(ClipTrial::mode==7);',
     '    stapip.core.setDiagnosticClipRing(false);stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=ClipTrial::mode==7;')
edit(physics,'  stapip.core.setDiagnosticClipRing(active==7);',
     '  stapip.core.setDiagnosticClipRing(false);stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=active==7;')
edit(physics,'      ClipConsumer::reset();ClipConsumer::observe=frame==31;',
     '      ClipConsumer::reset();CoarseStrip::reset();ClipConsumer::observe=frame==31;')
edit(physics,'    TYRA_ASSERT(arm==1?ClipConsumer::nativeCalls>0:ClipConsumer::nativeCalls==0,',
     '    TYRA_ASSERT(ClipConsumer::nativeCalls>0,')
edit(physics,'''    TYRA_ASSERT(arm==1?ClipConsumer::fallbackCalls>0:ClipConsumer::fallbackCalls==0,
      "A29 native fallback activation mismatch");''','''    TYRA_ASSERT(CoarseStrip::sourcePackages>0 &&
      (arm==1?(CoarseStrip::deliveryJobs>0 && CoarseStrip::same(CoarseStrip::source,CoarseStrip::delivered)):
               CoarseStrip::deliveryJobs==0),"A30 source/delivery contract");
    printf("COARSESOURCE phase=%u arm=%u packages=%u triangles=%u vertices=%u hash=%u\\n",
      MotionProbe::phase,arm,CoarseStrip::sourcePackages,CoarseStrip::source.triangles,
      CoarseStrip::source.vertices,CoarseStrip::source.hash);
    printf("COARSEDELIVERY phase=%u arm=%u jobs=%u triangles=%u vertices=%u hash=%u\\n",
      MotionProbe::phase,arm,CoarseStrip::deliveryJobs,CoarseStrip::delivered.triangles,
      CoarseStrip::delivered.vertices,CoarseStrip::delivered.hash);
    printf("COARSERESIDUAL phase=%u arm=%u triangles=%u vertices=%u hash=%u\\n",
      MotionProbe::phase,arm,CoarseStrip::residual.triangles,CoarseStrip::residual.vertices,
      CoarseStrip::residual.hash);fflush(stdout);''')
plan=base.copy()
plan.update(experiment='A30',coarseStrip=True,nativeFallback=False,ringStateOnce=False,
            preparationSha256=q.sha(core),qbufferSha256=q.sha(src/'stapip_qbuffer.cpp'),
            consumerSha256=q.sha(consumer),coarseSha256=q.sha(engine/'inc/debug/coarse_strip.hpp'),
            baseElfSha256=binding['elfSha256'],
            status='UNQUALIFIED: source/delivery/residual plus raster/runtime gates required')
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
print(a.out,'UNQUALIFIED coarse route; identical ring shader both arms')
