"""Terrain-only private A/B/A and B/A/B subrange intervention."""
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
p=engine/'inc/debug/motion_probe.hpp'
edit(p,'inline constexpr U phaseCount=20;','inline constexpr U phaseCount=12;')
edit(p,'return phase>=10;','return phase>=6;')
edit(p,'return phase%10;','return phase%6;')
edit(p,'inline U lifetime(){return 250;}','inline U lifetime(){return 250;}\ninline bool candidate(){return mode()==1||mode()==3||mode()==5;}')
edit(p,'enabled=sample&&mode()==1;clocks=sample&&mode()>=3&&mode()<=8;',
     'enabled=sample&&mode()<3;clocks=false;')
edit(p,'DeferredPush,DeferredPop,Count};','DeferredPush,DeferredPop,SubCalls,SubOutsideTris,SubCullTris,SubClipTris,SubBuild,SubHits,Count};')
edit(p,'"deferredPop","coreTicks"','"deferredPop","subCalls","subOutsideTris","subCullTris","subClipTris","subBuild","subHits","coreTicks"')
edit(p,'#include <stdio.h>','#include <stdio.h>\n#include "debug/strip_subranges.hpp"')
edit(p,'  ++phase;offset=~0U;', '''  printf("SUBPHASE phase=%u candidate=%u checks=%u failed=%u bytes=%u\\n",phase,unsigned(candidate()),StripSubranges::checks,StripSubranges::failures,unsigned(sizeof(StripSubranges::entries)));fflush(stdout);
  if(StripSubranges::failures)valid=false;
  ++phase;offset=~0U;''')
(engine/'inc/debug/strip_subranges.hpp').write_bytes((ROOT/'tools/tyrax2-strip-subranges.hpp').read_bytes())
p=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
needle='''      const u32 triangles = packages[i].size - 2;
      for (u32 t = 0; t < triangles; t += trisPerChunk) {'''
code='''      const u32 triangles = packages[i].size - 2;
      if(MotionProbe::candidate() && MotionProbe::producer==MotionProbe::Terrain &&
          qbufferRenderer.isVU1ClippingEnabled() && !packages[i].bag->billboard) {
        bool built=false;
        const auto& pkg=packages[i];
        const auto* entry=StripSubranges::acquire(pkg.vertices,pkg.size,trisPerChunk,
            pkg.bag->bboxVersion,pkg.bag->contentVersion?*pkg.bag->contentVersion:0,
            MotionProbe::phase,built);
        if(entry) {
          MotionProbe::add(MotionProbe::SubCalls);
          MotionProbe::add(built?MotionProbe::SubBuild:MotionProbe::SubHits);
          StripSubranges::Route routes[32];
          const u32 blocks=(triangles+trisPerChunk-1)/trisPerChunk;
          for(u32 b=0;b<blocks;++b) {
            routes[b]=StripSubranges::classify(entry->boxes[b],objectSpacePlanes,clipObjectSpacePlanes);
            const u32 first=b*trisPerChunk,n=std::min(trisPerChunk,triangles-first);
            VERIFY_SUBRANGE
            MotionProbe::add(routes[b]==StripSubranges::Outside?MotionProbe::SubOutsideTris:
                routes[b]==StripSubranges::Cull?MotionProbe::SubCullTris:MotionProbe::SubClipTris,n);
          }
          for(u32 b=0;b<blocks;) {
            const auto route=routes[b];
            const u32 first=b*trisPerChunk;
            u32 n=std::min(trisPerChunk,triangles-first);++b;
            if(route==StripSubranges::Outside)continue;
            if(route==StripSubranges::Cull)while(b<blocks&&routes[b]==route) {
              const u32 next=std::min(trisPerChunk,triangles-b*trisPerChunk);
              if((n+next)*3>maxVertCount)break;
              n+=next;++b;
            }
            auto* buffer=qbufferRenderer.getBuffer();
            if(!qbufferRenderer.fillCachedStrip(buffer,pkg,first,n))
              buffer->fillByStripExpand(pkg,first,n);
            if(route==StripSubranges::Cull)qbufferRenderer.cull(buffer);
            else qbufferRenderer.clip(buffer);
          }
          continue;
        }
      }
      for (u32 t = 0; t < triangles; t += trisPerChunk) {'''
code=code.replace('VERIFY_SUBRANGE', '''TYRA_ASSERT(StripSubranges::verify(pkg.vertices,first,n,entry->boxes[b],routes[b],objectSpacePlanes,clipObjectSpacePlanes),"Subrange witness failed");''' if verify else '(void)first;(void)n;')
edit(p,needle,code)
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps({'experiment':'terrain-subranges','oracle':verify,
    'files':quiet.source_inputs(out,'vehicle-playground.tyra')},indent=2)+'\n')
print(out)
