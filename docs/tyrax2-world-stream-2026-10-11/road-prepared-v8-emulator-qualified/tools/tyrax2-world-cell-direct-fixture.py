"""Same-ELF materialized/direct cold compiler, common accepted terrain and ordinary roads."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--physical-attempt',type=Path,required=True)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
for platform,location in [('emulator',a.source),('ps2',a.physical_attempt)]:
    report=json.loads((location/f'world-cell-price-{platform}.json').read_text())
    assert report['valid'] and report['variant']=='skip-a31'
    assert report['elfSha256']==binding['elfSha256']

def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
shutil.copy2(repo/'tyrax2-world-cell-direct.hpp',a.out/'tyra/engine/inc/debug/world_cell_direct.hpp')

def edit(rel,old,new):
    file=a.out/rel;t=file.read_text();assert t.count(old)==1,(rel,old)
    file.write_text(t.replace(old,new))

edit('tyra/engine/inc/debug/world_cell_complete.hpp','inline bool active=false;',
     'inline bool active=false;\ninline bool directCold=false,directAudit=false;\ninline unsigned directCalls=0,materializedCalls=0,audits=0;')
edit('tyra/engine/inc/debug/world_cell_complete.hpp','#include <draw_types.h>',
     '#include <draw_types.h>\n#include <draw_sampling.h>')
renderer='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
file=a.out/renderer;t=file.read_text()
start=t.index('bool StaPipQBufferRenderer::compileCompleteWorldCell(')
end=t.index('\nbool StaPipQBufferRenderer::beginCompleteWorldView()',start)
old=t[start:end]
direct=old.replace('::compileCompleteWorldCell(', '::compileCompleteWorldCellDirect(',1)
first=direct.index('    auto source=WorldCellPlan::compile(')
last=direct.index('    std::vector<WorldBody::Quad> body;',first)
direct=direct[:first]+direct[last:]
direct=direct.replace('    std::vector<WorldBody::Quad> body;',
    '    std::vector<WorldBody::Quad> body;\n    std::vector<std::vector<WorldBody::Quad>> heads;\n    heads.reserve(bags.size());\n    unsigned exactWords=0;')
needle='''      const auto& geometry=compiled.passes[p].commands;
      if(head.size()+geometry.size()>budget-body.size())return false;
      body.insert(body.end(),head.begin(),head.end());
      body.insert(body.end(),geometry.begin(),geometry.end());
    }
    body.shrink_to_fit();'''
assert direct.count(needle)==1
direct=direct.replace(needle,'''      const unsigned geometry=WorldCellDirect::geometryQwords(sources[p]);
      if(head.size()+geometry>budget-exactWords)return false;
      exactWords+=head.size()+geometry;
      heads.push_back(std::move(head));
    }
    body.reserve(exactWords);
    for(unsigned p=0;p<bags.size();++p){
      body.insert(body.end(),heads[p].begin(),heads[p].end());
      WorldCellDirect::appendSdk(sources[p],bindings[p],body,budget);
    }
    if(body.size()!=exactWords)return false;
    body.shrink_to_fit();''')
direct=direct.replace('key.programAddress=compiled.passes[0].programAddress;',
                      'key.programAddress=bindings[0].program->getDestinationAddress();')
assert 'WorldCellPlan::compile' not in direct and 'compiled.' not in direct
materialized=old.replace('::compileCompleteWorldCell(', '::compileCompleteWorldCellMaterialized(',1)
wrapper='''bool StaPipQBufferRenderer::compileCompleteWorldCell(
    WorldCellComplete::Cell& cell,const std::vector<StaPipBag*>& bags){
  const bool direct=WorldCellComplete::directCold;
  const bool ok=direct?compileCompleteWorldCellDirect(cell,bags):
                       compileCompleteWorldCellMaterialized(cell,bags);
  if(!ok)return false;
  if(direct)++WorldCellComplete::directCalls;else ++WorldCellComplete::materializedCalls;
  if(WorldCellComplete::directAudit){
    WorldCellComplete::Cell oracle;
    const bool other=direct?compileCompleteWorldCellMaterialized(oracle,bags):
                           compileCompleteWorldCellDirect(oracle,bags);
    const auto* bytes=cell.owner->admit(cell.key);
    const auto* reference=other?oracle.owner->admit(oracle.key):nullptr;
    TYRA_ASSERT(bytes && reference && *bytes==*reference,"A44 direct complete body oracle failed");
    ++WorldCellComplete::audits;
    oracle.owner.reset();worldPool.collect();
  }
  return true;
}
'''
t=t[:start]+wrapper+'\n'+materialized+'\n'+direct+t[end:]
needle='#include "debug/world_cell_sdk_check.hpp"'
assert t.count(needle)==1;t=t.replace(needle,needle+'\n#include "debug/world_cell_direct.hpp"')
file.write_text(t)
header='tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
file=a.out/header;t=file.read_text()
start=t.index('  bool compileCompleteWorldCell(')
end=t.index(';',start)+1
declaration=t[start:end]
t=t[:end]+'\n'+declaration.replace('compileCompleteWorldCell(', 'compileCompleteWorldCellDirect(')+'\n'+declaration.replace('compileCompleteWorldCell(', 'compileCompleteWorldCellMaterialized(')+t[end:]
file.write_text(t)
edit('game/inc/world_moving.hpp','#include "debug/debug.hpp"',
     '#include "debug/debug.hpp"\n#include "debug/world_cell_complete.hpp"')
edit('game/inc/world_moving.hpp','  enabled=p<6;skipA31=p<6 && modes[p];',
     '  enabled=p<6;skipA31=true;WorldCellComplete::directCold=p<6 && modes[p];')
edit('game/inc/world_moving.hpp','  legacyCalls=legacyPrepared=0;',
     '  legacyCalls=legacyPrepared=0;\n  WorldCellComplete::directCalls=WorldCellComplete::materializedCalls=WorldCellComplete::audits=0;')
edit('game/inc/world_moving.hpp','variant=skip-a31\\n",p,unsigned(skipA31));',
     'variant=direct-cold\\n",p,unsigned(WorldCellComplete::directCold));')
edit('game/inc/world_moving.hpp','  fflush(stdout);\n}\n}',
     '  printf("A44COLDPHASE phase=%u direct=%u directCalls=%u materializedCalls=%u audits=%u\\n",phase,unsigned(WorldCellComplete::directCold),WorldCellComplete::directCalls,WorldCellComplete::materializedCalls,WorldCellComplete::audits);\n  fflush(stdout);\n}\n}')
edit('game/src/terrain_game.cpp','  WorldMoving::begin(MotionProbe::phase);',
     '  WorldMoving::begin(MotionProbe::phase);\n  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;')
edit('game/src/terrain_game.cpp','  const bool movingCandidate=WorldMoving::enabled;',
     '  const bool movingCandidate=WorldMoving::enabled;\n  const bool movingDirect=WorldCellComplete::directCold;')
edit('game/src/terrain_game.cpp','  const bool candidate=arm==1?!movingCandidate:movingCandidate;',
     '  const bool candidate=true;\n  WorldCellComplete::directCold=arm==1?!movingDirect:movingDirect;\n  const unsigned beforeDirect=WorldCellComplete::directCalls,beforeOld=WorldCellComplete::materializedCalls;')
edit('game/src/terrain_game.cpp','  fflush(stdout);\n  }\n  WorldCellComplete::active=movingCandidate;',
     '''  printf("A44COLDIMAGE phase=%u arm=%u direct=%u directCalls=%u materializedCalls=%u\\n",
    MotionProbe::phase,arm,unsigned(WorldCellComplete::directCold),WorldCellComplete::directCalls-beforeDirect,WorldCellComplete::materializedCalls-beforeOld);
  fflush(stdout);
  }
  WorldCellComplete::directCold=movingDirect;
  WorldCellComplete::active=movingCandidate;''')
manifest=json.loads((a.source/'world-cell-price-manifest.json').read_text())
manifest.update(baseElfSha256=binding['elfSha256'],generatorSha256=sha(Path(__file__)),
                variant='direct-cold',comparison='common skip-A31 terrain: materialized versus direct final-owned compiler',
                directHeaderSha256=sha(repo/'tyrax2-world-cell-direct.hpp'),
                status='UNQUALIFIED',limits=[
                    'Ordinary roads, full six-plane terrain clipping, actual SDK writers unchanged.',
                    'All cold bodies compared to opposite compiler at offset1100, outside priced rows.',
                    'Measured local replacement invokes only selected compiler; immutable bank leases unchanged.',
                    'No native/raster/physical gain or60fps/default claim before target gates.'])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED direct compiler with outside-price full-body oracle')
