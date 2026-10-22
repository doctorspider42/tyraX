"""Cold world input encoding A/B with unchanged common direct/paint/wheels/audio."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path)
p.add_argument('native_evidence', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parent
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
helpers = {name: sha(repo / name) for name in ('tyrax2-world-input-policy.hpp', 'tyrax2-world-input-compact.hpp')}
evidence = a.native_evidence.resolve()
for rel, digest in json.loads((evidence / 'sha256.json').read_text()).items():
    assert sha(evidence / rel) == digest, rel
for rel in ('world-input-native-emulator.json', 'physical/world-input-native-ps2.json'):
    r = json.loads((evidence / rel).read_text())
    assert r['valid'] and r['compactVifExecuted'] and r['restored'] and not r['shaderExecuted']
assert not a.out.exists()
b = json.loads((a.source / 'native-binding.json').read_text())
for rel, digest in b['files'].items():
    assert sha(a.source / rel) == digest, rel
assert sha(a.source / 'game/bin/vehicle-playground.elf') == b['elfSha256']
assert sha(a.source / 'tyra/engine/bin/libtyra.a') == b['engineArchiveSha256']
for rel in ('world-cell-price-emulator.json', 'physical-v1/world-cell-price-ps2.json'):
    r = json.loads((a.source / rel).read_text())
    assert r['valid'] and r['variant'] == 'audio-event' and r['elfSha256'] == b['elfSha256']

def ignore(path, names):
    return [n for n in names if n in ('obj', '.git') or
            (Path(path).name == 'bin' and (n in ('log.txt', 'drive-probe.csv') or
                                         n.startswith('triple-present-'))) or
            (n.startswith(('tape-', 'command-', 'cell-', 'source-', 'moving-', 'world-price-')) and n.endswith('.bin'))]

for part in ('game', 'tyra/engine'):
    shutil.copytree(a.source / part, a.out / part, ignore=ignore)
for rel in ('tyra/Makefile.base', 'authored-manifest.json'):
    shutil.copy2(a.source / rel, a.out / rel)
for name, target in [('tyrax2-world-input-policy.hpp', 'world_input_policy.hpp'),
                     ('tyrax2-world-input-compact.hpp', 'world_input_compact.hpp')]:
    shutil.copy2(repo / name, a.out / 'tyra/engine/inc/debug' / target)

def edit(rel, old, new):
    f = a.out / rel
    t = f.read_text()
    assert t.count(old) == 1, (rel, old)
    f.write_text(t.replace(old, new))

moving = 'game/inc/world_moving.hpp'
edit(moving, '#pragma once', '#pragma once\n#include "debug/world_input_policy.hpp"')
edit(moving, 'AudioIdle::set(p<6 && modes[p]);', 'AudioIdle::set(true);WorldInputPolicy::enabled=p<6 && modes[p];')
edit(moving, 'WheelVu::reset();AudioIdle::reset();', 'WheelVu::reset();AudioIdle::reset();WorldInputPolicy::reset();')
edit(moving, 'variant=audio-event\\n",p,unsigned(AudioIdle::enabled));',
     'variant=input-compact\\n",p,unsigned(WorldInputPolicy::enabled));')
edit(moving, 'WheelVu::witness(phase);AudioIdle::witness(phase);',
     'WheelVu::witness(phase);AudioIdle::witness(phase);WorldInputPolicy::witness(phase);')

game = 'game/src/terrain_game.cpp'
edit(game, 'stapip.setRenderer(&engine->renderer.core);',
     'stapip.setRenderer(&engine->renderer.core);WorldInputPolicy::initialize();')
edit(game, '  const bool movingAudio=AudioIdle::enabled;',
     '  const bool movingAudio=AudioIdle::enabled;\n  const bool movingInput=WorldInputPolicy::enabled;')
edit(game, 'AudioIdle::set(arm==1?!movingAudio:movingAudio);',
     'AudioIdle::set(true);WorldInputPolicy::enabled=arm==1?!movingInput:movingInput;WorldInputPolicy::reset();')
edit(game, 'AudioIdle::witness(MotionProbe::phase,int(arm));',
     'AudioIdle::witness(MotionProbe::phase,int(arm));WorldInputPolicy::witness(MotionProbe::phase,int(arm));')
edit(game, 'AudioIdle::set(movingAudio);',
     'AudioIdle::set(movingAudio);WorldInputPolicy::enabled=movingInput;')
edit('game/inc/terrain_game.hpp', 'bool completeTerrainFog=false;',
     'bool completeTerrainFog=false,completeTerrainCompact=false;')
edit(game, 'completeTerrainDepth!=depth || completeTerrainFog!=core.fog.enabled)return false;',
     'completeTerrainDepth!=depth || completeTerrainFog!=core.fog.enabled ||\n     completeTerrainCompact!=WorldInputPolicy::enabled)return false;')
edit(game, 'completeTerrainDepth==depth && completeTerrainFog==core.fog.enabled){',
     'completeTerrainDepth==depth && completeTerrainFog==core.fog.enabled &&\n     completeTerrainCompact==WorldInputPolicy::enabled){')
edit(game, 'completeTerrainDepth=depth;completeTerrainFog=core.fog.enabled;',
     'completeTerrainDepth=depth;completeTerrainFog=core.fog.enabled;completeTerrainCompact=WorldInputPolicy::enabled;')

cell = 'tyra/engine/inc/debug/world_cell_complete.hpp'
edit(cell, 'bool fogEnabled=false;', 'bool fogEnabled=false,compactInputs=false;')
core = 'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(core, '#include "debug/world_cell_direct.hpp"',
     '#include "debug/world_cell_direct.hpp"\n#include "debug/world_input_policy.hpp"')
edit(core, 'bytes && reference && *bytes==*reference',
     'bytes && reference && WorldInputPolicy::equivalent(*bytes,*reference)')
edit(core, '''      WorldCellDirect::appendSdk(sources[p],bindings[p],body,budget);''',
     '''      const auto geometryStart=body.size();
      WorldCellDirect::appendSdk(sources[p],bindings[p],body,budget);
      if(WorldInputPolicy::enabled)WorldInputPolicy::compactRange(body,geometryStart,sources[p].textured?3:2);''')
edit(core, '    if(body.size()!=exactWords)return false;',
     '    if(body.size()>exactWords || (!WorldInputPolicy::enabled && body.size()!=exactWords))return false;\n    ++WorldInputPolicy::cells;cell.compactInputs=WorldInputPolicy::enabled;')
edit(core, '''bool StaPipQBufferRenderer::renderCompleteWorldCell(WorldCellComplete::Cell& cell){
  u32 depth;memcpy(&depth,&RendererCoreDepth::scale,4);
  if(!ClipRing::active''', '''bool StaPipQBufferRenderer::renderCompleteWorldCell(WorldCellComplete::Cell& cell){
  u32 depth;memcpy(&depth,&RendererCoreDepth::scale,4);
  if(cell.compactInputs!=WorldInputPolicy::enabled || !ClipRing::active''')

for name, digest in helpers.items():
    assert sha(repo / name) == digest, 'Helper changed during generation'
m = json.loads((a.source / 'world-cell-price-manifest.json').read_text())
m.update(baseElfSha256=b['elfSha256'], generatorSha256=sha(Path(__file__)), variant='input-compact',
         compactHelpers=helpers, status='UNQUALIFIED',
         comparison='common direct/paint/wheels/event audio: original versus cold compact world geometry',
         limits=['Cold-only constant-W masked V3_32; normal direct/paint/wheels/event audio common ON.',
                 'Common one-time zero ROW/MASK normalization with STMOD=0; no per-frame repacking.',
                 'Encoding choice invalidates cells; original queue/texture/immutable owner contracts retained.',
                 'Native exact SDK body audit uses lossless inverse, including material DIRECT payloads.',
                 'Full native/images/lifetimes/capture and same-ELF PS2 net required.'])
(a.out / 'world-cell-price-manifest.json').write_text(json.dumps(m, indent=2) + '\n')
print(a.out, 'UNQUALIFIED cold input-format price fixture')
