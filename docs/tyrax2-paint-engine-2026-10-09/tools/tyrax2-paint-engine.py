"""A10 public engine paint API qualification, separate enabled/disabled builds."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('out',type=Path)
parser.add_argument('raster_fixture',type=Path,help='Completed A09 V3 fixture')
parser.add_argument('--enabled',action='store_true')
args=parser.parse_args()
out=args.out.resolve(); source=args.raster_fixture.resolve()
assert not out.exists(), 'Refuse to overwrite evidence'
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec); spec.loader.exec_module(quiet)

def ignore(path,names):
    return quiet.ignore(path,names)+[n for n in names if n.startswith('paint-raster-') or n=='paint-raster-analysis.json']

shutil.copytree(source/'game',out/'game',ignore=ignore)
shutil.copytree(repo/'vendor/tyra',out/'tyra',ignore=quiet.ignore)
shutil.copy2(source/'authored-manifest.json',out/'authored-manifest.json')

def edit(path,old,new):
    data=path.read_text(encoding='utf-8-sig')
    assert data.count(old)==1,(path,old,data.count(old))
    path.write_bytes(data.replace(old,new).encode())

engine=out/'tyra/engine'
shutil.copy2(source/'tyra/engine/inc/debug/motion_probe.hpp',engine/'inc/debug/motion_probe.hpp')
if args.enabled:
    edit(engine/'inc/renderer/3d/pipeline/static/core/stapip_vu1_experiments.hpp',
         '#define TYRA_VU1_EXP_PAINT 0','#define TYRA_VU1_EXP_PAINT 1')
q=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(q,'namespace Tyra {','extern unsigned paintRasterCull, paintRasterClip;\nnamespace Tyra {')
for family,counter in [('cull','Cull'),('clip','Clip')]:
    marker=f'void StaPipQBufferRenderer::{family}(StaPipQBuffer* buffer) {{'
    edit(q,marker,marker+f'\n  if(buffer->size) ++paintRaster{counter};')

# TerrainGame is not instantiated by this isolated harness, but its generated
# TUs still compile. Remove the older private C16 API usage, retaining EE paint.
physics=out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'''      part.envTexBag->paintVu = MotionProbe::variant() == 6;
      if(part.envTexBag->paintVu) MotionProbe::add(MotionProbe::PaintVu);
''','')
edit(physics,'''        if(part.envTexBag->paintVu) {
          part.envTexBag->paintForward = fwdL;
          part.envTexBag->paintHalf = hL;
        } else {''','        { // Isolated raster fixture: unused TerrainGame retains EE paint.')

header=out/'game/inc/paint_raster.hpp'
data=header.read_text()
# Keep requested test groups but record the actual engine representation.
data=data.replace('tex.paintVu','paintVu')
data=data.replace('tex.paintForward','paintForward').replace('tex.paintHalf','paintHalf')
header.write_bytes(data.encode())
edit(header,'  unsigned stamp = 1, step = 0, frames = 0;',
     '''  unsigned stamp = 1, step = 0, frames = 0;
  bool paintVu = false;
  Tyra::Vec4 paintForward, paintHalf;''')
edit(header,'    paintVu = step % 3 == 1;',
     '    paintVu = step % 3 == 1 && pipe.core.supportsPaintVu(&bag);')
edit(header,'    ++stamp; bag.bboxVersion = stamp;','''#if TYRA_VU1_EXP_PAINT
    tex.paintVu = paintVu;
    tex.paintForward = paintForward;
    tex.paintHalf = paintHalf;
#endif
    ++stamp; bag.bboxVersion = stamp;''')
edit(header,'#include <tyra>','#include <tyra>\n#include "renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tce_vu1_program.hpp"')
edit(header,'    configure();\n  }','''    unsigned checks = 0;
    auto check = [&](bool wanted) {
      TYRA_ASSERT(pipe.core.supportsPaintVu(&bag) == wanted, "Paint capability mismatch");
      ++checks;
    };
    const bool enabled = TYRA_VU1_EXP_PAINT != 0;
    check(enabled);
    TYRA_ASSERT(!pipe.core.supportsPaintVu(nullptr), "Null paint bag accepted");
    tex.textureFunction=0; check(false); tex.textureFunction=3;
    tex.coordinatesAreNormals=false; check(false); tex.coordinatesAreNormals=true;
    tex.coordinates=nullptr; check(false); tex.coordinates=normals;
    auto* image=tex.texture; tex.texture=nullptr; check(false); tex.texture=image;
    color.single=colors; check(false); color.single=nullptr;
    color.many=nullptr; check(false); color.many=colors;
    auto transform=info.transformationType;
    info.transformationType=static_cast<decltype(transform)>(999); check(false);
    info.transformationType=transform;
    bag.info=nullptr; check(false); bag.info=&info;
    bag.color=nullptr; check(false); bag.color=&color;
    bag.texture=nullptr; check(false); bag.texture=&tex;
    Tyra::StaPipLightingBag light;
    bag.lighting=&light; check(false); bag.lighting=nullptr;
    Tyra::StaPipBillboardBag billboard;
    bag.billboard=&billboard; check(false); bag.billboard=nullptr;
    const unsigned classes=pipe.core.getResidentClasses();
    pipe.core.setResidentClasses(classes & ~Tyra::StaPipQBufferRenderer::StaPipClassTextureEnv);
    check(false); pipe.core.setResidentClasses(classes);
    pipe.core.setVU1Clipping(false); check(false); pipe.core.setVU1Clipping(true);
    Tyra::StaPipCullTCEVU1Program replacement;
    pipe.core.setProgramOverride(Tyra::StaPipCullTextureEnv,&replacement); check(false);
    pipe.core.setProgramOverride(Tyra::StaPipCullTextureEnv,nullptr);
    check(enabled);
    printf("PAINTCAP enabled=%u checks=%u nullRejected=1 valid=1\\n",unsigned(enabled),checks);
    configure();
  }''')
manifest=json.loads((source/'paint-raster-manifest.json').read_text(encoding='utf-8-sig'))
manifest.update(experiment='A10 public engine paint capability and output',
                paintBuildEnabled=args.enabled,
                images='Engine whole-image selection; no private wrapper replacement',
                limitations=['no generated-game paint lifecycle yet','no physical performance qualification'])
import hashlib
manifest['harnessSha256']=hashlib.sha256(header.read_bytes()).hexdigest()
(out/'paint-raster-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out)
