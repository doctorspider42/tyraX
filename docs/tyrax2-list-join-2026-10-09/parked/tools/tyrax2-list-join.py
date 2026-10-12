"""Private A19 actual-gather oracle on the source-bound A18 frozen fixture."""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('out', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(repo/'tools/tyrax2-command-pricing.py'),
                str(a.source), str(a.out)], check=True)
engine = a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-list-join.hpp', engine/'inc/debug/list_join.hpp')

def edit(path, old, new):
    text = path.read_text(encoding='utf-8-sig')
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_text(text.replace(old, new), encoding='utf-8')

path = engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp'
edit(path, '#include <string.h>', '#include <string.h>\n#include "debug/list_join.hpp"')
for name, count, following in [('fillByCopyMax', 3, 'fillByCopy1By2'),
                                ('fillByCopy1By2', 2, 'fillByCopy1By3')]:
    text = path.read_text(encoding='utf-8')
    start = text.index('void StaPipQBuffer::'+name+'(')
    end = text.index('void StaPipQBuffer::'+following+'(', start)
    body = text[start:end]
    anchor = '  bag = pkg1.bag;\n}'
    assert body.count(anchor) == 1
    body = body.replace(anchor, '  bag = pkg1.bag;\n'
                        f'  ListJoin::checkAndApply(*this, pkgs, {count});\n'+'}')
    path.write_text(text[:start]+body+text[end:], encoding='utf-8')
path = a.out/'game/src/gen/game_physics.gen.cpp'
edit(path, '#include "debug/command_pricing.hpp"',
     '#include "debug/command_pricing.hpp"\n#include "debug/list_join.hpp"')
edit(path, '  const unsigned mode=TapePricing::mode(stage);',
     '  const unsigned mode=TapePricing::mode(stage);\n  ListJoin::reset(mode==1);')
edit(path, '    captureImage(core,stage,false);captureImage(core,stage,true);',
     '    printf("LISTJOIN stage=%u active=%u calls=%u accepted=%u bytes=%u\\n",stage,unsigned(ListJoin::active),ListJoin::calls,ListJoin::accepted,ListJoin::bytes);fflush(stdout);\n'
     '    captureImage(core,stage,false);captureImage(core,stage,true);')
(a.out/'list-join-manifest.json').write_text(json.dumps(dict(
    experiment='A19', qualificationOnly=True, originalGatherRetained=True,
    activeModes=[1], productionChanges=False), indent=2)+'\n', encoding='utf-8')
print(a.out)
