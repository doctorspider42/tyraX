"""Screen compact velocity storage with unchanged six/three scalar solver math."""
from pathlib import Path
import argparse,json,subprocess

PROBE=r'''
#pragma once
#include "debug/physics_pmu_probe.hpp"
#include <cstring>
#include <vector>
namespace Vehicle_playground { namespace SolverLayoutProbe {
using namespace runtime_detail;
struct alignas(16) CompactBody {
  float invMass, invIw[9];
  RbV v,w,vp,wp;
};
static_assert(sizeof(CompactBody)==96,"wire/layout contract");
inline CompactBody pack(const RbStep& s){
  CompactBody b;memset(&b,0,sizeof(b));b.invMass=s.invMass;
  for(int k=0;k<9;++k)b.invIw[k]=s.invIw[k];
  b.v=s.v;b.w=s.w;b.vp=s.vp;b.wp=s.wp;return b;
}
inline void scatter(RbStep& s,const CompactBody& b){s.v=b.v;s.w=b.w;s.vp=b.vp;s.wp=b.wp;}
@@CORE@@
inline unsigned errors=0,cases=0;
inline int pending=-1;
inline std::vector<RbStep> original,full,expectedFull,expectedCompact;
inline Tyra::SolverBuffer<CompactBody> compact;
inline std::vector<RbContact> originalContacts,workContacts,fullContacts,compactContacts;
inline unsigned bits(float x){unsigned u;memcpy(&u,&x,4);return u;}
inline bool same(RbV a,RbV b){return bits(a.x)==bits(b.x)&&bits(a.y)==bits(b.y)&&bits(a.z)==bits(b.z);}
inline bool sameBody(const RbStep& a,const RbStep& b){return same(a.v,b.v)&&same(a.w,b.w)&&same(a.vp,b.vp)&&same(a.wp,b.wp);}
inline bool sameContact(const RbContact& a,const RbContact& b){return bits(a.jn)==bits(b.jn)&&bits(a.jp)==bits(b.jp)&&same(a.ft,b.ft);}
inline void delta(float a,float b,float& maximum,unsigned& zeroSigns){
  if(a==0 && b==0 && bits(a)!=bits(b))++zeroSigns;
  const float d=fabsf(a-b);if(d>maximum)maximum=d;
}
inline void delta(RbV a,RbV b,float& maximum,unsigned& zeroSigns){delta(a.x,b.x,maximum,zeroSigns);delta(a.y,b.y,maximum,zeroSigns);delta(a.z,b.z,maximum,zeroSigns);}
inline void run(const std::vector<RbStep>& steps,const std::vector<RbContact>& contacts,int phase,int frame){
  if(contacts.empty())return;
  const unsigned nb=steps.size(),nc=contacts.size();
  original=steps;originalContacts=contacts;compact.resize(nb);
  char name[96];snprintf(name,sizeof(name),"solver-layout-%d-%d.bin",phase,frame);
  FILE* f=fopen(name,"wb");if(!f){++errors;return;}
  const unsigned header[6]={0x534c5954u,1u,nb,nc,sizeof(RbStep),sizeof(RbContact)};
  fwrite(header,sizeof(header),1,f);
  for(const auto& s:steps){const auto b=pack(s);fwrite(&b,sizeof(b),1,f);}
  fwrite(contacts.data(),sizeof(RbContact),nc,f);fclose(f);
  printf("SOLVERLAYOUTCASE phase=%d frame=%d bodies=%u contacts=%u full_stride=%u compact_stride=%u contact_stride=%u\n",phase,frame,nb,nc,unsigned(sizeof(RbStep)),unsigned(sizeof(CompactBody)),unsigned(sizeof(RbContact)));
  // Every repeat restores identical prepared inputs outside timing. Compact
  // timings include gathering all hot fields and scattering four velocities.
  for(int repeat=0;repeat<4;++repeat)for(int order=0;order<2;++order){
    const int mode=order^(repeat&1);
    full=original;workContacts=originalContacts;
    if(!PhysicsPmu::enter(0)){++errors;return;}
    const auto first=PhysicsPmu::mark();
    if(mode){
      for(unsigned i=0;i<nb;++i)compact[i]=pack(original[i]);
      core(compact.data(),workContacts.data(),int(nc));
      for(unsigned i=0;i<nb;++i)scatter(full[i],compact[i]);
    }else core(full.data(),workContacts.data(),int(nc));
    const auto last=PhysicsPmu::mark();PhysicsPmu::leave();
    if((last.a|last.b)&0x80000000u)++errors;
    printf("SOLVERLAYOUT phase=%d frame=%d repeat=%d mode=%d ticks=%u im=%u dm=%u\n",phase,frame,repeat,mode,last.t-first.t,last.a-first.a,last.b-first.b);
    if(!repeat){if(mode){expectedCompact=full;compactContacts=workContacts;}else{expectedFull=full;fullContacts=workContacts;}}
  }
  pending=frame;++cases;fflush(stdout);
}
inline void after(const std::vector<RbStep>& steps,const std::vector<RbContact>& contacts,int phase,int frame){
  if(pending!=frame)return;
  pending=-1;unsigned referenceDiff=0,compactDiff=0,layoutDiff=0,zeroSigns=0;float maximum=0,layoutMaximum=0;
  for(unsigned i=0;i<steps.size();++i){
    const auto& a=steps[i];const auto& b=expectedFull[i];const auto& c=expectedCompact[i];
    referenceDiff+=!sameBody(a,b);compactDiff+=!sameBody(a,c);layoutDiff+=!sameBody(b,c);
    delta(a.v,b.v,maximum,zeroSigns);delta(a.w,b.w,maximum,zeroSigns);delta(a.vp,b.vp,maximum,zeroSigns);delta(a.wp,b.wp,maximum,zeroSigns);
    unsigned ignored=0;delta(b.v,c.v,layoutMaximum,ignored);delta(b.w,c.w,layoutMaximum,ignored);delta(b.vp,c.vp,layoutMaximum,ignored);delta(b.wp,c.wp,layoutMaximum,ignored);
  }
  for(unsigned i=0;i<contacts.size();++i){
    const auto& a=contacts[i];const auto& b=fullContacts[i];const auto& c=compactContacts[i];
    referenceDiff+=!sameContact(a,b);compactDiff+=!sameContact(a,c);layoutDiff+=!sameContact(b,c);
    delta(a.jn,b.jn,maximum,zeroSigns);delta(a.jp,b.jp,maximum,zeroSigns);delta(a.ft,b.ft,maximum,zeroSigns);
    unsigned ignored=0;delta(b.jn,c.jn,layoutMaximum,ignored);delta(b.jp,c.jp,layoutMaximum,ignored);delta(b.ft,c.ft,layoutMaximum,ignored);
  }
  errors+=referenceDiff+compactDiff;
  printf("SOLVERLAYOUTCHECK phase=%d frame=%d reference_diff=%u compact_diff=%u errors=%u cases=%u\n",phase,frame,referenceDiff,compactDiff,errors,cases);fflush(stdout);
  printf("SOLVERLAYOUTDELTA phase=%d frame=%d maximum=%.9g zero_signs=%u layout_diff=%u layout_maximum=%.9g\n",phase,frame,maximum,zeroSigns,layoutDiff,layoutMaximum);fflush(stdout);
}
} } // Vehicle_playground::SolverLayoutProbe
'''

