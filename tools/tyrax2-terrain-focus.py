"""A28: separate terrain residency/LOD focus from visual camera shake."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path)
p.add_argument('--reverse', action='store_true', help='Candidate/baseline/candidate travel order')
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('quiet', repo/'tools/tyrax2-quiet-fixture.py')
q = importlib.util.module_from_spec(spec)
spec.loader.exec_module(q)
assert not a.out.exists()
binding = json.loads((a.source/'native-binding.json').read_text())
for rel, digest in binding['files'].items():
    assert q.sha(a.source/rel) == digest, rel

def ignore(path, names):
    return q.ignore(path, names) + [n for n in names if n.startswith(('tape-', 'command-')) and n.endswith('.bin')]

for part in ('game', 'tyra/engine'):
    shutil.copytree(a.source/part, a.out/part, ignore=ignore)
for rel in ('tyra/Makefile.base', 'authored-manifest.json'):
    shutil.copy2(a.source/rel, a.out/rel)

def edit(path, old, new):
    text = path.read_text()
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_text(text.replace(old, new))

engine = a.out/'tyra/engine'
(engine/'inc/debug/terrain_focus.hpp').write_text('''#pragma once
namespace TerrainFocus {
inline float x=0,z=0;
inline bool checkpoint=false;
}
''')
probe = engine/'inc/debug/motion_probe.hpp'
edit(probe, '#include <stdio.h>', '#include <stdio.h>\n#include "debug/terrain_focus.hpp"')
edit(probe, 'OrdinaryLog::enabled=variant()!=7 || offset<gasStart || finishPending;',
     'OrdinaryLog::enabled=offset<gasStart || finishPending;')
edit(probe, 'printf("LOGWINDOWBEGIN phase=%u quiet=%u\\n",phase,unsigned(variant()==7));',
     'printf("LOGWINDOWBEGIN phase=%u quiet=1 mode=%u\\n",phase,variant());')

game = a.out/'game/src/terrain_game.cpp'
edit(game, '  if (scriptCtx.shakeAmp >= 0.0F) {', '''  // Visual perturbations must not move the terrain residency/LOD focus.
  TerrainFocus::x=cameraLookAt.x;TerrainFocus::z=cameraLookAt.z;
  if (scriptCtx.shakeAmp >= 0.0F) {''')
physics = a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics, '    updateTerrainChunks(cameraLookAt.x, cameraLookAt.z,', '''    const bool stableTerrainFocus=ClipTrial::mode==7 || TerrainFocus::checkpoint;
    updateTerrainChunks(stableTerrainFocus?TerrainFocus::x:cameraLookAt.x,
                        stableTerrainFocus?TerrainFocus::z:cameraLookAt.z,''')
if a.reverse:
    edit(physics, 'stableTerrainFocus=ClipTrial::mode==7 || TerrainFocus::checkpoint;',
         'stableTerrainFocus=ClipTrial::mode!=7 || TerrainFocus::checkpoint;')
# Identical held input focus is the cold equality contract, not moving LOD.
edit(physics, '  FastJoin::checkpoint=true;FastJoin::interleaved=heavyActive;',
     '  TerrainFocus::checkpoint=true;\n  FastJoin::checkpoint=true;FastJoin::interleaved=heavyActive;')
edit(physics, 'for(unsigned frame=0;frame<8;++frame){', 'for(unsigned frame=0;frame<32;++frame){')
edit(physics, 'ClipConsumer::observe=frame==7;', 'ClipConsumer::observe=frame==31;')
edit(physics, '    ClipConsumer::observe=false;\n    printf("HEADERORACLE', '''    ClipConsumer::observe=false;
    unsigned focusChecks=0;
    for(const auto& ch:terrainChunks){
      if(ch.cx<0)continue;
      TYRA_ASSERT(ch.lod==terrainLodStep(ch.cx,ch.cz) &&
        ch.lodW==terrainLodStep(ch.cx-1,ch.cz) &&
        ch.lodE==terrainLodStep(ch.cx+1,ch.cz) &&
        ch.lodN==terrainLodStep(ch.cx,ch.cz-1) &&
        ch.lodS==terrainLodStep(ch.cx,ch.cz+1),"A28 stale held-focus terrain/edge contract");
      ++focusChecks;
    }
    TYRA_ASSERT(focusChecks>0,"A28 missing held-focus terrain");
    printf("TERRAINFOCUSCHECK phase=%u arm=%u checks=%u x=%f z=%f\\n",MotionProbe::phase,arm,focusChecks,TerrainFocus::x,TerrainFocus::z);fflush(stdout);
    printf("HEADERORACLE''')
edit(physics, '  FastJoin::checkpoint=false;ClipTrial::mode=active;g_frameDt=dt;',
     '  TerrainFocus::checkpoint=false;FastJoin::checkpoint=false;ClipTrial::mode=active;g_frameDt=dt;')
plan = json.loads((a.source/'clip-capacity-manifest.json').read_text())
plan.update(experiment='A28', ordinaryLogGate=False, terrainFocus=True, reverse=a.reverse,
    scope=('0' if a.reverse else '7')+' uses pre-shake terrain residency/LOD focus; ordinary logs quiet in BOTH gas arms; visual shake/normal LOD/edges/roads/live reflections retained',
    raster='all cold arms share held pre-shake terrain focus, 32 settle frames and exact main/env images; moving LOD raster changes expected')
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan, indent=2)+'\n')
print(a.out)
