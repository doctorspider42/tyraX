"""Compile actual FPP/orbit terrain-focus snapshots and streaming call on host."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path)
a = p.parse_args()
templates = (repo/'src/templates.cpp').read_text()
source = (repo/'src/game_templates.inc').read_text()
declaration = '  float terrainFocusX = 0.0F, terrainFocusZ = 0.0F;'
assert templates.count(declaration) == 2
marker = '  // Visual shake must not move the terrain residency/LOD focus.\n'
assert source.count(marker) == 2
snapshots = []
for part in source.split(marker)[1:]:
    snapshots.append(part[:part.index('  // Camera Shake')])
assert snapshots[0] == snapshots[1]
assert source.count('updateTerrainChunks(terrainFocusX, terrainFocusZ,') == 1
assert 'updateTerrainChunks(cameraLookAt.x, cameraLookAt.z,' not in source
anchor = source.index('    const bool p2Focus = playerTwoActive && players[1].objIndex >= 0;')
start = source.rfind('  if (!splitSecondPass) {', 0, anchor)
end = source.index('\n  }\n  renderTerrain();', anchor) + len('\n  }')
stream = source[start:end]
snap_start = source.index('    auto snap = [](float v, float step) {')
snap_end = source.index('\n    };', snap_start) + len('\n    };')
snap = source[snap_start:snap_end]
test = r'''
#include <cassert>
#include <cmath>
struct Point { float x=0,z=0; };
struct Player { int objIndex=-1; float x=0,z=0; };
struct Game {
  Point cameraLookAt;
  Player players[2];
  bool splitSecondPass=false,playerTwoActive=false;
  int calls=0,budget=0;
  float fx=0,fz=0,p2x=0,p2z=0;
  bool two=false;
@DECL@
  void fpp(){
@FPP@
  }
  void orbit(){
@ORBIT@
  }
  void updateTerrainChunks(float x,float z,float x2,float z2,bool pair,int n){
    ++calls;fx=x;fz=z;p2x=x2;p2z=z2;two=pair;budget=n;
  }
  void stream(){
@STREAM@
  }
};
int main(){
@SNAP@
  for(auto snapshot : {&Game::fpp,&Game::orbit}){
    Game g;
    for(int i=0;i<240;++i){
      g.cameraLookAt={0,-74};(g.*snapshot)();
      g.cameraLookAt.x=(i&1)?-.007F:.007F;
      g.cameraLookAt.z+=.001F;
      g.stream();
      assert(g.fx==0 && g.fz==-74 && g.budget==2 && !g.two);
      assert(snap(g.fx,32)==16);
      assert(snap(g.cameraLookAt.x,32)==((i&1)?-16:16));
    }
    g.cameraLookAt={-42,99};(g.*snapshot)();
    g.cameraLookAt={1000,-1000};g.stream();
    assert(g.fx==-42 && g.fz==99);
    g.playerTwoActive=true;g.players[1]={1,42,-17};g.stream();
    assert(g.two && g.p2x==42 && g.p2z==-17);
    int calls=g.calls;g.splitSecondPass=true;g.stream();assert(g.calls==calls);
    g.splitSecondPass=false;g.players[1].objIndex=-1;g.stream();
    assert(!g.two && g.p2x==0 && g.p2z==0);
  }
}
'''
for key, value in {'DECL': declaration, 'FPP': snapshots[0], 'ORBIT': snapshots[1], 'STREAM': stream, 'SNAP': snap}.items():
    test = test.replace('@'+key+'@', value)
test = test.replace('#include <cmath>', '#include <cmath>\n#include <initializer_list>')
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.cpp').write_text(test)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(path/'test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
result = dict(passed=True, checks=['both actual snapshots', '480 boundary-jitter frames', 'fresh moved/cutscene target', 'player two focus', 'split second-pass exclusion', 'inactive second player'],
    files={name: hashlib.sha256((repo/name).read_bytes()).hexdigest() for name in ('src/templates.cpp', 'src/game_templates.inc')})
if a.output:
    a.output.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
