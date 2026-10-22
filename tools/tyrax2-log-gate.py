"""A27: same-ELF ordinary logging gate, preserving all renderer work."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert q.sha(a.source/rel)==h,rel
def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
engine=a.out/'tyra/engine'
def edit(path,old,new):
    text=path.read_text();assert text.count(old)==1,(path,old,text.count(old))
    path.write_text(text.replace(old,new))
header=engine/'inc/debug/ordinary_log_gate.hpp'
header.write_text('#pragma once\nnamespace OrdinaryLog { inline bool enabled=true; }\n')
debug=engine/'inc/debug/debug.hpp'
edit(debug,'#include <stdio.h>','#include <stdio.h>\n#include "debug/ordinary_log_gate.hpp"')
edit(debug,'#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")',
     '#define TYRA_LOG(...) do { if (OrdinaryLog::enabled) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n"); } while (0)')
program=engine/'src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp'
# Disable A26 intervention in every arm; retain its sparse SDK byte oracle.
edit(program,'  if(ClipTrial::mode==7){','  if(false){')
probe=engine/'inc/debug/motion_probe.hpp'
edit(probe,'#include <stdio.h>','#include <stdio.h>\n#include "debug/ordinary_log_gate.hpp"')
edit(probe,'inline void begin(U pacing) {','''inline void begin(U pacing) {
  OrdinaryLog::enabled=variant()!=7 || offset<gasStart || finishPending;
  if(finishPending){printf("LOGWINDOWEND phase=%u\\n",phase);fflush(stdout);}''')
edit(probe,'inline void end(U pacing) {','''inline void end(U pacing) {
  if(phase<phaseCount && offset==gasStart-1){
    printf("LOGWINDOWBEGIN phase=%u quiet=%u\\n",phase,unsigned(variant()==7));fflush(stdout);
  }''')
plan=json.loads((a.source/'clip-capacity-manifest.json').read_text())
plan.update(experiment='A27',ordinaryLogGate=True,fixedHeader=False,
    scope='7 omits only ordinary TYRA_LOG argument evaluation/format/output during gas; SDK automatic header close in every arm',
    raster='all five main/env arms exact; original SDK byte oracle retained')
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
print(a.out)
