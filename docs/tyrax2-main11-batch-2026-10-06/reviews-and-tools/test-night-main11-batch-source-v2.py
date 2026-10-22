"""Pure host source/closure controls. No game compilation or runtime."""
from pathlib import Path
import hashlib,json,argparse
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,default=Path('F:/Projects/tyrax2-lab-20261001/night-main11-batch-source-controls-v2.json'))
controlArgs=parser.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001');f=lab/'night-main11-batch-physical-v2'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((f/'draft-source-manifest.json').read_text(encoding='utf8'))
checks=[]
def check(name,condition):
    checks.append({'name':name,'pass':bool(condition)})
    assert condition,name
check('unfrozen501',not m['frozen'] and len(m['files'])==501)
check('all501Hashes',all(sha(f/n)==h for n,h in m['files'].items()))
text=lambda n:(f/n).read_text(encoding='utf8')
c=text('game/src/gen/game_collision.gen.cpp');p=text('game/src/gen/game_physics.gen.cpp')
core=text('tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp')
r=text('tyra/engine/inc/debug/night_runtime.hpp');a=text('tyra/engine/inc/debug/night_ablation.hpp')
check('noCompanionCensusHooks',all('CompanionCensus' not in text(n) for n in m['files'] if n.endswith(('.cpp','.hpp'))))
check('producerTimingDisabled','NightAblation::producerTiming=false;' in r and 'producerEnabled&&index<5400&&o>=800' not in r)
check('ordinarySamplerUnchanged',sha(f/'tyra/engine/inc/debug/night_sampler.hpp')==sha(lab/'companion-census-physical-v1/tyra/engine/inc/debug/night_sampler.hpp'))
check('qrendererProductionExact',sha(f/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp')==sha(Path('F:/Projects/tyra-editor/vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp')))
check('cacherProductionExact',sha(f/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_bag_bboxes_cacher.cpp')==sha(Path('F:/Projects/tyra-editor/vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_bag_bboxes_cacher.cpp')))
basecore=Path('F:/Projects/tyra-editor/vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text(encoding='utf8')
reverted=core.replace('#include "debug/night_ablation.hpp"\n','').replace('  // PRIVATE kind29 cold actual producer acceptance, after original main cull.\n  NightMainBatch::accepted();\n','')
check('CoreOnlyColdAcceptedAnnotation',reverted==basecore)
check('preSecondaryDirtyReconcile',p.index('prepareNightMainCarriers();')<p.index('if (ro.dirty) rebuildObjectGeometry(ri);'))
private=c[c.index('// PRIVATE kind29 copied'):c.index('// never pull closer')]
check('noPrivateGlobalBatchMembershipWrites','objectBatchOf.assign' not in private and 'objectBatchOf[' not in private.replace('objectBatchOf[i]!=-1',''))
check('copyOriginalActiveArrays','p.vertices.begin(),p.vertices.end()' in private and 'p.colors.begin(),p.colors.end()' in private and 'addBox(' not in private)
check('noPrivateCountReadsOrWaits',all(x not in private for x in ('producerTick(', 'profTicks(', 'align3D(', 'dma_wait_', 'mfc0 ')))
check('originalCarrierNeverFreedByPrivate',all(x not in private for x in ('objectGeometry.clear','parts.clear','parts.erase','parts.resize','p.vertices.clear','p.colors.clear')))
check('groupStorageNeverRebakedWithinScene',private.count('g.owned.vertices.insert')==1 and 'if(!g.ready&&!g.demoted)' in private)
check('noOutOfArrayLightPointerSubtract','pick-rc.dynLights' not in private and 'pick==&rc.spot?9:0' in private)
check('exactInfoAndFullLightingKey','nightMainInfoSame' in private and 'key!=g.key' in private and 'nightMainLightKey(key,rc.spot)' in private and 'rc.dynLights[k]' in private)
check('fullyInsideAndEffectiveSpotDomain','CoreBBoxFrustum::IN_FRUSTUM' in private and 'pick!=chosen||on!=active' in private and 'stapipSpotHasNoInfluence(s,box)' in private)
check('delayedEntirePreparation', 'if(!NightAblation::producerSelected||NightSampler::expectedIndex<600)return;' in private and 'if(!NightAblation::producerSelected||NightSampler::expectedIndex<600)return false;' in private)
check('coldWhyProtocol','NIGHTMAINBATCHWHY phase=%u offset=%u enabled=%u group=%u reason=%u firstDiff=%u keyReady=%u keyAdmitted=%u' in a and 'NightMainBatch::why[gi]={g.reason,g.firstDiff,unsigned(g.keyInitialized),unsigned(g.keyAdmitted)};' in private)
check('whyExactKeyDiffCategories', 'g.firstDiff=d;' in private and 'd<16' in private and 'd<40' in private and 'd<g.keyLightEnd' in private and 'd<g.keyFogEnd' in private and '(d-g.keyFogEnd)%37<5' in private)
check('opaqueAndNoCompanionGate','p.colors[v].a!=128.0F' in private and 'p.envBag||p.aoBag||p.emisBag' in private)
check('orderSafePreservesHeavyDrips','!heavyActive||heavyNext>=heavyBags.size()' in p and 'i!=111+int(g.first+j)' in private and 'member==0' in private)
check('actualAcceptanceBeforePrepAndAfterMainCull',core.index('if (frustumCheck == OUTSIDE_FRUSTUM)')<core.index('NightMainBatch::accepted();')<core.index('const u32 prepareStart'))
check('reflectionAcceptedHooksExactly2',p.count('{nightMainReflectUse(ri);stapip.core.render(part.bag.get());NightMainBatch::clearPending();}')==2)
check('coldOnlyFullWordOracle','nightMainWord(a.w)!=nightMainWord(b.w)' in private and 'p.colors[v].rgba[k]' in private and 'if(cold)' in private)
check('coldEmitOutsideLoopPrice',r.index('endLoop(pacing)')<r.index('NightMainBatch::emit(p,o,NightAblation::producerEnabled)'))
baseManifest=json.loads((lab/'companion-census-physical-v1/target-source-manifest.json').read_text(encoding='utf8'))
modifiedEngine=[n for n in m['files'] if n.startswith('tyra/engine/') and n.endswith(('.cpp','.hpp')) and m['files'][n]!=baseManifest['files'][n]]
check('modifiedEngineLF',all(b'\r' not in (f/n).read_bytes() for n in modifiedEngine))
inheritedCR=[n for n in m['files'] if n.startswith('tyra/engine/') and n.endswith(('.cpp','.hpp')) and b'\r' in (f/n).read_bytes()]
check('inheritedCRExactFrozenBytes',all(m['files'][n]==baseManifest['files'][n] for n in inheritedCR))
# Mutation controls exercise the protocol/state safety obligations independently
# from a native compiler. They are not a renderer-output oracle.
def admission(first_member,heavy_done,partial,light_pick_same,effective_same,dirty,key_same):
    return first_member and heavy_done and not partial and light_pick_same and effective_same and not dirty and key_same
check('positiveRestrictedAdmission',admission(True,True,False,True,True,False,True))
for name,args in [('laterMemberCannotRetroMerge',(False,True,False,True,True,False,True)),('pendingHeavyRefuses',(True,False,False,True,True,False,True)),('partialFrustumRefuses',(True,True,True,True,True,False,True)),('pickedLightDiffRefuses',(True,True,False,False,True,False,True)),('effectiveLightDiffRefuses',(True,True,False,True,False,False,True)),('dirtyRefusesBeforeClear',(True,True,False,True,True,True,True)),('lightCameraFogKeyChangeRefuses',(True,True,False,True,True,False,False))]:
    check(name,not admission(*args))
out={'status':'PASS','scope':'Pure host source closure and independent guard mutation controls only. No native compile, packet/VU/output/lifetime or activation qualification.','draftManifestSha256':sha(f/'draft-source-manifest.json'),'generatorSha256':sha(lab/'prepare-night-main11-batch-v2.py'),'sourceFiles':501,'unchangedInheritedCRFiles':inheritedCR,'checks':checks}
controlArgs.output.parent.mkdir(parents=True,exist_ok=True)
controlArgs.output.write_text(json.dumps(out,indent=2)+'\n',encoding='utf8')
print(json.dumps({'status':'PASS','checks':len(checks),'sourceFiles':501,'scope':out['scope']}))
