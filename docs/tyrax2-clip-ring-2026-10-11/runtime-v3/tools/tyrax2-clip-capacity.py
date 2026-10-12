"""A24: plane-aware Clip grouping versus disabling the complete clipping route."""
import argparse,hashlib,importlib.util,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path);p.add_argument('--group-only',action='store_true')
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert hashlib.sha256((a.source/rel).read_bytes()).hexdigest()==h,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
def ignore(path,names):
 return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
engine=a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-clip-capacity.hpp',engine/'inc/debug/clip_capacity.hpp')
def edit(path,old,new):
 text=path.read_text(encoding='utf-8-sig');assert text.count(old)==1,(path,old,text.count(old))
 path.write_text(text.replace(old,new),encoding='utf8')
# Remove A23's actual-copy intervention; the unchanged baseline is not promoted.
original=Path('/mnt/f/Projects/tyrax2-target-drive-20261009-v1/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp')
assert hashlib.sha256(original.read_bytes()).hexdigest()=='ba059bd60f30728a7ebb0e96ca2e370676a9f26f0c278b379b0a02f130298eab'
shutil.copy2(original,engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp')
probe=engine/'inc/debug/motion_probe.hpp'
edit(probe,'#include "debug/list_join_fast.hpp"','#include "debug/list_join_fast.hpp"\n#include "debug/clip_capacity.hpp"')
edit(probe,'inline constexpr U phaseCount=12;','inline constexpr U phaseCount=10;')
edit(probe,'constexpr U modes[]={0,6,0,6,0,6,0,6,0,6,0,6};','constexpr U modes[]={0,7,0,8,0,0,7,0,8,0};')
edit(probe,'  FastJoin::active=variant()==6;','  FastJoin::active=false;\n  ClipTrial::mode=variant();\n  ClipTrial::observe=!finishPending && offset>=gasStart && offset%128==64;\n  ClipTrial::current={};')
edit(probe,'    const auto& join=FastJoin::witnesses[phase];','''    const auto& w=ClipTrial::witnesses[phase];
    printf("CLIPWITNESS phase=%u frames=%u clipCalls=%u eligible=%u merged=%u removed=%u maxInput=%u bypassBags=%u\\n",phase,w.frames,w.clipCalls,w.eligible,w.merged,w.removed,w.maxInput,w.bypassBags);
    const auto& join=FastJoin::witnesses[phase];''')
edit(probe,'  enabled=false;clocks=false;++frame;++offset;','''  if(ClipTrial::observe){auto& w=ClipTrial::witnesses[phase];const auto& c=ClipTrial::current;
    ++w.frames;w.clipCalls+=c.clipCalls;w.eligible+=c.eligible;w.merged+=c.merged;
    w.removed+=c.removed;w.maxInput=std::max(w.maxInput,c.maxInput);w.bypassBags+=c.bypassBags;}
  enabled=false;clocks=false;++frame;++offset;''')
game=a.out/'game/src/terrain_game.cpp'
edit(game,'  FastJoin::active=MotionProbe::variant()==6;','  FastJoin::active=false;ClipTrial::mode=MotionProbe::variant();')
edit(game,'MotionProbe::phase >= 6','MotionProbe::phase >= 5')
edit(game,'MotionProbe::phase>=6 ? lights==0 : lights!=0','MotionProbe::phase>=5 ? lights==0 : lights!=0')
edit(game,'MotionProbe::phase>=6?0.0F:12.0F','MotionProbe::phase>=5?0.0F:12.0F')
physics=a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'#include "debug/list_join_fast.hpp"','#include "debug/list_join_fast.hpp"\n#include "debug/clip_capacity.hpp"')
edit(physics,'  const bool active=FastJoin::active;','  const unsigned active=ClipTrial::mode;')
edit(physics,'  for(unsigned arm=0;arm<3;++arm){\n    FastJoin::active=arm==1;',
 '''  ClipTrial::observe=false;
  for(unsigned arm=0;arm<5;++arm){
    ClipTrial::mode=arm==1?7:arm==3?8:0;''')
edit(physics,'const unsigned stage=MotionProbe::phase*3+arm;','const unsigned stage=MotionProbe::phase*5+arm;')
edit(physics,'  printf("JOINCHECKPOINT phase=%u target=%u z=%f speed=%f arms=3\\n",',
 '  printf("CLIPCHECKPOINT phase=%u target=%u z=%f speed=%f arms=5\\n",')
edit(physics,'FastJoin::checkpoint=false;FastJoin::active=active;g_frameDt=dt;',
 'FastJoin::checkpoint=false;ClipTrial::mode=active;g_frameDt=dt;')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
text=core.read_text();text='#include "debug/clip_capacity.hpp"\n'+text
start=text.index('void StaPipCore::render(');end=text.index('void StaPipCore::renderPkgs(',start)
body=text[start:end];assert body.count('bag->info->fullClipChecks')==11,body.count('bag->info->fullClipChecks')
body=body.replace('bag->info->fullClipChecks','fullClipChecks')
first=body.index('{')+1
body=body[:first]+'''\n  const bool fullClipChecks=bag->info->fullClipChecks && ClipTrial::mode!=8;
  if(ClipTrial::observe && ClipTrial::mode==8 && bag->info->fullClipChecks)
    ++ClipTrial::current.bypassBags;
'''+body[first:]
text=text[:start]+body+text[end:];core.write_text(text)
edit(core,'  if (classifyPackages && qbufferRenderer.isVU1ClippingEnabled()) {',
 '  if (classifyPackages && qbufferRenderer.isVU1ClippingEnabled() && ClipTrial::mode!=8) {')
