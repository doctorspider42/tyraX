"""Create isolated diagnostic engine/game copies; never changes the source tree.

Requires an already-built, complete fixed-workload fixture. Build the returned
game with native-build and the returned engine. See docs/hardware-profiler.md.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('project', type=Path)
p.add_argument('engine', type=Path, help='Tyra root containing engine/ and Makefile.base')
p.add_argument('destination', type=Path)
p.add_argument('--kind', choices=['one-pixel-scissor', 'unlit'], required=True)
a = p.parse_args()
if any(a.destination.resolve().is_relative_to(x.resolve()) for x in [a.project, a.engine]):
    p.error('Destination must be outside both source trees')
if a.destination.exists():
    p.error('Destination already exists; choose a new isolated directory')
if not (a.project/'bin').is_dir() or not (a.engine/'engine/src').is_dir():
    p.error('Expected a built project and a Tyra source root')
a.destination.mkdir(parents=True)
engine = a.destination/'tyra'
shutil.copytree(a.engine/'engine', engine/'engine', ignore=shutil.ignore_patterns('obj','bin'))
shutil.copy2(a.engine/'Makefile.base', engine/'Makefile.base')
game = a.destination/'game'
shutil.copytree(a.project, game, ignore=shutil.ignore_patterns(
    '*.csv','*.log','hardware-trace.cfg','livedbg.bin','livedbg.cmd','frame.tga'))
changed = []
if a.kind == 'one-pixel-scissor':
    # Keep the same geometry, projection and VU programs. Limit each engine
    # scissor write to pixel (0,0), including offscreen targets and restores.
    # The volatile mask retains evaluation of original scissor arguments.
    for f in (engine/'engine/src').rglob('*.cpp'):
        s = f.read_text(encoding='utf-8-sig')
        if 'GS_SET_SCISSOR(' not in s:
            continue
        s = s.replace('GS_SET_SCISSOR(', 'TYRAX_PROBE_SCISSOR(')
        s = ('// Diagnostic copy only: suppress raster area, retain geometry.\n'
             '#include <tamtypes.h>\n'
             'static volatile u64 probeScissorMask = 0;\n'
             '#define TYRAX_PROBE_SCISSOR(...) (GS_SET_SCISSOR(__VA_ARGS__) & probeScissorMask)\n' + s)
        f.write_bytes(s.encode('utf-8'))
        changed.append(str(f.relative_to(engine)))
else:
    f = engine/'engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    s = f.read_text(encoding='utf-8-sig')
    needle = 'void StaPipCore::render(StaPipBag* bag) {'
    assert s.count(needle)==1
    s = s.replace(needle, needle + '''
  // Diagnostic copy only: omit lighting ALU and normal transfers. This is
  // NOT a pure VU ALU test: program class and package capacity can change.
  struct RestoreLighting {
    StaPipBag* bag; StaPipLightingBag* saved;
    ~RestoreLighting() { bag->lighting = saved; }
  } restoreLighting{bag, bag->lighting};
  bag->lighting = nullptr;
''')
    f.write_bytes(s.encode('utf-8'))
    changed.append(str(f.relative_to(engine)))
manifest = {'kind': a.kind, 'source_project': str(a.project.resolve()),
            'source_engine': str(a.engine.resolve()), 'changed_engine_files': changed,
            'assets': {str(f.relative_to(game/'bin')): hashlib.sha256(f.read_bytes()).hexdigest()
                       for f in (game/'bin').rglob('*') if f.is_file() and
                       f.suffix.lower() in ['.png','.tmdl','.mtl']}}
(a.destination/'probe.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(f'Game: {game}\nEngine: {engine}\nDiagnostic only; do not ship this engine.')
