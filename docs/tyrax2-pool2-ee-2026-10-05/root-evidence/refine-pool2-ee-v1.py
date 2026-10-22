from pathlib import Path
import re
root=Path('F:/Projects/tyrax2-lab-20261001/wild-pool2-ee-physical-v1')
p=root/'game/src/gen/game_lighting.gen.cpp';s=p.read_text();old='      for (size_t k = 0; k < cv.size(); ++k) {'
assert s.count(old)==1;s=s.replace(old,'      if(rebuildGeometry || !compact) for (size_t k = 0; k < cv.size(); ++k) {')
if '#include <algorithm>' not in s:s='#include <algorithm>\n'+s
p.write_bytes(s.encode())
for rel in ['tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp',
            'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp']:
 p=root/rel;s=p.read_text()
 s,n=re.subn(r'TYRA_ASSERT\(ExperimentalPoolTable::materializeRange\((.*?)\),"(Invalid lazy pool [^"]+)"\);',
     lambda m:'if(!ExperimentalPoolTable::materializeRange('+m[1]+')) { TYRA_TRAP("'+m[2]+'"); return; }',s,flags=re.S)
 assert n in (4,3),(rel,n)
 p.write_bytes(s.encode())
print('Refined: no vertex loop on color-only compact updates; materialization executes with NDEBUG.')
