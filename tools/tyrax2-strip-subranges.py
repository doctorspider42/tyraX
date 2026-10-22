"""A/B/A and B/A/B validation of the production terrain subrange path."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve()
verify='--verify' in sys.argv[2:]
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-producer-probe.py'),str(out)]+
               (['--verify'] if verify else []),check=True)
engine=out/'tyra/engine'
def edit(p,old,new,count=1):
    t=p.read_text(encoding='utf-8-sig')
    assert t.count(old)==count,(p,old,t.count(old))
    p.write_bytes(t.replace(old,new).encode())
terrain=(out/'game/src/gen/game_physics.gen.cpp').read_text()
assert terrain.count('refineStrips = true;')==5, 'Rebuild editor: all terrain passes must opt in'
p=engine/'inc/debug/motion_probe.hpp'
edit(p,'inline constexpr U phaseCount=20;','inline constexpr U phaseCount=12;')
edit(p,'return phase>=10;','return phase>=6;')
edit(p,'return phase%10;','return phase%6;')
edit(p,'inline U lifetime(){return 250;}','inline U lifetime(){return 250;}\ninline bool candidate(){return mode()==1||mode()==3||mode()==5;}')
edit(p,'enabled=sample&&mode()==1;clocks=sample&&mode()>=3&&mode()<=8;',
     'enabled=sample&&mode()<3;clocks=false;')
edit(p,'DeferredPush,DeferredPop,Count};','DeferredPush,DeferredPop,SubCalls,SubOutsideTris,SubCullTris,SubClipTris,SubBuild,SubHits,Count};')
edit(p,'"deferredPop","coreTicks"','"deferredPop","subCalls","subOutsideTris","subCullTris","subClipTris","subBuild","subHits","coreTicks"')
edit(p,'#include <stdio.h>','#include <stdio.h>\n#include "renderer/3d/pipeline/static/core/stapip_strip_subranges.hpp"\nnamespace StripSubranges = Tyra::StaPipStripSubranges;')
edit(p,'  ++phase;offset=~0U;', '''  printf("SUBPHASE phase=%u candidate=%u checks=%u failed=%u bytes=%u\\n",phase,unsigned(candidate()),StripSubranges::checks,StripSubranges::failures,unsigned(sizeof(StripSubranges::Cache)));fflush(stdout);
  if(StripSubranges::failures)valid=false;
  ++phase;offset=~0U;''')
if verify:
    p=engine/'inc/renderer/3d/pipeline/static/core/stapip_strip_subranges.hpp'
    edit(p,'#define TYRA_STAPIP_SUBRANGES_VERIFY 0','#define TYRA_STAPIP_SUBRANGES_VERIFY 1')
p=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(p,'if (pkg.bag->refineStrips &&','if (MotionProbe::candidate() && pkg.bag->refineStrips &&')
edit(p,'if (entry) {\n          StaPipStripSubranges::Route', '''if (entry) {
          MotionProbe::add(MotionProbe::SubCalls);
          MotionProbe::add(built?MotionProbe::SubBuild:MotionProbe::SubHits);
          StaPipStripSubranges::Route''')
edit(p,'#if TYRA_STAPIP_SUBRANGES_VERIFY', '''            const u32 probeN=std::min(trisPerChunk,triangles-b*trisPerChunk);
            MotionProbe::add(routes[b]==StaPipStripSubranges::Outside?MotionProbe::SubOutsideTris:
                routes[b]==StaPipStripSubranges::Cull?MotionProbe::SubCullTris:MotionProbe::SubClipTris,probeN);
#if TYRA_STAPIP_SUBRANGES_VERIFY''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps({'experiment':'terrain-subranges','oracle':verify,
    'integrated':True,'files':quiet.source_inputs(out,'vehicle-playground.tyra')},indent=2)+'\n')
print(out)
