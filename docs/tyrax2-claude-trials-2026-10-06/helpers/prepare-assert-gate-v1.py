from pathlib import Path
import shutil,json,hashlib,re
b=Path('F:/Projects/tyrax2-lab-20261001'); repo=Path('F:/Projects/tyra-editor')
base=b/'object-route-physical-v1';out=b/'assert-gate-physical-v1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert not out.exists()
m=json.loads((base/'target-source-manifest.json').read_text())
for name,digest in m['files'].items():
 assert sha(base/name)==digest,name
 p=out/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/name,p)
for name in ('res','.res-baked'):shutil.copytree(base/'game'/name,out/'game'/name,dirs_exist_ok=True)
def edit(name,old,new):
 p=out/name;s=p.read_text(encoding='utf8');assert s.count(old)==1,(name,old);p.write_bytes(s.replace(old,new).encode())
for name in ('night_plan.hpp','night_runtime.hpp'):
 p=out/'tyra/engine/inc/debug'/name;p.write_bytes(p.read_text().replace('kind==22','kind==25').encode())
edit('tyra/engine/inc/debug/night_runtime.hpp','NightAblation::producerTiming=NightAblation::producerSelected&&NightAblation::producerEnabled&&index<5400&&o>=800&&o<1120;','NightAblation::producerTiming=false;')
productionPins={}
for name in ('stapip_core.cpp','stapip_qbuffer_renderer.cpp'):
 rel='tyra/engine/src/renderer/3d/pipeline/static/core/'+name
 src=repo/'vendor'/rel;productionPins[rel]=sha(src);shutil.copyfile(src,out/rel)
name='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'
p=out/name;stock=p.read_text();s=stock
start=s.index('  TYRA_ASSERT(bag->vertices != nullptr,',s.index('void StaPipCore::render'))
env=s.index('#if TYRA_VU1_EXP_ENV_NORMALIZED',start)
tail=s.index('  TYRA_ASSERT(bag->info->transformationType',env)
end=s.index('  HardwareTrace::Scope traceBounds',tail)
block1=s[start:env];block2=s[tail:end]
assert block1.count('TYRA_ASSERT(')==10 and block2.count('TYRA_ASSERT(')==2
pre='''  // Private bounded-fixture experiment: only original validation expressions.
  // Cold witnesses always execute the original checks in both arms. They do
  // not prove counts in hot windows, where On bypasses these checks only.
  const bool assertGateRequested = NightAblation::producerSelected &&
                                   NightAblation::producerEnabled;
  const bool assertGateCold = NightAblation::collectCounters;
#ifdef NDEBUG
  constexpr u32 assertGateActiveChecks = 0;
#else
  constexpr u32 assertGateActiveChecks = 12;
#endif
  NightAblation::producerCount(0,0);
  NightAblation::producerCount(0,1,assertGateActiveChecks);
  NightAblation::producerCount(0,2,
      (!assertGateRequested || assertGateCold) ? assertGateActiveChecks : 0);
  NightAblation::producerCount(0,3,
      (assertGateRequested && !assertGateCold) ? assertGateActiveChecks : 0);
  NightAblation::producerCount(0,4,assertGateRequested ? 1 : 0);
  if (!assertGateRequested || assertGateCold) {
'''
s=s[:start]+pre+block1+'  }\n'+s[env:tail]+'  if (!assertGateRequested || assertGateCold) {\n'+block2+'  }\n\n'+s[end:]
s=s.replace('#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
p.write_bytes(s.encode())
for n in m['files']:m['files'][n]=sha(out/n)
m['frozen']=False
(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode())
changes=sorted(n for n,d in m['files'].items()if d!=json.loads((base/'target-source-manifest.json').read_text())['files'][n])
proof=dict(frozen=False,sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=len(m['files']),baseManifestSha256=sha(base/'target-source-manifest.json'),changes=changes,productionRestorePins=productionPins,assertStatements=12,firstBlockStatements=10,secondBlockStatements=2,envNormalizedPreservedBetweenBlocks=True,leadingEmptyGuardPreserved=True,noAddedScopedClocks=True,commonExtraBranchesUnpriced=True,sparseColdAlwaysRunsOriginalChecks=True,fullWindowActivationObserved=False,publicApiSafeToBypass=False,candidate='On skips the 12 original validation expressions only in hot frames; Off evaluates original expressions. Both arms run original checks on cold frames750/1155. No global NDEBUG or production change.')
(out/'root-source-freeze.json').write_bytes((json.dumps(proof,indent=2)+'\n').encode())
(out/'source-review.md').write_bytes(b'# Private assertion gate draft\n\nNot released or frozen. Stock head has twelve assertions despite its inherited comment saying thirteen. The ten-expression block and two-expression block retain original expression/message bytes, order and short-circuit semantics. The ENV_NORMALIZED operation remains between those blocks and outside both gates. Cold sparse rows count twelve actually executed checks in both arms, zero actually bypassed checks, and requested On-arm state separately. Hot activation is inferred from source and arm state, not counted. All source clocks are unchanged. No global NDEBUG, no production API claim.\n')
print(json.dumps(proof,indent=2))
