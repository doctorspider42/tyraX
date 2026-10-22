"""P09: reuse the acquired expansion within one synchronous strip loop."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
out,source=[Path(p).resolve() for p in sys.argv[1:3]]
reverse='--reverse' in sys.argv
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-vu-paint.py'),str(out),str(source),'--roads-day']+
    (['--reverse'] if reverse else []),check=True,env={**os.environ,'PYTHONUTF8':'1'})
def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig');assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())
engine=out/'tyra/engine';game=out/'game'
edit(game/'inc/terrain_config.hpp','constexpr int INTERLEAVE_PASSES = 1;','constexpr int INTERLEAVE_PASSES = 2;')
edit(game/'src/gen/game_physics.gen.cpp','part.envTexBag->paintVu = MotionProbe::variant() == 6;','part.envTexBag->paintVu = true;')
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp','  StaPipBag();','  unsigned diagnosticRoadId=0;\n  StaPipBag();')
edit(game/'src/gen/game_collision.gen.cpp','    c.colors.bind(c.colorBag);',
    '    c.colors.bind(c.colorBag);\n    c.bag->diagnosticRoadId=c.owner==-3?unsigned(&c-procChunks.data())+1:0;')
edit(engine/'inc/debug/motion_probe.hpp','inline bool observing(){return false;}',
    'inline U onceLoops=0,onceAcquire=0,onceReuse=0,onceChecks=0,onceFailed=0,onceFallback=0;\ninline bool observing(){return false;}')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
old='''      for (u32 t = 0; t < triangles; t += trisPerChunk) {
        const u32 remaining = triangles - t;
        const u32 n = remaining < trisPerChunk ? remaining : trisPerChunk;
        auto buffer = qbufferRenderer.getBuffer();
        if (!(TYRA_STAPIP_EXPANSION_CACHE &&
              qbufferRenderer.fillCachedStrip(buffer, packages[i], t, n)))
          buffer->fillByStripExpand(packages[i], t, n);
        qbufferRenderer.clip(buffer);
      }'''
new=r'''      const bool selected = pkg.bag->diagnosticRoadId == 24 || pkg.bag->diagnosticRoadId == 34;
      const bool once = selected && MotionProbe::variant() == 6;
      const bool cold = selected && MotionProbe::offset >= 1000 && MotionProbe::offset < 1100;
      StaPipBagPackage base = pkg;
      bool ready = false;
      if (cold) ++MotionProbe::onceLoops;
      for (u32 t = 0; t < triangles; t += trisPerChunk) {
        const u32 remaining = triangles - t;
        const u32 n = remaining < trisPerChunk ? remaining : trisPerChunk;
        auto buffer = qbufferRenderer.getBuffer();
        if (once && t && ready) {
          StaPipBagPackage slice = base;
          slice.size = n * 3;
          slice.vertices += t * 3;
          if (slice.sts) slice.sts += t * 3;
          if (slice.colors) slice.colors += t * 3;
          if (slice.normals) slice.normals += t * 3;
          buffer->fillByPointer(slice);
          buffer->stripped = false;
          if (cold) {
            ++MotionProbe::onceReuse; ++MotionProbe::onceChecks;
            const bool found = qbufferRenderer.fillCachedStrip(buffer, pkg, t, n);
            const bool equal = found && buffer->vertices == slice.vertices &&
                buffer->sts == slice.sts && buffer->colors == slice.colors &&
                buffer->normals == slice.normals && buffer->size == slice.size &&
                buffer->clipPlaneMask == slice.clipPlaneMask && buffer->bag == slice.bag && !buffer->stripped;
            if (!equal) ++MotionProbe::onceFailed;
            TYRA_ASSERT(equal,"One-lookup slice differs from original lookup");
          }
        } else {
          if (cold && (!once || !t)) ++MotionProbe::onceAcquire;
          const bool found = (!once || !t) && TYRA_STAPIP_EXPANSION_CACHE &&
              qbufferRenderer.fillCachedStrip(buffer, pkg, t, n);
          if (!found) {
            if (cold) ++MotionProbe::onceFallback;
            buffer->fillByStripExpand(pkg, t, n);
          } else if (once && !t) {
            ready = true;
            base.vertices=buffer->vertices; base.sts=buffer->sts;
            base.colors=buffer->colors; base.normals=buffer->normals;
          }
        }
        qbufferRenderer.clip(buffer);
      }'''
edit(core,old,new)
edit(game/'src/terrain_game.cpp','  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {',r'''  if(MotionProbe::offset==1000)
    MotionProbe::onceLoops=MotionProbe::onceAcquire=MotionProbe::onceReuse=MotionProbe::onceChecks=MotionProbe::onceFailed=MotionProbe::onceFallback=0;
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    unsigned found=0;
    for(const auto& c:procChunks) if(c.bag && (c.bag->diagnosticRoadId==24 || c.bag->diagnosticRoadId==34)) {
      TYRA_ASSERT(c.owner==-3 && c.bag->stripped && c.bag->count==66 && c.stripRun==75 && !c.bag->refineStrips,"One-lookup identity changed");
      ++found;
    }
    if(found!=2 || !MotionProbe::onceLoops || MotionProbe::onceFailed || MotionProbe::onceFallback)MotionProbe::valid=false;
    printf("LOG: STRIPONCE phase=%u loops=%u acquire=%u reuse=%u checks=%u failed=%u fallback=%u interleave=%u\n",
      MotionProbe::phase,MotionProbe::onceLoops,MotionProbe::onceAcquire,MotionProbe::onceReuse,
      MotionProbe::onceChecks,MotionProbe::onceFailed,MotionProbe::onceFallback,unsigned(INTERLEAVE_PASSES));''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='one-expansion-lookup-per-strip',
    modes=[6,0,6] if reverse else [0,6,0],ids=[24,34],newCache=False,frameLifetimeUnchanged=True,
    constantVuPaint=True,fixedInterleave=2,files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
