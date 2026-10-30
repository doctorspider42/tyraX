"""Private compact solver replay in EE scratchpad, with save/restore included."""
from pathlib import Path
import argparse,json,subprocess

SPR=r'''
// No in-tree packet/renderer owner uses SPR. This private CPU-only interval
// checks live stack/DMA use and restores all16KiB before any host I/O/rendering.
alignas(64) inline unsigned scratchSave[4096];
inline unsigned scratchRestoreErrors=0;
inline bool scratchAvailable(unsigned bodies){
  unsigned sp;asm volatile("move %0,$sp":"=r"(sp));
  const unsigned dma=(*reinterpret_cast<volatile unsigned*>(0x1000d000)) |
                     (*reinterpret_cast<volatile unsigned*>(0x1000d400));
  return bodies<=16384/sizeof(CompactBody) && (sp&0xffffc000u)!=0x70000000u && !(dma&0x100u);
}
inline CompactBody* scratchBegin(){
  auto* words=reinterpret_cast<volatile unsigned*>(0x70000000u);
  for(unsigned i=0;i<4096;++i)scratchSave[i]=words[i];
  asm volatile("sync.l":::"memory");
  return reinterpret_cast<CompactBody*>(0x70000000u);
}
inline void scratchEnd(){
  auto* words=reinterpret_cast<volatile unsigned*>(0x70000000u);
  for(unsigned i=0;i<4096;++i)words[i]=scratchSave[i];
  asm volatile("sync.l":::"memory");
  for(unsigned i=0;i<4096;++i)if(words[i]!=scratchSave[i])++scratchRestoreErrors;
}
inline std::vector<RbStep> expectedScratch;
inline std::vector<RbContact> scratchContacts;
'''

def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    p=root/'metadata.json';meta=json.loads(p.read_text(encoding='utf8'));assert meta.get('solver_layout') and not meta.get('solver_scratchpad')
    p=root/'engine/engine/inc/debug/solver_layout_probe.hpp';s=p.read_text(encoding='utf8')
    before='inline unsigned errors=0,cases=0;';assert s.count(before)==1;s=s.replace(before,SPR+'\n'+before,1)
    before='for(int repeat=0;repeat<4;++repeat)for(int order=0;order<2;++order){\n    const int mode=order^(repeat&1);';assert s.count(before)==1
    s=s.replace(before,'for(int repeat=0;repeat<4;++repeat)for(int order=0;order<3;++order){\n    const int mode=(repeat&1)?2-order:order;',1)
    before='''    const auto first=PhysicsPmu::mark();
    if(mode){
      for(unsigned i=0;i<nb;++i)compact[i]=pack(original[i]);
      core(compact.data(),workContacts.data(),int(nc));
      for(unsigned i=0;i<nb;++i)scatter(full[i],compact[i]);
    }else core(full.data(),workContacts.data(),int(nc));''';assert s.count(before)==1
    s=s.replace(before,'''    const bool useScratch=mode==2 && scratchAvailable(nb);
    const auto first=PhysicsPmu::mark();
    if(mode){
      auto* hot=useScratch?scratchBegin():compact.data();
      for(unsigned i=0;i<nb;++i)new(hot+i) CompactBody(pack(original[i]));
      core(hot,workContacts.data(),int(nc));
      for(unsigned i=0;i<nb;++i)scatter(full[i],hot[i]);
      if(useScratch)scratchEnd();
    }else core(full.data(),workContacts.data(),int(nc));''',1)
    before='''    if(!repeat){if(mode){expectedCompact=full;compactContacts=workContacts;}else{expectedFull=full;fullContacts=workContacts;}}''';assert s.count(before)==1
    s=s.replace(before,'''    if(mode==2)printf("SOLVERSPR phase=%d frame=%d repeat=%d used=%d restore_errors=%u\\n",phase,frame,repeat,useScratch,scratchRestoreErrors);
    if(!repeat){if(mode==2){expectedScratch=full;scratchContacts=workContacts;}else if(mode){expectedCompact=full;compactContacts=workContacts;}else{expectedFull=full;fullContacts=workContacts;}}''',1)
    before='  errors+=referenceDiff+compactDiff;';assert s.count(before)==1
    s=s.replace(before,'''  unsigned scratchDiff=0;
  for(unsigned i=0;i<steps.size();++i)scratchDiff+=!sameBody(expectedCompact[i],expectedScratch[i]);
  for(unsigned i=0;i<contacts.size();++i)scratchDiff+=!sameContact(compactContacts[i],scratchContacts[i]);
  printf("SOLVERSPRCHECK phase=%d frame=%d heap_scratch_diff=%u restore_errors=%u\\n",phase,frame,scratchDiff,scratchRestoreErrors);
'''+before,1)
    p.write_bytes(s.encode())
    meta.update(solver_scratchpad=True,active_mode='solver-scratchpad-screening',scene_timing_qualified=False,
        scratchpad_scope='Private CPU-only replay. Runtime stack/DMA guard, heap fallback above170 bodies or unavailable SPR, all16KiB saved/restored and verified within mode2 timing. No contact/body omission; full production ownership/API qualification remains pending.')
    (root/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve())
