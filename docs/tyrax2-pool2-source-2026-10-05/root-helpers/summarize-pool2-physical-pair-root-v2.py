"""Root reparse and raw arithmetic of completed physical orders, no new device work."""
from pathlib import Path
import hashlib, json, subprocess, sys
lab=Path('F:/Projects/tyrax2-lab-20261001');out=lab/'wild-pool-table-physical-pair-root-review-v2';assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
fixture=lab/'wild-pool-table-physical-v3';pins={};runs=[]
def pin(p):pins[str(p)]=sha(p);return p
for order in (0,1):
 d=lab/f'night-ablation-ps2-pool-table-order{order}-20261005-evidence'
 m=json.loads(pin(d/'machine-evidence.json').read_text())
 assert m['status']=='PASS_COMPLETED_NIGHT_SOURCE_NATIVE_CFG_ASSET_BOUND_RUNTIME' and m['kind']==7 and m['order']==order
 manifest=json.loads(pin(d/'target-source-manifest.json').read_text());assert manifest['frozen'] and len(manifest['files'])==499
 assert sha(d/'target-source-manifest.json')==m['sourceManifestSha256']==sha(fixture/'target-source-manifest.json')
 for rel,h in manifest['files'].items():assert sha(fixture/rel)==h,rel
 assets=json.loads(pin(d/'runtime-assets-manifest.json').read_text())['files'];assert len(assets)==298
 for rel,h in assets.items():assert sha(fixture/'game/bin'/rel)==h,rel
 assert sha(pin(d/'vehicle-playground.elf'))==m['selectedElfSha256']==sha(fixture/'game/bin/vehicle-playground.elf')
 assert (d/'ps2client-raw.log').read_bytes().decode('latin1').encode('utf8')==(d/'stdout.log').read_bytes()
 for name in ('ps2client-raw.log','stdout.log','night-ablation.log','normalization-proof.json','owned-launch.json','owned-stop.json','client-process.json','host-arrivals.jsonl'):
  pin(d/name)
 assert (d/'night-ablation.cfg').read_text().split()==['7',str(order),'0','0']
 report=out/f'order-{order}-reparse.json'
 command=[sys.executable,str(pin(d/'analyze-night-cli.py')),'--stdout',str(d/'stdout.log'),'--artifact',str(d/'night-ablation.log'),
  '--environment','ps2','--kind','7','--order',str(order),'--joint','0','--restored','0','--report',str(report)]
 r=subprocess.run(command,capture_output=True,text=True);assert r.returncode==0,(r.stdout,r.stderr)
 actual=json.loads(report.read_text());original=json.loads(pin(d/'strict-analysis.json').read_text())
 assert {k:v for k,v in actual.items() if k!='inputPins'}=={k:v for k,v in original.items() if k!='inputPins'}
 assert actual['engine_loops']==5400 and actual['sample_rows']==384 and actual['chunk_rows']==15
 assert actual['poolTableFlags']==([0,1,0] if order==0 else [1,0,1])
 assert actual['poolTableCountersUntimedFrames']==[750,1155] and len(actual['poolTableWindows'])==6 and not actual['fullTimedWindowTableActivationObserved']
 for key,counter in actual['poolTableWindows'].items():
  phase=int(key.split(':')[0]);enabled=actual['poolTableFlags'][phase]
  assert counter==dict(invocations=19,eligible=9,applied=9*enabled,fallback=10,sourceVertices=753,admittedVertices=618*enabled,baselineColorQwords=753,tableColorQwords=18*enabled,coldCompared=9,coldMismatches=0,invalid=0)
 phases=[actual['by_phase'][str(i)] for i in range(3)]
 means=[]
 for phase in phases:
  assert phase['tax_loop_count']==320 and phase['rendered_completions']==320 and phase['synthetic_completions']==0
  assert phase['elapsed_ticks_u64']-phase['pacing_ticks_u64']==phase['non_pacing_ticks_u64']
  value=phase['non_pacing_ticks_u64']/320/294912
  assert abs(value-phase['mean_non_pacing_ms'])<1e-12;means.append(value)
 delta=[means[1]-means[0],means[1]-means[2]] if order==0 else [means[0]-means[1],means[2]-means[1]]
 runs.append(dict(order=order,flags=actual['poolTableFlags'],machineEvidenceSha256=sha(d/'machine-evidence.json'),
  phaseMeansInclusiveElapsedMinusExistingPacingMs=means,candidateMinusOwnBaselineMs=delta,
  outerArmSpreadMs=abs(means[2]-means[0]),presentPeriodMeanMs=[x['sample_stats']['presentPeriod']['mean_ms']for x in phases],
  counts=dict(loops=5400,rawSamples=384,chunks=15,sparseColdFrames=6),reparseSha256=sha(report)))
pin(Path(__file__));pin(lab/'wild-pool-table-postrun-listener-20261005/operator-visual-confirmation.json')
result=dict(status='ROOT_REPARSED_COMPLETED_PHYSICAL_POOL2_PAIR_RAW_IDENTITIES_AND_OWN_ARITHMETIC',kind=7,
 sourceManifestSha256=sha(fixture/'target-source-manifest.json'),elfSha256=sha(fixture/'game/bin/vehicle-playground.elf'),
 sourceFiles=499,runtimeAssets=298,sameElfBothOrders=True,loops=10800,rawSamples=768,chunks=30,
 losslessLatin1ProtocolAndStrictReparseBound=True,runs=runs,inputPins=pins,
 verdict='Small contrasts are inconsistent across order and one candidate-minus-control contrast is positive. No repeatable gain or 60FPS established.',
 nativeSixtyFPSAccepted=False,productionPromotionAccepted=False,operatorAppearanceConfirmed=True,
 limits=['Inclusive elapsed-minus-existing-pacing is not pure EE, VU or GS cost.',
 'Sparse source expansion witnesses do not count complete timed activation or prove warm replay or packed GS output.',
 'New common layout, branch and apparatus cost remains unpriced. Do not subtract results from older ELF/source.',
 'Physical appearance confirmation is qualitative, not pixel equality. Fixed-case actual output probes remain a separate qualification.'])
(out/'proof.json').write_bytes((json.dumps(result,indent=2)+'\n').encode());print(result['status'],sha(out/'proof.json'))
print(json.dumps(runs,indent=2))
