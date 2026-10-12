"""Private VU/output bypass versus road no-kick, preserving EE submission."""
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
out,source=[Path(p).resolve() for p in sys.argv[1:3]]
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-road-submit.py'),
                str(out),str(source),'--no-kick'],check=True)

def edit(path,old,new):
    text=path.read_text()
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())

engine=out/'tyra/engine'
core=engine/'src/renderer/3d/pipeline/static/core'
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp',
     '  bool diagnosticNoKick = false;',
     '  bool diagnosticNoKick = false;\n  bool diagnosticVuBypass = false;')
proc=next(p for p in (out/'game/src').rglob('*.cpp') if 'c.bag->diagnosticNoKick=c.owner' in p.read_text())
edit(proc,'c.bag->diagnosticNoKick=c.owner==-3 && MotionProbe::variant()==6;',
     'c.bag->diagnosticNoKick=c.owner==-3;\n    c.bag->diagnosticVuBypass=c.owner==-3 && MotionProbe::variant()==6;')
edit(out/'game/src/gen/game_vehicles.gen.cpp',
     'c.bag->diagnosticNoKick==omit',
     'c.bag->diagnosticNoKick && c.bag->diagnosticVuBypass==omit')
edit(engine/'inc/debug/motion_probe.hpp','inline U roadAccepted=0, roadVertices=0, roadSkipped=0;',
     'inline U roadAccepted=0, roadVertices=0, roadSkipped=0, roadUniforms=0, roadBypassUniforms=0;')
edit(core/'stapip_qbuffer_renderer.cpp',
     'const s32 variantLane = paintVu ? 2 : (sharedClipVariant ? 1 : (spotActive ? -1 : 0));',
     '''if(bag->diagnosticNoKick) {
      TYRA_ASSERT(vu1Clipping && !repository.hasAnyOverride() && !bag->lighting &&
                  bag->texture && bag->texture->texture && !bag->texture->coordinatesAreNormals,
                  "Road bypass only supports ordinary textured color VU clipping");
      if(MotionProbe::offset>=1000 && MotionProbe::offset<1100) {
        ++MotionProbe::roadUniforms;
        if(bag->diagnosticVuBypass) ++MotionProbe::roadBypassUniforms;
      }
    }
    const s32 variantLane = bag->diagnosticVuBypass ? 3 :
        (paintVu ? 2 : (sharedClipVariant ? 1 : (spotActive ? -1 : 0)));''')
edit(out/'game/src/terrain_game.cpp',
     'MotionProbe::roadAccepted=MotionProbe::roadVertices=MotionProbe::roadSkipped=0;',
     'MotionProbe::roadAccepted=MotionProbe::roadVertices=MotionProbe::roadSkipped=MotionProbe::roadUniforms=MotionProbe::roadBypassUniforms=0;')
edit(out/'game/src/terrain_game.cpp','    printf("LOG: ROADRASTER phase=',
     '''    if(!MotionProbe::roadUniforms || (omit ? MotionProbe::roadBypassUniforms!=MotionProbe::roadUniforms : MotionProbe::roadBypassUniforms!=0)) MotionProbe::valid=false;
    printf("LOG: ROADVU phase=%u uniforms=%u bypass=%u\\n",MotionProbe::phase,MotionProbe::roadUniforms,MotionProbe::roadBypassUniforms);
    printf("LOG: ROADRASTER phase=''')

bypass='''    ; Private road VU/output bypass. Keep material state, emit zero primitives.
    ilw.y diagMode, 8(vi00)
    iaddiu diagWant, vi00, 3
    ibne diagMode, diagWant, diagContinue
    lq diagPrim, 1(buffer)
    lq diagWord, 19(vi00)
    sq diagWord, 0(buffer)
    sq diagWord, 2(buffer)
    sq diagWord, 4(buffer)
    sq diagWord, 6(buffer)
    lq diagWord, 10(vi00)
    sq diagWord, 1(buffer)
    lq diagWord, 9(vi00)
    sq diagWord, 3(buffer)
    lq diagWord, 11(vi00)
    sq diagWord, 5(buffer)
    lq diagWord, 21(vi00)
    sq diagWord, 7(buffer)
    sq diagPrim, 8(buffer)
    iaddiu diagZero, vi00, 16384
    iaddiu diagZero, diagZero, 16384
    isw.x diagZero, 8(buffer)
    xgkick buffer
    b diagYield
diagContinue:
'''
programs=['cull/stapip_cull_tc_vu1.vclpp','clip/stapip_clip_tc_vu1.vclpp',
          'clip/stapip_clip_tc_fold_vu1.vclpp']
for name in programs:
    path=core/'programs'/name
    text=path.read_text()
    match=re.search(r'begin:\n    xtop\s+buffer\n',text)
    assert match,path
    edit(path,match[0],match[0]+bypass)
    edit(path,'--barrier','diagYield:\n--barrier')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-vu-output-bypass',
    modes=[0,6,0],constantVuPaint=True,constantRoadNoKick=True,programs=programs,
    sourceProject=str(source),files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
