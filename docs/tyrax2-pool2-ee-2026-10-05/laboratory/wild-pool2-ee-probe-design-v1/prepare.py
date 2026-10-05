import pathlib,hashlib,json
ROOT=pathlib.Path(__file__).resolve().parent
LAB=ROOT.parent
OLD=LAB/'wild-pool2-static-probe-physical-v1/game/inc/pool2_static_probe.hpp'
ENGINE=LAB/'wild-pool2-ee-physical-v2/tyra/engine'
inputs=[OLD,ENGINE/'inc/renderer/3d/pipeline/static/core/stapip_pool_color_table.hpp',ENGINE/'inc/debug/night_ablation.hpp',ENGINE/'src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp',ENGINE/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp']
pins={str(p):{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size} for p in inputs}
s=OLD.read_text()
def replace(old,new):
 global s
 assert s.count(old)==1,old
 s=s.replace(old,new)
replace('#pragma once','#pragma once\n#ifndef POOL2_EE_PROBE_CLIP\n#define POOL2_EE_PROBE_CLIP 0\n#endif\nstatic_assert(POOL2_EE_PROBE_CLIP==0 || POOL2_EE_PROBE_CLIP==1,"separate clip design gate");')
replace('alignas(16) inline Color colors[192];','alignas(16) inline Color colors[192]; // immutable preexpanded baseline\nalignas(16) inline Color lazyColors[192]; // owned persistent candidate backing\nalignas(16) inline uint32_t ready[192];\ninline u32 colorGeneration=1;')
replace('    positions[i+2]=Vec4(x,y+0.75F,0.0F,1.0F);','    positions[i+2]=Vec4(x,y+0.75F,0.0F,1.0F);\n#if POOL2_EE_PROBE_CLIP\n    // Separate case: huge finite triangles straddle both side planes at z=0.\n    // Camera remains z=-20; no near-plane or behind-camera singularity.\n    positions[i]=Vec4(-1000.0F,-1.0F,0.0F,1.0F);\n    positions[i+1]=Vec4(1000.0F,-1.0F,0.0F,1.0F);\n    positions[i+2]=Vec4(0.0F,1.0F,0.0F,1.0F);\n#endif')
replace('  view.colors=memberColors;view.members=caseId/96;view.total=caseId;view.generation=generation;','  memset(static_cast<void*>(lazyColors),0xa5,sizeof lazyColors);\n  memset(ready,0,sizeof ready);\n  view.colors=memberColors;view.members=caseId/96;view.total=caseId;view.generation=colorGeneration;\n  view.expanded=arm ? static_cast<void*>(lazyColors) : nullptr;\n  view.ready=arm ? ready : nullptr;')
replace('  color.single=nullptr;color.many=colors;color.contentVersion=&generation;','  color.single=nullptr;color.many=arm ? lazyColors : colors;color.contentVersion=&colorGeneration;')
replace('  const bool cold=completed==0;\n  NightAblation::collectCounters=cold;\n  if(cold)NightAblation::poolTableCounter=NightAblation::PoolTableCounter{};','  const bool cold=completed==0;\n  // Oracle collection itself materializes eligible colors: keep it disabled.\n  NightAblation::collectCounters=false;')
replace('  const unsigned packages=(caseId+74)/75;\n  if(cold && (q.invocations!=packages || q.eligible!=packages || q.fallback!=0 ||\n     q.applied!=(arm?packages:0) || q.sourceVertices!=caseId ||\n     q.coldCompared!=packages || q.coldMismatches!=0 || q.invalid!=0))reject("cold_route_or_oracle");\n  if(!cold && memcmp(&saved,&q,sizeof q)!=0)reject("warm_counter_write");','  if(memcmp(&saved,&q,sizeof q)!=0)reject("disabled_counter_write");\n  unsigned readyCount=0;\n  for(unsigned i=0;i<caseId;++i) {\n    if(ready[i]==colorGeneration) {\n      ++readyCount;\n      if(!arm || memcmp(&lazyColors[i],&colors[i],16)!=0)reject("fallback_color_bytes");\n    } else {\n      if(ready[i]!=0)reject("invalid_ready_generation");\n      const auto* bytes=reinterpret_cast<const unsigned char*>(&lazyColors[i]);\n      for(unsigned j=0;j<16;++j)if(bytes[j]!=0xa5)reject("unready_source_write");\n    }\n  }\n#if POOL2_EE_PROBE_CLIP\n  if(arm && readyCount==0)reject("clip_did_not_materialize_source");\n#else\n  if(arm && readyCount!=0)reject("inside_case_materialized_source");\n#endif\n  if(!arm && readyCount!=0)reject("baseline_ready_write");\n  printf("LOG: POOL2EEPROBE_SOURCE clip=%u ready=%u total=%u generation=%u coldOracle=0\\n",\n         unsigned(POOL2_EE_PROBE_CLIP),readyCount,unsigned(caseId),unsigned(colorGeneration));fflush(stdout);')
replace('unsigned(arm),unsigned(caseId),unsigned(view.members),unsigned(repeats));fflush(stdout);','unsigned(arm),unsigned(caseId),unsigned(view.members),unsigned(repeats));fflush(stdout);\n  printf("LOG: POOL2EEPROBE_CONFIG kind=7 clip=%u lazy=%u oracle=0\\n",\n         unsigned(POOL2_EE_PROBE_CLIP),unsigned(arm));fflush(stdout);')
replace('inline u32 colorGeneration=1;', 'alignas(16) inline Color previousLazyColors[192];\nalignas(16) inline uint32_t previousReady[192];\ninline u32 colorGeneration=0;')
replace('  const bool cold=completed==0;', '  const bool cold=completed==0;\n  // Both arms author the same explicit color epoch before every render.\n  memcpy(previousLazyColors,lazyColors,sizeof lazyColors);\n  memcpy(previousReady,ready,sizeof ready);\n  ++colorGeneration;view.generation=colorGeneration;\n  const Color epoch0(31.0F+float(completed),63.0F,95.0F,128.0F);\n  const Color epoch1(95.0F-float(completed),15.0F,1.0F,128.0F);\n  memcpy(&memberColors[0],&epoch0,16);memcpy(&memberColors[1],&epoch1,16);\n  for(unsigned i=0;i<caseId;++i)memcpy(&colors[i],&memberColors[i/96],16);')
replace('      if(ready[i]!=0)reject("invalid_ready_generation");\n      const auto* bytes=reinterpret_cast<const unsigned char*>(&lazyColors[i]);\n      for(unsigned j=0;j<16;++j)if(bytes[j]!=0xa5)reject("unready_source_write");', '      if(ready[i]>=colorGeneration || ready[i]!=previousReady[i])reject("invalid_ready_generation");\n      if(memcmp(&lazyColors[i],&previousLazyColors[i],16)!=0)reject("unready_source_write");')
replace('// immutable preexpanded baseline', '// preexpanded baseline for the current color epoch')
out=ROOT/'source';out.mkdir(exist_ok=True)
(out/'pool2_static_probe.hpp').write_text(s)
(out/'pool2_static_probe_clip.hpp').write_text('#pragma once\n#define POOL2_EE_PROBE_CLIP 1\n#include "pool2_static_probe.hpp"\n')
(out/'stapip_pool_color_table.hpp.reference').write_bytes(inputs[1].read_bytes())
cfg=ROOT/'configs';cfg.mkdir(exist_ok=True)
for a in [0,1]:
 for count in [96,192]:
  for r in [1,2,3]:(cfg/f'arm{a}-vertices{count}-repeat{r}.cfg').write_text(f'{a} {count} {r}\n')
for p in inputs:assert hashlib.sha256(p.read_bytes()).hexdigest()==pins[str(p)]['sha256']
outputs={str(p.relative_to(ROOT)):{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size} for p in ROOT.rglob('*') if p.is_file() and p.name!='source-proof.json'}
(ROOT/'source-proof.json').write_text(json.dumps({'status':'SOURCE_ONLY_NOT_COMPILED_OR_EXECUTED','inputs':pins,'outputs':outputs,'input_hashes_revalidated':True,'configuration':'Original strict three integers: arm 0/1, vertices96/192, repeat1..3. Compile-time clip=1 is a separate fixture, default0 preserves original geometry.','limitations':'No target build, actual route/output or device proof. Clip geometry intended to straddle side planes; route must be verified when run. No new waits: original synchronization retained.'},indent=2))
print('Prepared source-only',ROOT)
