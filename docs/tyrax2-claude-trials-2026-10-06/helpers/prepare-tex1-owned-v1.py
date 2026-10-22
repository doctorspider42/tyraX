from pathlib import Path
import hashlib,json,shutil
b=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');base=b/'object-route-physical-v1';f=b/'tex1-owned-physical-v1';assert not(f/'target-source-manifest.json').exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text())
for n,d in m['files'].items():
 assert sha(base/n)==d;p=f/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/n,p)
for n in ('res','.res-baked'):shutil.copytree(base/'game'/n,f/'game'/n,dirs_exist_ok=True)
def edit(n,a,z):
 p=f/n;s=p.read_text(encoding='utf8');assert s.count(a)==1,(n,a,s.count(a));p.write_bytes(s.replace(a,z).encode())
for n in ('stapip_core.cpp','stapip_qbuffer_renderer.cpp'):
 rel='tyra/engine/src/renderer/3d/pipeline/static/core/'+n;shutil.copyfile(repo/'vendor'/rel,f/rel)
for n in ('night_plan.hpp','night_runtime.hpp'):
 p=f/'tyra/engine/inc/debug'/n;p.write_bytes(p.read_text(encoding='utf8').replace('kind==22','kind==28').encode())
n='tyra/engine/inc/debug/night_runtime.hpp';edit(n,'NightAblation::producerTiming=NightAblation::producerSelected&&NightAblation::producerEnabled&&index<5400&&o>=800&&o<1120;','NightAblation::producerTiming=false;')
h='tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp';edit(h,'  void init(RendererCore* t_core, prim_t* prim, lod_t* lod);','  void init(RendererCore* t_core, prim_t* prim, lod_t* lod);\n  // Private kind28 experiment. Called only for StaPipCore-owned initialized LOD.\n  void enableOwnedTex1();')
edit(h,'  lod_t* lod;','  lod_t* lod;\n  bool ownedTex1 = false;\n  u64 ownedTex1Linear = 0, ownedTex1Nearest = 0, ownedTex1Current = 0;')
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';edit(n,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
edit(n,'  lod = t_lod;','  lod = t_lod;\n  ownedTex1 = false; // Generic public init never assumes caller ownership; no LOD reads here.')
anchor='void StaPipQBufferRenderer::reinitVU1() {'
func='''// Private kind28 experiment: only immutable scalar fields of Core-owned LOD.
// Its constructor calls setLod before init; renderer init establishes lod pointer.
void StaPipQBufferRenderer::enableOwnedTex1() {
  ownedTex1Linear = GS_SET_TEX1(lod->calculation,lod->max_level,LOD_MAG_LINEAR,LOD_MIN_LINEAR,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));
  ownedTex1Nearest = GS_SET_TEX1(lod->calculation,lod->max_level,LOD_MAG_NEAREST,LOD_MIN_NEAREST,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));
  ownedTex1Current = GS_SET_TEX1(lod->calculation,lod->max_level,lod->mag_filter,lod->min_filter,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));
  ownedTex1 = true;
}

'''
edit(n,anchor,func+anchor)
old='''      q.dw[0] = GS_SET_TEX1(lod->calculation, lod->max_level, lod->mag_filter,
                            lod->min_filter, lod->mipmap_select, lod->l,
                            (int)(lod->k * 16.0F));'''
new='''      const bool useOwnedTex1 = ownedTex1 && NightAblation::producerEnabled;
      q.dw[0] = useOwnedTex1 ? ownedTex1Current : GS_SET_TEX1(lod->calculation,lod->max_level,lod->mag_filter,lod->min_filter,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));
      if(NightAblation::collectCounters){
        const u64 reference=GS_SET_TEX1(lod->calculation,lod->max_level,lod->mag_filter,lod->min_filter,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));
        NightAblation::producerCount(0,0);NightAblation::producerCount(0,1,ownedTex1?1:0);NightAblation::producerCount(0,2,useOwnedTex1?1:0);NightAblation::producerCount(0,3,2);NightAblation::producerCount(0,4,q.dw[0]!=reference?1:0);
        NightAblation::producerCount(1,0,!ownedTex1?1:0);NightAblation::producerCount(1,1,!useOwnedTex1?1:0);
      }'''
edit(n,old,new)
edit(n,'    lod->min_filter = LOD_MIN_NEAREST;\n  }\n}', '    lod->min_filter = LOD_MIN_NEAREST;\n  }\n  if(ownedTex1)ownedTex1Current=bag->textureMappingType==TyraLinear?ownedTex1Linear:ownedTex1Nearest;\n}')
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp';edit(n,'  qbufferRenderer.init(t_core, &prim, &lod);','  qbufferRenderer.init(t_core, &prim, &lod);\n  qbufferRenderer.enableOwnedTex1();')
for n in m['files']:m['files'][n]=sha(f/n)
m['frozen']=False;(f/'draft-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());(f/'draft-review.json').write_bytes((json.dumps(dict(status='DRAFT_NOT_FROZEN',sourceFiles=501,baseManifestSha256=sha(base/'target-source-manifest.json'),abiClassGrowthUnqualified=True,sourceScope='Explicit StaPipCore-owned LOD prepacked TEX1, generic public init falls back. Send-before-setInfo maintained. Cold full64-bit parity every TEX1 emit; no new Count clocks. All physical claims pending native/root/hardware.',commonCandidatePreparationFootprintUnpriced=True,phaseCandidateFlags='On/Off/On or Off/On/Off, full night all phases.'),indent=2)+'\n').encode());print('kind28 draft501 source closure')
