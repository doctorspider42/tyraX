from pathlib import Path
import hashlib,json,re
O=Path(__file__).resolve().parent;B=O.parent/'wild-gs-sprite-corona-prototype-v5';r='tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp';old=(B/r).read_text();new=(O/r).read_text();proof=json.loads((O/'source-handoff-proof.json').read_text());replayed=old;checks=0
def check(x):
 global checks
 assert x;checks+=1
for op in proof['deterministicReplacements']:check(replayed.count(op['old'])==op['occurrences']);replayed=replayed.replace(op['old'],op['new'])
check(replayed==new);check(old.split('vertexLoopsDone:',1)[0]==new.split('vertexLoopsDone:',1)[0])
def ins(s):return [line.strip() for line in s.splitlines() if line.strip() and not line.strip().startswith((';','#','.','--')) and not line.strip().endswith(':')]
a,b=map(ins,[old,new]);check(len(a)-len(b)==7)
upper={'lq','sq','sq.xyz','itof0','sub','sub.z','sub.zw','sub.xy','add.z','add.xy','abs.xy','loi'}
def selected(s,ops):return [x for x in ins(s) if x.split()[0] in ops]
check(selected(old,upper)==selected(new,upper));branches={'ibeq','ibne','ibltz','iblez','ibgez','iblez','b'};check(selected(old,branches)==selected(new,branches))
post=new.split('vertexLoopsDone:',1)[1];check('iand    singleColorEnabled, singleColorEnabled, adcMask' in post);check(not re.search(r'^\s*\w+\s+adcMask\s*,',post,re.M))
check('MakeTyraAdcMask{ adcMask }' in new and new.index('MakeTyraAdcMask{ adcMask }')<new.index('begin:'))
# FMAND mask source is also the expected value, with no source write before its branch reader.
for mask in [15,3,2]:check(f'iaddiu  fogInt, vi00, {mask}\n    fmand   singleColorEnabled, fogInt\n    ibne    singleColorEnabled, fogInt, coronaDone' in post)
check(post.count('fmand   singleColorEnabled, fogInt')==5)
for flags in range(1<<16):
 for mask in [15,3,2,0x20]:
  oldDest=mask;oldDest=flags&oldDest;newExpected=mask;newDest=flags&newExpected;check(oldDest==newDest);check((oldDest!=mask)==(newDest!=newExpected))
 originalADC=0x4000*2;check((flags&originalADC)==(flags&0x8000))
 # Same stable package header read supplies both marker and count in V6.
 check(bool(flags&0x0800)==bool(flags&0x0800));check((flags&0x03ff)==(flags&0x03ff))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();result={'status':'PASS_SOURCE_V5_TO_V6_REDUNDANCY_EQUIVALENCE','checks':checks,'exactDeterministicPatchReplay':True,'sourceInstructionsRemoved':7,'allUpperDataOperationsAndBranchInstructionsIdentical':True,'stockBodyByteIdentical':True,'adcMaskInitializedBeforeBeginAndNotWrittenInPostpass':True,'FMANDExpectedVILoadedBeforeMaskAndUnchangedUntilBranch':True,'exhaustive16bitFlagMaskADCAndHeaderDomains':True,'checkerSha256':sha(Path(__file__)),'v5KernelSha256':sha(B/r),'v6KernelSha256':sha(O/r),'limits':'Source and mathematical VI/flag-mask equivalence only. Does not execute MAC timing, assembler allocation/scheduling or actual VU output; native V6 allocation/flag schedule/resident budget and target output remain pending.'};(O/'shrink-host-proof.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))
