"""Private road submission omission; retain road storage and physical receivers."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out, source = [Path(p).resolve() for p in sys.argv[1:3]]
context = '--night-roads' if '--night-roads' in sys.argv else '--roads-day'
no_kick = '--no-kick' in sys.argv
subprocess.run([sys.executable, str(ROOT/'tools/tyrax2-vu-paint.py'),
                str(out), str(source), context], check=True)

def edit(path, old, new):
    text = path.read_text()
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_bytes(text.replace(old, new).encode())

edit(out/'game/src/gen/game_physics.gen.cpp',
     'part.envTexBag->paintVu = MotionProbe::variant() == 6;',
     'part.envTexBag->paintVu = true;')
edit(out/'tyra/engine/inc/debug/motion_probe.hpp',
     'inline bool observing(){return false;}',
     'inline U roadAccepted=0, roadVertices=0, roadSkipped=0;\ninline bool observing(){return false;}')
edit(out/'game/src/gen/game_vehicles.gen.cpp',
     '    submitHeavy(c.bag.get());', '''    const bool omit=MotionProbe::variant()==6;
    if(MotionProbe::offset>=1000 && MotionProbe::offset<1100) {
      ++MotionProbe::roadAccepted;
      MotionProbe::roadVertices+=c.bag->count;
      if(omit) ++MotionProbe::roadSkipped;
    }
    if(!omit) submitHeavy(c.bag.get());''')
edit(out/'game/src/terrain_game.cpp',
     '  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {',
     '''  if(MotionProbe::offset==1000)
    MotionProbe::roadAccepted=MotionProbe::roadVertices=MotionProbe::roadSkipped=0;
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    const bool omit=MotionProbe::variant()==6;
    if(!MotionProbe::roadAccepted || !MotionProbe::roadVertices ||
       (omit ? MotionProbe::roadSkipped!=MotionProbe::roadAccepted : MotionProbe::roadSkipped!=0))
      MotionProbe::valid=false;
    printf("LOG: ROADSUBMIT phase=%u accepted=%u vertices=%u skipped=%u\\n",
      MotionProbe::phase,MotionProbe::roadAccepted,MotionProbe::roadVertices,MotionProbe::roadSkipped);''')
if no_kick:
    engine=out/'tyra/engine'
    edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp',
         '  StaPipBag();', '  bool diagnosticNoKick = false;\n  StaPipBag();')
    proc=next(p for p in (out/'game/src').rglob('*.cpp') if '    c.colors.bind(c.colorBag);' in p.read_text())
    edit(proc,'    c.colors.bind(c.colorBag);',
         '    c.colors.bind(c.colorBag);\n    c.bag->diagnosticNoKick=c.owner==-3 && MotionProbe::variant()==6;')
    edit(out/'game/src/gen/game_vehicles.gen.cpp',
         '    if(!omit) submitHeavy(c.bag.get());',
         '    TYRA_ASSERT(c.bag->diagnosticNoKick==omit,"Road no-kick selection");\n    submitHeavy(c.bag.get());')
    edit(out/'game/src/terrain_game.cpp','LOG: ROADSUBMIT phase=', 'LOG: ROADRASTER phase=')
    program=engine/'src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp'
    edit(program,'    packet2_utils_gs_add_prim_giftag(packet, &bufferPrim,', '''    u32 outputRegs=reglist;
    if(buffer->bag->diagnosticNoKick) {
      unsigned changed=0;
      for(unsigned lane=0;lane<reglistCount;++lane) {
        const unsigned shift=lane*4;
        if(((outputRegs>>shift)&15u)==GIF_REG_XYZF2) {
          outputRegs=(outputRegs & ~(15u<<shift)) | (GIF_REG_XYZF3<<shift);
          ++changed;
        }
      }
      TYRA_ASSERT(changed==1,"Road no-kick requires exactly one XYZF2");
    }
    packet2_utils_gs_add_prim_giftag(packet, &bufferPrim,''')
    edit(program,'gsVertexCount(buffer->size), reglist,','gsVertexCount(buffer->size), outputRegs,')
    q=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    text=q.read_text()
    old='(static_cast<u32>(prim->colorfix) << 11);'
    assert text.count(old)==2
    q.write_bytes(text.replace(old,'(static_cast<u32>(prim->colorfix) << 11) |\n                (static_cast<u32>(bag->diagnosticNoKick) << 12);').encode())
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-submit-omission',
    context=context,modes=[0,6,0],noKick=no_kick,constantVuPaint=True,sourceProject=str(source),
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
