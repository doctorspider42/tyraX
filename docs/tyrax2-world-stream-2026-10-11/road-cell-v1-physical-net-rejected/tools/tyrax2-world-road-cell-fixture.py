"""Extend qualified terrain cells to owned roads without changing heavy ordering."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path)
p.add_argument('--physical-attempt', type=Path, required=True)
a = p.parse_args()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding = json.loads((a.source/'native-binding.json').read_text())
for rel, digest in binding['files'].items():
    assert sha(a.source/rel) == digest, rel
assert sha(a.source/'game/bin/vehicle-playground.elf') == binding['elfSha256']
for platform, location in [('emulator', a.source), ('ps2', a.physical_attempt)]:
    gate = json.loads((location/f'world-cell-price-{platform}.json').read_text())
    assert gate['valid'] and gate['variant'] == 'skip-a31'
    assert gate['elfSha256'] == binding['elfSha256']

def ignore(path, names):
    return [n for n in names if n in ('obj', '.git') or
            (Path(path).name == 'bin' and (n in ('log.txt', 'drive-probe.csv') or
                                         n.startswith('triple-present-'))) or
            (n.startswith(('tape-', 'command-', 'cell-', 'source-', 'moving-', 'world-price-'))
             and n.endswith('.bin'))]

for part in ('game', 'tyra/engine'):
    shutil.copytree(a.source/part, a.out/part, ignore=ignore)
for rel in ('tyra/Makefile.base', 'authored-manifest.json'):
    shutil.copy2(a.source/rel, a.out/rel)

def edit(relative, old, new):
    file = a.out/relative
    text = file.read_text()
    assert text.count(old) == 1, (relative, old)
    file.write_text(text.replace(old, new))

edit('tyra/engine/inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp',
     'namespace Tyra {', 'namespace WorldCellComplete { struct Cell; }\n\nnamespace Tyra {')
edit('tyra/engine/inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp',
     '  StaPipBag();', '  WorldCellComplete::Cell* completeRoadCell=nullptr;\n  StaPipBag();')
edit('game/inc/terrain_game.hpp', '  struct ProcChunk {',
     '  bool prepareCompleteRoads();\n  struct ProcChunk {\n    std::unique_ptr<WorldCellComplete::Cell> completeRoadCell;')
edit('game/inc/world_moving.hpp', 'inline bool skipA31=false;',
     'inline bool skipA31=false,roadCells=false;\ninline unsigned roadRequested=0,roadSubmitted=0,roadFallback=0,roadCompiled=0;')
edit('game/inc/world_moving.hpp', '  enabled=p<6;skipA31=p<6 && modes[p];',
     '  enabled=p<6;skipA31=true;roadCells=p<6 && modes[p];')
edit('game/inc/world_moving.hpp', '  legacyCalls=legacyPrepared=0;',
     '  legacyCalls=legacyPrepared=0;\n  roadRequested=roadSubmitted=roadFallback=roadCompiled=0;')
edit('game/inc/world_moving.hpp', 'variant=skip-a31\\n",p,unsigned(skipA31));',
     'variant=road-cells\\n",p,unsigned(roadCells));')
edit('game/inc/world_moving.hpp', '  fflush(stdout);\n}\n}',
     '  printf("A44ROADPHASE phase=%u enabled=%u compiled=%u requested=%u submitted=%u fallback=%u\\n",phase,unsigned(roadCells),roadCompiled,roadRequested,roadSubmitted,roadFallback);\n  fflush(stdout);\n}\n}')
edit('game/src/gen/game_scene.gen.cpp', '#include "world_source_capture.hpp"',
     '#include "world_source_capture.hpp"\n#include "world_moving.hpp"')
edit('game/src/gen/game_scene.gen.cpp',
     'void TerrainGame::renderWorldBag(Tyra::StaPipBag* bag) {',
     '''void TerrainGame::renderWorldBag(Tyra::StaPipBag* bag) {
  if(WorldCellComplete::active && WorldMoving::roadCells && bag && bag->completeRoadCell){
    ++WorldMoving::roadRequested;++WorldCellComplete::requested;
    // Interleaved objects overwrite uniforms: restore live view before each road.
    if(stapip.core.beginCompleteWorldView() &&
       stapip.core.renderCompleteWorldCell(*bag->completeRoadCell)){
      ++WorldMoving::roadSubmitted;return;
    }
    ++WorldMoving::roadFallback;++WorldCellComplete::fallback;
  }''')
edit('game/src/gen/game_collision.gen.cpp', 'void TerrainGame::procFinishChunks() {',
     '''void TerrainGame::procFinishChunks() {
  for(auto& c:procChunks){
    if(c.bag)c.bag->completeRoadCell=nullptr;
    c.completeRoadCell.reset();
  }''')
edit('game/src/terrain_game.cpp', 'void TerrainGame::releaseCompleteTerrain(){',
     '''void TerrainGame::releaseCompleteTerrain(){
  for(auto& c:procChunks){
    if(c.bag)c.bag->completeRoadCell=nullptr;
    c.completeRoadCell.reset();
  }''')
edit('game/src/terrain_game.cpp', '  // These masks already have scene-owned EE references, including future ring cells.',
     '''  if(WorldMoving::roadCells)
    for(auto& c:procChunks)if(c.owner==-3)add(c.bag.get());
  // These masks already have scene-owned EE references, including future ring cells.''')
edit('game/src/terrain_game.cpp',
     '    TYRA_ASSERT(prepareCompleteTerrain(),"A44 moving pre-record preparation failed");',
     '''    TYRA_ASSERT(prepareCompleteTerrain(),"A44 moving pre-record preparation failed");
    if(WorldMoving::roadCells)TYRA_ASSERT(prepareCompleteRoads(),"A44 road preparation failed");''')
edit('game/src/terrain_game.cpp', '  const bool movingCandidate=WorldMoving::enabled;',
     '  const bool movingCandidate=WorldMoving::enabled;\n  const bool movingRoad=WorldMoving::roadCells;')
edit('game/src/terrain_game.cpp', '  const bool candidate=arm==1?!movingCandidate:movingCandidate;',
     '  const bool candidate=true;\n  WorldMoving::roadCells=arm==1?!movingRoad:movingRoad;')
edit('game/src/terrain_game.cpp',
     '  if(candidate)TYRA_ASSERT(prepareCompleteTerrain(),"A44 price checkpoint preparation");',
     '''  if(candidate){
    TYRA_ASSERT(prepareCompleteTerrain(),"A44 price checkpoint preparation");
    if(WorldMoving::roadCells)TYRA_ASSERT(prepareCompleteRoads(),"A44 checkpoint road preparation");
  }''')
edit('game/src/terrain_game.cpp', '  WorldCellComplete::active=movingCandidate;',
     '  WorldMoving::roadCells=movingRoad;\n  WorldCellComplete::active=movingCandidate;')
file = a.out/'game/src/terrain_game.cpp'
with file.open('a') as f:
    f.write('''
namespace Vehicle_playground {
bool TerrainGame::prepareCompleteRoads(){
  if(!completeTerrainSlots)return false;
  for(auto& c:procChunks){
    if(c.owner!=-3 || !c.bag || !c.bag->count || c.completeRoadCell)continue;
    auto cell=std::make_unique<WorldCellComplete::Cell>();
    if(!stapip.core.compileCompleteWorldCell(*cell,{c.bag.get()}))return false;
    c.completeRoadCell=std::move(cell);
    c.bag->completeRoadCell=c.completeRoadCell.get();
    c.bag->worldOwner.reset();
    ++WorldMoving::roadCompiled;
  }
  stapip.core.collectWorldOwners();return true;
}
}
''')
manifest = json.loads((a.source/'world-cell-price-manifest.json').read_text())
manifest.update(baseElfSha256=binding['elfSha256'], generatorSha256=sha(Path(__file__)),
                variant='road-cells', comparison='common skip-A31 terrain cells, ordinary roads versus complete road cells',
                status='UNQUALIFIED', limits=[
                    'Original distance/AABB/reflection/Auto heavy ordering remains unchanged.',
                    'Full six-plane clipping and independent strip seams retained.',
                    'Road cells own bytes; core/CLUT reservations released after actual GS fence.',
                    'Live uniforms restored at every interleaved road; no assumption of adjacent cells.',
                    'No measured saving, correctness/default or60fps claim before target gates.'])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(a.out, 'UNQUALIFIED road cells, requires native build and target gates')
