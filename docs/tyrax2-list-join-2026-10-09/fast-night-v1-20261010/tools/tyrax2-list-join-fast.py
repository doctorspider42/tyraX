"""A22: actual adjacent-list copy bypass on ordinary producers, same-ELF A/B."""
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
subprocess.run([sys.executable, str(repo/'tools/tyrax2-bag-tape-clone.py'),
                str(a.source), str(a.out)], check=True)
engine = a.out/'tyra/engine'
assert (engine/'inc/debug/command_size.hpp').exists()
assert not (engine/'inc/debug/preparation_split.hpp').exists()
for src, dst in [('tyrax2-list-join.hpp', 'list_join.hpp'),
                 ('tyrax2-list-join-fast.hpp', 'list_join_fast.hpp')]:
    shutil.copy2(repo/'tools'/src, engine/'inc/debug'/dst)

def edit(path, old, new):
    text = path.read_text(encoding='utf-8-sig')
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_text(text.replace(old, new), encoding='utf-8')

edit(engine/'inc/debug/command_pricing.hpp',
     'constexpr U modes[stages]={0,0,1,0,2,0,2,0,1,0};',
     'constexpr U modes[stages]={0,0,0,0,0,0,0,0,0,0};')
path = engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp'
edit(path, '#include <string.h>', '#include <string.h>\n#include "debug/list_join_fast.hpp"')
for name, count, following in [('fillByCopyMax', 3, 'fillByCopy1By2'),
                               ('fillByCopy1By2', 2, 'fillByCopy1By3')]:
    text = path.read_text(encoding='utf-8')
    start = text.index('void StaPipQBuffer::'+name+'(')
    end = text.index('void StaPipQBuffer::'+following+'(', start)
    body = text[start:end]
    args = ', '.join('&pkg'+str(i+1) for i in range(count))
    declaration = f'  const StaPipBagPackage* pkgs[{count}] = {{{args}}};'
    assert body.count(declaration) == 1
    body = body.replace(declaration+'\n', '')
    body = body.replace('  deallocateDynamicData();', declaration+'\n'
                        f'  if (FastJoin::apply(*this, pkgs, {count})) return;\n'
                        '  deallocateDynamicData();', 1)
    body = body.replace('  bag = pkg1.bag;\n}', '  bag = pkg1.bag;\n'
                        f'  ListJoin::checkAndApply(*this, pkgs, {count});\n'+'}')
    path.write_text(text[:start]+body+text[end:], encoding='utf-8')
path = a.out/'game/src/gen/game_physics.gen.cpp'
edit(path, '#include "debug/command_pricing.hpp"',
     '#include "debug/command_pricing.hpp"\n#include "debug/list_join_fast.hpp"')
edit(path, '  const unsigned mode=TapePricing::mode(stage);',
     '''  const unsigned mode=TapePricing::mode(stage);
  FastJoin::active=FastJoin::mode(stage);
  FastJoin::oracle=local==TapePricing::length-2;
  ListJoin::reset(FastJoin::active && FastJoin::oracle);''')
edit(path, '  recording=checking=false;', '''  recording=checking=false;
  if(FastJoin::oracle){
    printf("FASTJOIN stage=%u active=%u calls=%u accepted=%u bytes=%u\\n",stage,unsigned(FastJoin::active),ListJoin::calls,ListJoin::accepted,ListJoin::bytes);fflush(stdout);
  }''')
(a.out/'list-join-fast-manifest.json').write_text(json.dumps(dict(
    experiment='A22', ordinaryProducers=True, candidateStages=[2,4,6,8],
    oracleLocal=222, rasterLocal=223, sampleLocal=[64,192],
    scope='Frozen ordinary scene; actual copy bypass, unchanged queue ownership; not live FPS or non-recording qualification'), indent=2)+'\n')
(a.out/'native-binding.json').unlink()
(a.out/'symbols.txt').unlink()
print(a.out)
