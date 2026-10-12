"""Road-free, same-bake 2x2 capture/body-pass intervention; scratch copies only."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve()
source=Path(sys.argv[2]).resolve()
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-motion-probe.py'),str(out),
               '--reflection-isolation','--source-project',str(source)]+
               [f for f in ('--night-roads','--roads-day','--night-no-roads','--day-no-roads') if f in sys.argv],check=True)
def edit(path,old,new):
    t=path.read_text(encoding='utf-8-sig');assert t.count(old)==1,(path,old,t.count(old))
    path.write_bytes(t.replace(old,new).encode())
p=out/'tyra/engine/inc/debug/motion_probe.hpp'
edit(p,'inline constexpr U phaseCount=6;','inline constexpr U phaseCount=9;')
t=p.read_text();a=t.index('inline U variant() {');b=t.index('inline U ticks()',a)
edit(p,t[a:b],'''inline U variant() {
  constexpr U modes[]={0,1,2,3,0,3,2,1,0};
  return phase<phaseCount?modes[phase]:0;
}
inline bool observing(){return false;}
''')
edit(p,'return (phase==1 || phase==4) && gas();','return (variant()==1 || variant()==2) && gas();')
edit(p,'inline void add(Counter c,U n=1) { if(enabled) counters[c]+=n; }',
     '''inline void add(Counter c,U n=1) {
  if(sample && (c==ProbeCapture || c==Paint || c==CaptureBlocked || c>=EnvEligible)) counters[c]+=n;
}
inline bool skipBodyEnv(){return gas() && (variant()==2 || variant()==3);}
''')
edit(p,'BarrierOmitted, Count','BarrierOmitted, EnvEligible, EnvDrawn, EnvSkipped, Count')
edit(p,'barrierOmitted,captureFrozen','barrierOmitted,envEligible,envDrawn,envSkipped,captureFrozen')
physics=out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'      if (!vehicleShineOn(objectIndex)) return;', '''      if (!vehicleShineOn(objectIndex)) return;
      MotionProbe::add(MotionProbe::EnvEligible);
      if(MotionProbe::skipBodyEnv()) { MotionProbe::add(MotionProbe::EnvSkipped); return; }''')
edit(physics,'    renderObjectBag(objectIndex, part.envBag.get());', '''    if(vehiclePaintFor(objectIndex)) MotionProbe::add(MotionProbe::EnvDrawn);
    renderObjectBag(objectIndex, part.envBag.get());''')
assert 'vehiclePaintFor(objectIndex)' in physics.read_text()
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-free-reflection-2x2',
    sourceProject=str(source),modes=[0,1,2,3,0,3,2,1,0],
    notes='Normal/frozen capture crossed with body env pass on/off; same shiny bake; sparse counters only.',
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
