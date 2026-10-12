"""Source-bind actual terrain edge interpolation against effective neighbour keys."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture', type=Path)
p.add_argument('out', type=Path)
a = p.parse_args()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
b = json.loads((a.fixture / 'native-binding.json').read_text())
for rel, digest in b['files'].items():
    assert sha(a.fixture / rel) == digest, rel
assert sha(a.fixture / 'game/bin/vehicle-playground.elf') == b['elfSha256']
source = a.fixture / 'game/src/gen/game_physics.gen.cpp'
t = source.read_text()
start = t.index('  struct EdgeMix {', t.index('void TerrainGame::buildTerrainChunk('))
end = t.index('  // Quads this chunk really emits', start)
edge = t[start:end]
assert edge.count('auto edgeMix =') == edge.count('auto hAtE =') == edge.count('auto shadeAtE =') == 1
assert not a.out.exists()
a.out.mkdir(parents=True)
repo = Path(__file__).resolve().parent
for name in ('tyrax2-terrain-lod-key.hpp', 'verify-terrain-lod-key.py'):
    shutil.copy2(repo / name, a.out / name)
shutil.copy2(source, a.out / 'game_physics.gen.cpp')
test = r'''
#include "tyrax2-terrain-lod-key.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using TerrainLodKey::Key;
struct V3 { float x,y,z; };
struct Value { int a,b;float t,height;bool alongZ,snap;V3 shade; };
static Value actual(Key ch,int cx,int cz,int extentX,int extentZ,int ix,int iz){
  const int terrainChunksX=5,terrainChunksZ=5,lod=ch.lod;
  const int gx0=cx*16,gz0=cz*16,gx1=gx0+extentX,gz1=gz0+extentZ;
  auto hAt=[](int x,int z){return float((x*x*13+z*67+x*z*31)%1009)*0.0137F;};
  auto shadeAt=[](int x,int z){return V3{float((x*11+z*17)%137)*0.0061F,
    float((x*x+z*23)%149)*0.0053F,float((x*31+z*z)%163)*0.0047F};};
__ACTUAL_EDGE__
  const auto m=edgeMix(ix,iz);return {m.a,m.b,m.t,hAtE(ix,iz),m.alongZ,m.snap,shadeAtE(ix,iz)};
}
static bool same(Value a,Value b){return a.a==b.a && a.b==b.b && a.t==b.t &&
  a.alongZ==b.alongZ && a.snap==b.snap && !std::memcmp(&a.height,&b.height,sizeof(float)) &&
  !std::memcmp(&a.shade,&b.shade,sizeof(V3));}
static Key key(int lod,unsigned code){
  const std::array<int,3> values{1,2,4};Key k{lod,0,0,0,0};
  k.lodW=values[code%3];code/=3;k.lodE=values[code%3];code/=3;
  k.lodN=values[code%3];code/=3;k.lodS=values[code%3];return k;
}
int main(){
  unsigned admitted=0,rejected=0,points=0,changedMetadata=0;
  for(int lod:{1,2,4})for(int cx:{0,2,4})for(int cz:{0,2,4})
  for(unsigned first=0;first<81;++first)for(unsigned second=0;second<81;++second){
    const auto a=key(lod,first),b=key(lod,second);
    if(!TerrainLodKey::equivalent(a,b,cx,cz,5,5)){++rejected;continue;}
    ++admitted;if(first!=second)++changedMetadata;
    for(auto extent:std::array<std::array<int,2>,2>{{{16,16},{15,11}}})
      for(int z=cz*16;z<=cz*16+extent[1];++z)
        for(int x=cx*16;x<=cx*16+extent[0];++x){
          assert(same(actual(a,cx,cz,extent[0],extent[1],x,z),
                      actual(b,cx,cz,extent[0],extent[1],x,z)));++points;
        }
  }
  for(int lod:{1,2,4})for(int other:{1,2,4})
    if(lod!=other)assert(!TerrainLodKey::equivalent(key(lod,0),key(other,0),2,2,5,5));
  assert(changedMetadata>0);
  std::printf("PASS admittedPairs=%u rejectedPairs=%u points=%u rawNeighbourChanges=%u actualEdgeSource=1 heightShadeExact=1\n",
    admitted,rejected,points,changedMetadata);
}
'''.replace('__ACTUAL_EDGE__', edge)
(a.out / 'verify.cpp').write_text(test)
logs = {}
for mode, flags in [('optimized', ['-O2']), ('sanitized', ['-O1', '-g',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
    binary = a.out / f'verify-{mode}'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                    str(a.out / 'verify.cpp'), '-o', str(binary)], check=True)
    r = subprocess.run([str(binary)], capture_output=True, text=True)
    (a.out / f'{mode}.log').write_text(r.stdout + r.stderr)
    assert r.returncode == 0, r.stderr
    logs[mode] = r.stdout
    print(mode, r.stdout.strip())
assert logs['optimized'] == logs['sanitized']
report = dict(valid=True, elfSha256=b['elfSha256'], producerSha256=sha(source),
              helperSha256=sha(repo / 'tyrax2-terrain-lod-key.hpp'), logs=logs,
              limits=['Actual edge lambda, synthetic stable height/shade functions only.',
                      'No entire producer/material/light side-effect equivalence or PS2 FPS proof.',
                      'Moving trial must preserve budget/order and update raw neighbour metadata.',
                      'Skipping full build needs shading-input validity and native full-stream oracle.'])
(a.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
pins = {f.name: sha(f) for f in a.out.iterdir() if f.is_file()}
(a.out / 'sha256.json').write_text(json.dumps(pins, indent=2) + '\n')
print('PASS frozen source-bound component; native whole-producer/net gates remain')
