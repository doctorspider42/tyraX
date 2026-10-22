"""Private immutable terrain-normal input versus original normalization A/B."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
b=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==b['elfSha256']
assert sha(a.source/'tyra/engine/bin/libtyra.a')==b['engineArchiveSha256']
for rel in ('world-cell-price-emulator.json','physical-v1/world-cell-price-ps2.json'):
    r=json.loads((a.source/rel).read_text());assert r['valid'] and r['variant']=='audio-event'
    assert r['elfSha256']==b['elfSha256']
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
        (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
        (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
shutil.copy2(repo/'tyrax2-terrain-normals.hpp',a.out/'game/inc/terrain_normals.hpp')
def edit(rel,old,new):
    f=a.out/rel;t=f.read_text();assert t.count(old)==1,(rel,old);f.write_text(t.replace(old,new))
moving='game/inc/world_moving.hpp'
edit(moving,'#pragma once','#pragma once\n#include "terrain_normals.hpp"')
edit(moving,'AudioIdle::set(p<6 && modes[p]);','AudioIdle::set(true);TerrainNormals::enabled=p<6 && modes[p];')
edit(moving,'WheelVu::reset();AudioIdle::reset();','WheelVu::reset();AudioIdle::reset();TerrainNormals::reset();')
edit(moving,'variant=audio-event\\n",p,unsigned(AudioIdle::enabled));','variant=terrain-normals\\n",p,unsigned(TerrainNormals::enabled));')
edit(moving,'WheelVu::witness(phase);AudioIdle::witness(phase);','WheelVu::witness(phase);AudioIdle::witness(phase);TerrainNormals::witness(phase);')
game='game/src/terrain_game.cpp'
edit(game,'  const bool movingAudio=AudioIdle::enabled;','  const bool movingAudio=AudioIdle::enabled;\n  const bool movingNormals=TerrainNormals::enabled;')
edit(game,'AudioIdle::set(arm==1?!movingAudio:movingAudio);','AudioIdle::set(true);TerrainNormals::enabled=arm==1?!movingNormals:movingNormals;')
edit(game,'AudioIdle::witness(MotionProbe::phase,int(arm));','AudioIdle::witness(MotionProbe::phase,int(arm));TerrainNormals::witness(MotionProbe::phase,int(arm));')
edit(game,'AudioIdle::set(movingAudio);','AudioIdle::set(movingAudio);TerrainNormals::enabled=movingNormals;')
physics='game/src/gen/game_physics.gen.cpp'
old='''  auto shadeAt = [&](int ix, int iz) -> V3 {
    V3 n = {hAt(ix - 1, iz) - hAt(ix + 1, iz), 2.0F * (stepX < stepZ ? stepX : stepZ),
            hAt(ix, iz - 1) - hAt(ix, iz + 1)};
    const float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 0.00001F) n.x /= len, n.y /= len, n.z /= len;
    const V3 wp'''
new='''  auto originalNormal = [&](int ix,int iz) -> V3 {
    V3 n = {hAt(ix - 1, iz) - hAt(ix + 1, iz), 2.0F * (stepX < stepZ ? stepX : stepZ),
            hAt(ix, iz - 1) - hAt(ix, iz + 1)};
    const float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 0.00001F) n.x /= len, n.y /= len, n.z /= len;
    return n;
  };
  TerrainNormals::ensure(sceneGeneration,TERRAIN_HEIGHTS,HM_W,HM_D,stepX,stepZ,originalNormal);
  ++TerrainNormals::builds;if(TerrainNormals::enabled)++TerrainNormals::uses;
  auto shadeAt = [&](int ix, int iz) -> V3 {
    V3 n;
    if(TerrainNormals::enabled){const auto prepared=TerrainNormals::at(ix,iz);n={prepared.x,prepared.y,prepared.z};}
    else n=originalNormal(ix,iz);
    const V3 wp'''
edit(physics,old,new)
m=json.loads((a.source/'world-cell-price-manifest.json').read_text())
m.update(baseElfSha256=b['elfSha256'],generatorSha256=sha(Path(__file__)),variant='terrain-normals',
    terrainNormals=True,status='UNQUALIFIED',comparison='common accepted renderer/event audio: original normals versus immutable lattice',
    limits=['Original native normalization builds lattice once per scene generation in BOTH roles.',
            'No normal array is submitted to DMA; values copied into original live shading.',
            'Point/emissive/probe lighting, shading order, LOD and edge interpolation retained.',
            'One build-level activation witness, no priced per-vertex counters or clocks.',
            'Native exact lattice and full image/pose/lifetime/delivery gates required before physical both-order net.'])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print(a.out,'UNQUALIFIED immutable terrain normal input')