def probe_source(repo):
    canonical=(repo/'src/game_templates.inc').read_text(encoding='utf8')
    a=canonical.index('  auto applyImpulse =');b=canonical.index('\n#endif',a)
    core=canonical[a:b]
    before='  if(warmStart)for(auto& c:contacts)applyImpulse(c,rbAdd(rbMul(c.n,c.jn),c.ft));';assert core.count(before)==1
    core=core.replace(before,'').replace('RbStep','Body')
    core='template<class Body> __attribute__((noinline,noclone))\ninline void core(Body* steps,RbContact* contacts,int nc){\n'+core+'\n}\n'
    return PROBE.replace('@@CORE@@',core)

def install(repo,root,refresh=False):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    p=root/'metadata.json';meta=json.loads(p.read_text(encoding='utf8'))
    if refresh:
        assert meta.get('solver_layout') and not meta.get('solver_scratchpad')
        (root/'engine/engine/inc/debug/solver_layout_probe.hpp').write_bytes(probe_source(repo).encode());return
    assert meta.get('physics_pmu') and meta.get('world_cache') and not meta.get('solver_layout')
    engine=root/'engine/engine';project=root/'district'
    probe=probe_source(repo)
    p=engine/'inc/debug/collision_bench.hpp';s=p.read_text(encoding='utf8')
    for before in ['  PhysicsPmu::frame(frame,contacts,awake,ready && !finished && frame>=warm && frame<total);\n','  PhysicsPmu::flush(phase);\n','  PhysicsPmu::Scope pmu{5};\n']:
        assert s.count(before)==1,before;s=s.replace(before,'',1)
    before='  PhysicsPmu::Scope pmu;\n  explicit StageScope(int s):start(ticks()),stage(s),pmu(s<=3?s+1:s==6?6:-1) {}';assert s.count(before)==1
    s=s.replace(before,'  explicit StageScope(int s):start(ticks()),stage(s) {}',1)
    before='struct PhysicsScope {\n  PhysicsPmu::Owner owner;\n  PhysicsPmu::Scope pmu;\n  U start;\n  PhysicsScope() : owner(frame%3),pmu(0),start(ticks()) {}';assert s.count(before)==1
    s=s.replace(before,'struct PhysicsScope {\n  U start;\n  PhysicsScope() : start(ticks()) {}',1)
    bench=s
    p=project/'src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8')
    before='#include "game_runtime.gen.hpp"';assert s.count(before)==1
    s=s.replace(before,before+'\n#include "debug/solver_layout_probe.hpp"',1)
    before='  auto applyImpulse =';assert s.count(before)==1
    s=s.replace(before,'  if(CB::frame==300 || CB::frame==600)SolverLayoutProbe::run(steps,contacts,CB::phase,CB::frame);\n'+before,1)
    a=s.index('  auto applyImpulse =');b=s.index('\n#endif',a)
    s=s[:b]+'\n  SolverLayoutProbe::after(steps,contacts,CB::phase,CB::frame);'+s[b:]
    # Both arms now use the selected production world query; only replay storage differs.
    before='if(CB::baseline)broadPhaseNear(area,bpNearPhys);else broadPhaseWorldNear(area,bpNearPhys);';assert s.count(before)==1
    s=s.replace(before,'broadPhaseWorldNear(area,bpNearPhys);',1)
    (engine/'inc/debug/solver_layout_probe.hpp').write_bytes(probe.encode())
    (engine/'inc/debug/collision_bench.hpp').write_bytes(bench.encode());p.write_bytes(s.encode())
    meta.update(solver_layout=True,physics_pmu=False,active_mode='solver-layout-screening',same_elf_admission_qualified=False,
        scene_timing_qualified=False,comparison='Fixed-input scalar solver replay, original full body stride versus compact96-byte hot fields; compact timing includes pack/scatter. Both live arms use cached world admission. Not a full-frame timing qualification.')
    (root/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--refresh-probe',action='store_true');a=p.parse_args()
    install(a.repo.resolve(),a.scratch.resolve(),a.refresh_probe)