edit(core,'          for (u32 b = 0; b < blocks;) {','''          const bool capacityClass = pkg.bag->texture && pkg.bag->texture->texture &&
              pkg.bag->color->many && !pkg.bag->color->single && !pkg.bag->lighting;
          const u32 wideCap=capacityClass ? ClipTrial::capacity(pkg.clipPlaneMask,
              qbufferRenderer.getBufferSize(),maxVertCount,trisPerChunk*3) : trisPerChunk*3;
          for (u32 b = 0; b < blocks;) {''')
edit(core,'            if (route == StaPipStripSubranges::Cull) {\n              while',
 '''            if(ClipTrial::observe && route==StaPipStripSubranges::Clip && capacityClass)
              ++ClipTrial::current.eligible;
            const bool wide=ClipTrial::mode==7 && capacityClass && route==StaPipStripSubranges::Clip;
            if (route == StaPipStripSubranges::Cull || wide) {
              const u32 cap=wide?wideCap:maxVertCount;
              while''')
edit(core,'                if ((n + next) * 3 > maxVertCount) break;',
 '''                if ((n + next) * 3 > cap) break;
                if(ClipTrial::observe && wide)++ClipTrial::current.removed;''')
edit(core,'            auto* buffer = qbufferRenderer.getBuffer();',
 '''            if(wide && n>trisPerChunk){
              TYRA_ASSERT(ClipTrial::footprint(pkg.clipPlaneMask,n*3)+91<=qbufferRenderer.getBufferSize(),"A24 clip output overflow");
              if(ClipTrial::observe){++ClipTrial::current.merged;ClipTrial::current.maxInput=std::max(ClipTrial::current.maxInput,n*3);}
            }
            auto* buffer = qbufferRenderer.getBuffer();''')
edit(core,'      for (u32 t = 0; t < triangles; t += trisPerChunk) {',
 '''      const bool capacityClass=pkg.bag->texture && pkg.bag->texture->texture &&
          pkg.bag->color->many && !pkg.bag->color->single && !pkg.bag->lighting &&
          !pkg.bag->billboard && !qbufferRenderer.hasProgramOverrides() && qbufferRenderer.isVU1ClippingEnabled();
      const u32 wideCap=capacityClass ? ClipTrial::capacity(pkg.clipPlaneMask,
          qbufferRenderer.getBufferSize(),maxVertCount,trisPerChunk*3) : trisPerChunk*3;
      const u32 chunk=ClipTrial::mode==7?wideCap/3:trisPerChunk;
      if(ClipTrial::observe && capacityClass)++ClipTrial::current.eligible;
      if(ClipTrial::observe && chunk>trisPerChunk && triangles>trisPerChunk){
        ++ClipTrial::current.merged;
        ClipTrial::current.removed+=(triangles+trisPerChunk-1)/trisPerChunk-(triangles+chunk-1)/chunk;
        ClipTrial::current.maxInput=std::max(ClipTrial::current.maxInput,std::min(chunk,triangles)*3);
      }
      for (u32 t = 0; t < triangles; t += chunk) {''')
edit(core,'        const u32 n = remaining < trisPerChunk ? remaining : trisPerChunk;',
 '''        const u32 n = remaining < chunk ? remaining : chunk;
        if(n>trisPerChunk)TYRA_ASSERT(ClipTrial::footprint(pkg.clipPlaneMask,n*3)+91<=qbufferRenderer.getBufferSize(),"A24 unrefined clip overflow");''')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
text=renderer.read_text();text='#include "debug/clip_capacity.hpp"\n'+text;renderer.write_text(text)
edit(renderer,'void StaPipQBufferRenderer::clip(StaPipQBuffer* buffer) {',
 '''void StaPipQBufferRenderer::clip(StaPipQBuffer* buffer) {
  TYRA_ASSERT(ClipTrial::mode!=8,"A24 bypass reached clip route");
  if(ClipTrial::observe)++ClipTrial::current.clipCalls;''')
if a.group_only:
 edit(probe,'inline constexpr U phaseCount=10;','inline constexpr U phaseCount=6;')
 edit(probe,'constexpr U modes[]={0,7,0,8,0,0,7,0,8,0};','constexpr U modes[]={0,7,0,0,7,0};')
 edit(game,'MotionProbe::phase >= 5','MotionProbe::phase >= 3')
 edit(game,'MotionProbe::phase>=5 ? lights==0 : lights!=0','MotionProbe::phase>=3 ? lights==0 : lights!=0')
 edit(game,'MotionProbe::phase>=5?0.0F:12.0F','MotionProbe::phase>=3?0.0F:12.0F')
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(dict(experiment='A24',source=str(a.source),sourceElf=binding['elfSha256'],
 modes=([0,7,0]*2 if a.group_only else [0,7,0,8,0]*2),hours=([12]*3+[0]*3 if a.group_only else [12]*5+[0]*5),
 unrefined=True,groupOnly=a.group_only,scope='0 baseline, 7 exact plane-capacity grouping, 8 destructive fullClipChecks bypass with normal frustum culling',
 sparse='all modes offset%128==64; exclude this and next frame',raster='0/7/0/8/0 checkpoint, 8 may differ'),indent=2)+'\n')
print(a.out)
