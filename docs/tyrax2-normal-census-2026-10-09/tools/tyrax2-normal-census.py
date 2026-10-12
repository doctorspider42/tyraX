"""Clone completed A16 drive, disable target gate, observe normal preparation only."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('source',type=Path)
a=p.parse_args()
out=a.out.resolve();source=a.source.resolve()
assert not out.exists(), 'Never overwrite evidence'
binding=json.loads((source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():
    assert hashlib.sha256((source/rel).read_bytes()).hexdigest()==digest,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
for part in ('game','tyra/engine'):
    shutil.copytree(source/part,out/part,ignore=quiet.ignore)
shutil.copy2(source/'tyra/Makefile.base',out/'tyra/Makefile.base')
shutil.copy2(source/'authored-manifest.json',out/'authored-manifest.json')
inc=out/'tyra/engine/inc/debug'
shutil.copy2(repo/'tools/tyrax2-normal-census.hpp',inc/'normal_census.hpp')
gate=inc/'target_audit.hpp'
gate.write_text(quiet.once(quiet.once(gate.read_text(),'return MotionProbe::variant()==6;','return false;'),
                          'inline unsigned tested=0,rejected=0;',''))
core=out/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'
s=core.read_text()
s=quiet.once(s,'namespace Tyra {','#include "debug/normal_census.hpp"\nnamespace Tyra {')
s=quiet.once(s,'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {',
    'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {\n  NormalCensus::beginBag();')
s=quiet.once(s,'  if (e.coordinates == n && e.count == bag->count &&',
    '  NormalCensus::observe(n,bag->count,bag->bboxVersion,content,e.coordinates,e.count,e.bboxVersion,e.content);\n  if (e.coordinates == n && e.count == bag->count &&')
s=quiet.once(s,'      const float inv = 1.0F / sqrtf(l2);',
    '      NormalCensus::root();\n      const float inv = 1.0F / sqrtf(l2);')
s=quiet.once(s,'    if (frustumCheck == OUTSIDE_FRUSTUM) {',
    '    if (frustumCheck == OUTSIDE_FRUSTUM) {\n      NormalCensus::outside();')
core.write_text(s)
probe=inc/'motion_probe.hpp'
s=probe.read_text()
s=quiet.once(s,'namespace TargetAudit { extern unsigned tested,rejected; }',
             'namespace TargetAudit { inline unsigned tested=0,rejected=0; }')
s=quiet.once(s,'#include <stdio.h>','#include <stdio.h>\n#include "debug/normal_census.hpp"')
s=quiet.once(s,'inline void begin(U pacing) {',
    'inline void begin(U pacing) {\n  NormalCensus::bucket=offset<sampleStart?0:(offset<gasStart?1:2);')
s=quiet.once(s,'    printf("TARGETCOUNTS phase=',
    '    NormalCensus::dump(phase);\n    printf("TARGETCOUNTS phase=')
probe.write_text(s)
(out/'normal-census-manifest.json').write_text(json.dumps(dict(
    source=str(source),experiment='A17 normal census',timings='intrusive; not performance evidence',
    buckets=['warmup','parked camera sweep','full throttle camera sweep'],
    policy='unchanged normalization, EE paint; target reject disabled'),indent=2)+'\n')
print(out)
