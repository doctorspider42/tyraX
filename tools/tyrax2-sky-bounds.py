"""A03 private sky-only exact list bounds, same-ELF turn/drive A/B/A."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out,source=map(lambda p:Path(p).resolve(),sys.argv[1:3])
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-vu-paint.py'),str(out),str(source),'--roads-day'],check=True)
engine,game=out/'tyra/engine',out/'game'
def edit(p,old,new):
    s=p.read_text(encoding='utf-8-sig');assert s.count(old)==1,(p,old,s.count(old))
    p.write_bytes(s.replace(old,new).encode())
h=engine/'inc/debug/motion_probe.hpp'
edit(h,'inline constexpr U phaseCount=3;','inline constexpr U phaseCount=6;')
edit(h,'constexpr U modes[]={0,6,0};','constexpr U modes[]={0,6,0,0,6,0};')
extra='SkyEligible, SkySafe, SkyOutside, SkyApplied, SkyCullCalls, SkyClipCalls'
edit(h,'PaintVu, Count','PaintVu, '+extra+', Count')
edit(h,'paintVu,captureFrozen','paintVu,skyEligible,skySafe,skyOutside,skyApplied,skyCullCalls,skyClipCalls,captureFrozen')
edit(h,'inline bool observing(){return false;}', '''inline unsigned skyChecks=0,skyFailures=0,skyColdCull=0,skyColdOutside=0;
inline bool skyBag=false;
struct SkyScope { bool old; explicit SkyScope(bool b):old(skyBag){skyBag=b;} ~SkyScope(){skyBag=old;} };
inline bool observing(){return false;}''')
edit(h,'if(offset==gasStart+719) { finishPending=true; }', '''if(phase<3 ? offset==gasStart+719 : current.z>=108.0F || size==1200) {
      finishPending=true;if(phase>=3 && current.z<108.0F)valid=false;
    }''')
edit(h,'  } else if(finishPending) {', '''  } else if(finishPending) {
    printf("SKYORACLE phase=%u checks=%u failed=%u safe=%u outside=%u\\n",phase,skyChecks,skyFailures,skyColdCull,skyColdOutside);
    if(!skyChecks || skyFailures || !skyColdCull || !skyColdOutside)valid=false;
    skyChecks=skyFailures=skyColdCull=skyColdOutside=0;''')
edit(game/'src/gen/game_physics.gen.cpp','part.envTexBag->paintVu = MotionProbe::variant() == 6;', 'part.envTexBag->paintVu = true;')
veh=game/'src/gen/game_vehicles.gen.cpp'
edit(veh,'inThrottle=0.0F; inBrake=0.0F;','inThrottle=MotionProbe::phase>=3 && MotionProbe::gas()?1.0F:0.0F; inBrake=0.0F;')
edit(veh,'inSteer=0.0F; inHand=1; inNos=0;','inSteer=0.0F; inHand=MotionProbe::phase>=3 && MotionProbe::gas()?0:1; inNos=0;')
edit(veh,'      if(MotionProbe::gas()) {','      if(MotionProbe::phase<3 && MotionProbe::gas()) {')
edit(game/'inc/terrain_config.hpp','constexpr int INTERLEAVE_PASSES = 1;','constexpr int INTERLEAVE_PASSES = 2;')
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp','  bool stripped;','  bool diagnosticSky = false;\n  bool stripped;')
edit(game/'src/gen/game_collision.gen.cpp','  skyDome.bag->count = static_cast<u32>(skyDome.vertices.size());',
     '  skyDome.bag->diagnosticSky = true;\n  skyDome.bag->count = static_cast<u32>(skyDome.vertices.size());')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'  MotionProbe::add(MotionProbe::Bags);', '  MotionProbe::SkyScope skyScope(bag->diagnosticSky);\n  MotionProbe::add(MotionProbe::Bags);')
q=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
for method,kind in [('cull','SkyCullCalls'),('clip','SkyClipCalls')]:
    s=q.read_text();start=s.index('void StaPipQBufferRenderer::'+method+'(')
    empty=s.index('  if (buffer->size == 0) {',start)
    pos=s.index('\n  }',empty)+len('\n  }')
    s=s[:pos]+'\n  if(MotionProbe::skyBag)MotionProbe::add(MotionProbe::'+kind+');'+s[pos:]
    q.write_bytes(s.encode())
pack=engine/'src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp'
pack.write_bytes(b'#include "debug/motion_probe.hpp"\n#include "debug/sky_bounds.hpp"\n'+pack.read_bytes())
edit(pack,'#if TYRA_STAPIP_PROBE_ACCEPT_ALL\n  // A1: every test above ran', '''  // Only an otherwise clipped ordinary sky LIST slice is eligible.
  if(pkg.bag->diagnosticSky && !pkg.bag->stripped && !pkg.bag->billboard &&
     clipObjectSpacePlanes && crossingMask && o_guardBandOnly &&
     result==PARTIALLY_IN_FRUSTUM && !*o_guardBandOnly &&
     pkg.size<=((maxVertCount/6)/3)*3) {
    MotionProbe::add(MotionProbe::SkyEligible);
    const bool cold=MotionProbe::offset>=1000 && MotionProbe::offset<1100;
    if(MotionProbe::variant()==6 || cold){
      const auto r=SkyBounds::classify(pkg.vertices,pkg.size,clipObjectSpacePlanes);
      if(cold){
        ++MotionProbe::skyChecks;
        if(r==SkyBounds::Cull)++MotionProbe::skyColdCull;
        if(r==SkyBounds::Outside)++MotionProbe::skyColdOutside;
        if(!SkyBounds::verify(pkg.vertices,pkg.size,clipObjectSpacePlanes,r))++MotionProbe::skyFailures;
      }
      if(MotionProbe::variant()==6){
        if(r==SkyBounds::Outside){MotionProbe::add(MotionProbe::SkyOutside);MotionProbe::add(MotionProbe::SkyApplied);return OUTSIDE_FRUSTUM;}
        if(r==SkyBounds::Cull){MotionProbe::add(MotionProbe::SkySafe);MotionProbe::add(MotionProbe::SkyApplied);*crossingMask=0;*o_guardBandOnly=true;}
      }
    }
  }
#if TYRA_STAPIP_PROBE_ACCEPT_ALL
  // A1: every test above ran''')
(engine/'inc/debug/sky_bounds.hpp').write_bytes((ROOT/'tools/tyrax2-sky-bounds.hpp').read_bytes())
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'sky-bounds-manifest.json').write_text(json.dumps(dict(experiment='A03-sky-tight-list-bounds',
    modes=[0,6,0,0,6,0],constantVuPaint=True,coldOracle=[1000,1100],
    note='Private sky only; existing cull/clip partition can reorder dispatch, requiring raster qualification',
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
