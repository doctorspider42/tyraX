"""Bind A44 complete-cell backend to the qualified engine; no runtime caller yet."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists(), 'Refusing to replace an existing fixture'
binding = json.loads((a.source/'native-binding.json').read_text())
for rel, digest in binding['files'].items():
    assert sha(a.source/rel) == digest, rel
gate = json.loads((a.source/'world-texture-emulator.json').read_text())
assert gate['valid'] and gate['elfSha256'] == binding['elfSha256']

def ignore(path, names):
    return [n for n in names if n in ('obj', '.git') or
            (n.startswith(('tape-', 'command-')) and n.endswith('.bin'))]

for part in ('game', 'tyra/engine'):
    shutil.copytree(a.source/part, a.out/part, ignore=ignore)
shutil.copy2(a.source/'tyra/Makefile.base', a.out/'tyra/Makefile.base')
shutil.copy2(a.source/'authored-manifest.json', a.out/'authored-manifest.json')
engine = a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-world-cell-complete.hpp',
             engine/'inc/debug/world_cell_complete.hpp')

def once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)

header = engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
text = header.read_text()
text = '#include "debug/world_cell_complete.hpp"\n' + text
text = once(text, '  void collectWorldOwners(){worldPool.collect();}', '''
  bool compileCompleteWorldCell(WorldCellComplete::Cell&,const std::vector<StaPipBag*>&);
  bool beginCompleteWorldView();
  bool renderCompleteWorldCell(WorldCellComplete::Cell&);
  void collectWorldOwners(){worldPool.collect();}''')
header.write_text(text)
cpp = engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
text = cpp.read_text()
anchor = 'bool StaPipQBufferRenderer::prepareWorldGeometry('
assert text.count(anchor) == 1
text = text.replace(anchor, (repo/'tools/tyrax2-world-cell-complete-methods.inc').read_text() +
                    '\n' + anchor)
cpp.write_text(text)
manifest = {
    'experiment': 'A44-complete-cell-backend',
    'baseElfSha256': binding['elfSha256'], 'status': 'UNQUALIFIED',
    'toolPins': {name: sha(repo/'tools'/name) for name in
                 ('tyrax2-world-cell-complete.hpp',
                  'tyrax2-world-cell-complete-methods.inc',
                  'tyrax2-world-cell-complete-fixture.py')},
    'limits': ['No terrain producer or GPU runtime caller yet.',
               'No full texture-set pressure, image, lifetime or physical price gate.',
               'Day-only admission rejects active spot/dynamic lighting.',
               'All six clipping planes and ordered passes retained.']}
(a.out/'world-cell-complete-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(a.out, 'UNQUALIFIED complete-cell backend')
