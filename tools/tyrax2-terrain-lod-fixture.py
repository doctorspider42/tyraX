"""Private budget-preserving effective neighbour LOD A/B, native full producer oracle."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parent
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
b=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(a.source/rel)==digest,rel
for rel,key in [('game/bin/vehicle-playground.elf','elfSha256'),('tyra/engine/bin/libtyra.a','engineArchiveSha256')]:
    assert sha(a.source/rel)==b[key]
for rel in ('world-cell-price-emulator.json','physical-v1/world-cell-price-ps2.json'):
    report=json.loads((a.source/rel).read_text());assert report['valid'] and report['variant']=='audio-event'
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
      (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
      (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
helpers={name:sha(repo/name) for name in ('tyrax2-terrain-lod-policy.hpp','tyrax2-terrain-lod-key.hpp','tyrax2-terrain-lod-audit.inc')}
for name,target in [('tyrax2-terrain-lod-policy.hpp','terrain_lod_policy.hpp'),('tyrax2-terrain-lod-key.hpp','terrain_lod_key.hpp')]:
    shutil.copy2(repo/name,a.out/'game/inc'/target)
def edit(rel,old,new):
    f=a.out/rel;t=f.read_text();assert t.count(old)==1,(rel,old);f.write_text(t.replace(old,new))
moving='game/inc/world_moving.hpp';game='game/src/terrain_game.cpp';physics='game/src/gen/game_physics.gen.cpp'
edit(moving,'#pragma once','#pragma once\n#include "terrain_lod_policy.hpp"')
edit(moving,'AudioIdle::set(p<6 && modes[p]);','AudioIdle::set(true);TerrainLodPolicy::enabled=p<6 && modes[p];')
edit(moving,'WheelVu::reset();AudioIdle::reset();','WheelVu::reset();AudioIdle::reset();TerrainLodPolicy::reset();TerrainLodPolicy::qualified=false;')
edit(moving,'variant=audio-event\\n",p,unsigned(AudioIdle::enabled));','variant=terrain-lod\\n",p,unsigned(TerrainLodPolicy::enabled));')
edit(moving,'WheelVu::witness(phase);AudioIdle::witness(phase);','WheelVu::witness(phase);AudioIdle::witness(phase);TerrainLodPolicy::witness(phase);')
edit('game/inc/terrain_game.hpp','  bool compileCompleteTerrainChunk(TerrainChunk& ch);','  void auditTerrainLodKey();\n  bool compileCompleteTerrainChunk(TerrainChunk& ch);')
edit(game,'  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;',
     '  if(MotionProbe::phase<6 && MotionProbe::offset==1099)auditTerrainLodKey();\n  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;')
edit(game,'  const bool movingAudio=AudioIdle::enabled;','  const bool movingAudio=AudioIdle::enabled;\n  const bool movingLod=TerrainLodPolicy::enabled;')
edit(game,'AudioIdle::set(arm==1?!movingAudio:movingAudio);','AudioIdle::set(true);TerrainLodPolicy::enabled=arm==1?!movingLod:movingLod;')
edit(game,'AudioIdle::set(movingAudio);','AudioIdle::set(movingAudio);TerrainLodPolicy::enabled=movingLod;')
edit(physics,'  ch.lodS = (unsigned char)terrainLodStep(cx, cz + 1);',
'''  ch.lodS = (unsigned char)terrainLodStep(cx, cz + 1);
  if(TerrainLodPolicy::forcing){
    ch.lodW=TerrainLodPolicy::forced.lodW;ch.lodE=TerrainLodPolicy::forced.lodE;
    ch.lodN=TerrainLodPolicy::forced.lodN;ch.lodS=TerrainLodPolicy::forced.lodS;
  }''')
edit(physics,'    buildTerrainChunk(slot, terrainChunks[slot].cx, terrainChunks[slot].cz);',
'''    auto& ch=terrainChunks[slot];
    const TerrainLodKey::Key old={ch.lod,ch.lodW,ch.lodE,ch.lodN,ch.lodS};
    const TerrainLodKey::Key next={terrainLodStep(ch.cx,ch.cz),terrainLodStep(ch.cx-1,ch.cz),
      terrainLodStep(ch.cx+1,ch.cz),terrainLodStep(ch.cx,ch.cz-1),terrainLodStep(ch.cx,ch.cz+1)};
    if(completeTerrainSlots)++TerrainLodPolicy::events;
    const bool staticShade=g_activeScene<2 && !TERRAIN_LAYER_COUNT && !SCENE_PROBES && !SCENE_EMIS &&
      !terrainMapLit && !SCENE_AO_MAP_LIT && !SCENE_AO_MAP_GI && !SCENE_AO_MAP_GILUM && g_emisLocal.empty();
    const bool sameTexture=TERRAIN_TEXTURE<0 ? ch.sts.empty() : ch.texBag.texture==loadedTextures[TERRAIN_TEXTURE];
    if(TerrainLodPolicy::enabled && TerrainLodPolicy::qualified && staticShade && sameTexture &&
       ch.completeCell.owner && TerrainLodKey::equivalent(old,next,ch.cx,ch.cz,terrainChunksX,terrainChunksZ)){
      ch.lodW=next.lodW;ch.lodE=next.lodE;ch.lodN=next.lodN;ch.lodS=next.lodS;
      ++TerrainLodPolicy::skipped;--budget;continue;
    }
    buildTerrainChunk(slot, ch.cx, ch.cz);''')
f=a.out/physics;f.write_text(f.read_text()+'\n'+(repo/'tyrax2-terrain-lod-audit.inc').read_text())
for name,digest in helpers.items():assert sha(repo/name)==digest
m=json.loads((a.source/'world-cell-price-manifest.json').read_text())
m.update(variant='terrain-lod',baseElfSha256=b['elfSha256'],generatorSha256=sha(Path(__file__)),lodHelpers=helpers,
         status='UNQUALIFIED',comparison='common direct/paint/wheels/event audio: raw versus effective neighbour dirty key',
         limits=['Metadata no-op consumes original budget and refreshes all raw neighbours.',
                 'Native full producer oracle before preparation in every phase; scene-bound static shading guard.',
                 'Original geometry encoding, immutable owners and all live reflection/pose/image gates retained.'])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print(a.out,'UNQUALIFIED terrain neighbour LOD price fixture')
