"""Private parked-car camera sweep; independent reflection interventions."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out,source=map(lambda p:Path(p).resolve(),sys.argv[1:3])
scatter='--scatter-copy' in sys.argv
modes=[0,5,0] if scatter else [0,4,3,1,0]
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-reflection-isolation.py'),str(out),str(source)]+
               (['--night-roads'] if '--night-roads' in sys.argv else []),check=True)
def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig');assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())
p=out/'tyra/engine/inc/debug/motion_probe.hpp'
edit(p,'inline constexpr U phaseCount=9;',f'inline constexpr U phaseCount={len(modes)};')
edit(p,'constexpr U modes[]={0,1,2,3,0,3,2,1,0};','constexpr U modes[]={'+','.join(map(str,modes))+'};')
edit(p,'if(current.z>=108.0F || size==1200) { finishPending=true; if(current.z<108.0F)valid=false; }',
     'if(offset==gasStart+719) { finishPending=true; }')
edit(p,'EnvEligible, EnvDrawn, EnvSkipped, Count','EnvEligible, EnvDrawn, EnvSkipped, PaintBlocked, Count')
edit(p,'envSkipped,captureFrozen','envSkipped,paintBlocked,captureFrozen')
game=out/'game/src/terrain_game.cpp'
edit(game,'void TerrainGame::loop() {','''void TerrainGame::loop() {
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    if(groundShadowPins.empty())MotionProbe::valid=false;
    printf("LOG: TURNPINS phase=%u count=%u\\n",MotionProbe::phase,unsigned(groundShadowPins.size()));
  }''')
veh=out/'game/src/gen/game_vehicles.gen.cpp'
edit(veh,'inThrottle=MotionProbe::gas()?1.0F:0.0F; inBrake=0.0F;',
     'inThrottle=0.0F; inBrake=0.0F;')
edit(veh,'inSteer=0.0F; inHand=MotionProbe::gas()?0:1; inNos=0;',
     'inSteer=0.0F; inHand=1; inNos=0;')
edit(veh,'      const float rigYaw =','''      // Deterministic supported-range glance, after stick integration but
      // before the ordinary chase-camera placement and collision handling.
      if(MotionProbe::gas()) {
        const unsigned t=(MotionProbe::offset-MotionProbe::gasStart)%240;
        vehCamOrbit_=t<=60 ? float(t) : (t<=180 ? 120.0F-float(t) : float(t)-240.0F);
      } else vehCamOrbit_=0.0F;
      const float rigYaw =''')
physics=out/'game/src/gen/game_physics.gen.cpp'
if scatter:
    edit(physics,'for (u32 k = 0; k < n; ++k) dst[k] = paintColors[k];',
         '''if(MotionProbe::variant()==5) {
            if(n) memcpy(dst[0].rgba, paintColors[0].rgba, n*sizeof(Tyra::Color));
          } else for (u32 k = 0; k < n; ++k) dst[k] = paintColors[k];''')
    edit(physics,'for (u32 k = 0; k < n; ++k) dst[k] = paintColors[map[k]];',
         '''if(MotionProbe::variant()==5) {
            for(u32 k=0;k<n;++k) memcpy(dst[k].rgba,paintColors[map[k]].rgba,sizeof(Tyra::Color));
          } else for (u32 k = 0; k < n; ++k) dst[k] = paintColors[map[k]];''')
edit(physics,'      if (paintChanged) {\n        MotionProbe::add(MotionProbe::Paint);',
     '''      if(paintChanged && MotionProbe::gas() && MotionProbe::variant()==4) {
        MotionProbe::add(MotionProbe::PaintBlocked);
        paintChanged=false;
      }
      if (paintChanged) {
        MotionProbe::add(MotionProbe::Paint);''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='parked-vehicle-reflection-turn',
    modes=modes,scatterCopy=scatter,warmup=1200,park=120,sweep=720,cycle=240,
    sourceProject=str(source),files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
