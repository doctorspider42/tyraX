"""A04: precompute exactly A03's sky clip-slice boxes during geometry build."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out,source=map(lambda p:Path(p).resolve(),sys.argv[1:3])
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-sky-bounds.py'),str(out),str(source)],check=True)
engine,game=out/'tyra/engine',out/'game'
def edit(p,old,new):
    s=p.read_text(encoding='utf-8-sig');assert s.count(old)==1,(p,old,s.count(old))
    p.write_bytes(s.replace(old,new).encode())
(engine/'inc/debug/sky_prepared.hpp').write_bytes((ROOT/'tools/tyrax2-sky-prepared.hpp').read_bytes())
h=engine/'inc/debug/motion_probe.hpp'
edit(h,'SkyClipCalls, Count','SkyClipCalls, SkyPreparedHits, SkyPreparedMisses, Count')
edit(h,'skyClipCalls,captureFrozen','skyClipCalls,skyPreparedHits,skyPreparedMisses,captureFrozen')
(engine/'inc/debug/sky_prepared_state.hpp').write_text('#pragma once\n#include "debug/sky_prepared.hpp"\n#include "math/vec4.hpp"\n'
    'namespace SkyPrepared { inline Store<Tyra::Vec4> sky; }\n')
sky=game/'src/gen/game_collision.gen.cpp'
sky.write_bytes(b'#include "debug/sky_prepared_state.hpp"\n'+sky.read_bytes())
edit(sky,'  skyDome.bag->bboxVersion = ++g_bboxStamp;  // dome rebuilds on retint', '''  skyDome.bag->bboxVersion = ++g_bboxStamp;  // dome rebuilds on retint
  const u32 skyMaximum=stapip.core.getMaxVertCountByBag(skyDome.bag.get());
  SkyPrepared::sky.build(skyDome.bag->vertices,skyDome.bag->count,skyDome.bag->bboxVersion,skyMaximum);
  printf("SKYPREP phase=%u offset=%u vertices=%u maximum=%u boxes=%u bytes=%u\\n",
    MotionProbe::phase,MotionProbe::offset,skyDome.bag->count,skyMaximum,
    unsigned(SkyPrepared::sky.boxes.size()),unsigned(SkyPrepared::sky.boxes.size()*sizeof(SkyPrepared::Store<Tyra::Vec4>::Box)));''')
pack=engine/'src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp'
pack.write_bytes(b'#include "debug/sky_prepared_state.hpp"\n'+pack.read_bytes())
edit(pack,'      const auto r=SkyBounds::classify(pkg.vertices,pkg.size,clipObjectSpacePlanes);','''      const auto* box=SkyPrepared::sky.get(pkg.bag->vertices,pkg.bag->count,
        pkg.bag->bboxVersion,maxVertCount,unsigned(pkg.vertices-pkg.bag->vertices),pkg.size);
      MotionProbe::add(box?MotionProbe::SkyPreparedHits:MotionProbe::SkyPreparedMisses);
      const auto r=box?SkyBounds::classify(box->corners,2,clipObjectSpacePlanes):SkyBounds::Keep;
      if(cold && r!=SkyBounds::classify(pkg.vertices,pkg.size,clipObjectSpacePlanes))++MotionProbe::skyFailures;''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'sky-prepared-manifest.json').write_text(json.dumps(dict(experiment='A04-sky-prepared-list-bounds',
    modes=[0,6,0,0,6,0],constantVuPaint=True,coldOracle=[1000,1100],
    note='Private geometry-time boxes; exact pointer/count/version/package shape required; missing metadata retains old clip route',
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
