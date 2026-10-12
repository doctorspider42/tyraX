"""Dump actual native SDK bodies for offline lossless compaction qualification."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path)
a = p.parse_args()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
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
f = a.out / 'tyra/engine/inc/debug/world_cell_sdk_check.hpp'
t = f.read_text()

def replace(old, new):
    global t
    assert t.count(old) == 1, old
    t = t.replace(old, new)

replace('#include <cstring>', '#include <cstring>\n#include <kernel.h>\n#include <vif_registers.h>')
replace('  if(done)return;', '''  if(done)return;
  std::vector<u32> dump;
  const u32 initial[]={0x43534957,1,60,VIF1_MODE,VIF1_MASK,
                       VIF1_R0,VIF1_R1,VIF1_R2,VIF1_R3};
  dump.insert(dump.end(),initial,initial+9);''')
replace('    auto reject=[&]', '''    for(unsigned p=0;p<compiled.passes.size();++p){
      const auto& output=compiled.passes[p];
      const u32 record[]={mode,topology,count,p,unsigned(cell->passes[p].textured),
                          unsigned(output.commands.size()*4)};
      dump.insert(dump.end(),record,record+6);
      for(const auto& quad:output.commands)
        dump.insert(dump.end(),quad.words.begin(),quad.words.end());
    }
    auto reject=[&]''')
replace('  done=true;', '''  const u32 final[]={0x454e4957,VIF1_MODE,VIF1_MASK,VIF1_R0,VIF1_R1,VIF1_R2,VIF1_R3};
  dump.insert(dump.end(),final,final+7);
  FILE* file=std::fopen("host:world-input-sdk.bin","wb");
  if(!file)throw std::runtime_error("World input SDK dump open failed");
  const auto wrote=std::fwrite(dump.data(),sizeof(u32),dump.size(),file);
  const auto closed=std::fclose(file);
  if(wrote!=dump.size() || closed)throw std::runtime_error("World input SDK dump write failed");
  done=true;''')
replace('              checks,checks*5,parts,controls);std::fflush(stdout);', '''              checks,checks*5,parts,controls);std::fflush(stdout);
  std::printf("INPUTSDKDONE records=60 words=%u gpuExecuted=0 componentOnly=1\\n",unsigned(dump.size()));
  std::fflush(stdout);
  SleepThread();''')
f.write_text(t)
m = dict(baseElfSha256=b['elfSha256'], generatorSha256=sha(Path(__file__)),
         status='UNQUALIFIED', componentOnly=True, renderingChanged=False,
         limits=['Actual C/TC SDK bodies from native execution; geometry recycled from real terrain bag.',
                 'Target sleeps after dump; no moving/FPS or compact GPU qualification.',
                 'VIF state snapshots are observational, not drained-queue lifetime proof.',
                 'Offline exact decode gate follows; actual compact VIF/native images/physical net remain.'])
(a.out / 'world-input-sdk-manifest.json').write_text(json.dumps(m, indent=2) + '\n')
print(a.out, 'UNQUALIFIED actual SDK body dump component')
