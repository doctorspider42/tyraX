"""Private road-only partial-strip refinement, constant VU paint in all arms."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
out,source=map(lambda p:Path(p).resolve(),sys.argv[1:3])
reverse='--reverse' in sys.argv
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-vu-paint.py'),str(out),str(source),'--roads-day']+
               (['--reverse'] if reverse else []),check=True)
def edit(path,old,new):
    text=path.read_text()
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())

engine=out/'tyra/engine'
probe=engine/'inc/debug/motion_probe.hpp'
edit(out/'game/src/gen/game_physics.gen.cpp',
     'part.envTexBag->paintVu = MotionProbe::variant() == 6;',
     'part.envTexBag->paintVu = true;')
edit(out/'game/src/gen/game_vehicles.gen.cpp',
     '    if (c.owner != -3 || !c.bag || c.bag->count == 0) continue;',
     '''    if (c.owner != -3 || !c.bag || c.bag->count == 0) continue;
    c.bag->refineStrips = c.bag->stripped && MotionProbe::variant()==6;''')
edit(probe,'inline bool observing(){return false;}', '''inline const void* roadBags[256]{};
inline U roadBagCount=0,roadPartial=0,roadRefined=0;
inline void roadWitness(const void* bag,bool refined) {
  if(offset<1000 || offset>=1100) return;
  for(U i=0;i<roadBagCount;++i) if(roadBags[i]==bag) {
    if(refined) ++roadRefined; else ++roadPartial;
    break;
  }
}
inline bool observing(){return false;}''')
edit(out/'game/src/terrain_game.cpp',
     '  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {',
     '''  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1000) {
    MotionProbe::roadBagCount=MotionProbe::roadPartial=MotionProbe::roadRefined=0;
    for(const auto& c:procChunks) if(c.owner==-3 && c.bag && c.bag->count) {
      TYRA_ASSERT(MotionProbe::roadBagCount<256,"Road witness capacity");
      MotionProbe::roadBags[MotionProbe::roadBagCount++]=c.bag.get();
    }
  }
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    printf("LOG: ROADREFINE phase=%u bags=%u partial=%u refined=%u\\n",
        MotionProbe::phase,MotionProbe::roadBagCount,MotionProbe::roadPartial,MotionProbe::roadRefined);
    if(!MotionProbe::roadPartial || (MotionProbe::variant()==6 ? !MotionProbe::roadRefined : MotionProbe::roadRefined!=0)) MotionProbe::valid=false;''')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'      if (pkg.bag->refineStrips &&',
     '      MotionProbe::roadWitness(pkg.bag,false);\n      if (pkg.bag->refineStrips &&')
edit(core,'          StaPipStripSubranges::Route routes[32];',
     '          MotionProbe::roadWitness(pkg.bag,true);\n          StaPipStripSubranges::Route routes[32];')
if '--verify' in sys.argv:
    edit(engine/'inc/renderer/3d/pipeline/static/core/stapip_strip_subranges.hpp',
         '#define TYRA_STAPIP_SUBRANGES_VERIFY 0','#define TYRA_STAPIP_SUBRANGES_VERIFY 1')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-refine',
    modes=[6,0,6] if reverse else [0,6,0],constantVuPaint=True,verify='--verify' in sys.argv,
    sourceProject=str(source),files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
