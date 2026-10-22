"""Root reparse and raw arithmetic of completed physical orders, no new device work."""
from pathlib import Path
import hashlib, json, subprocess, sys
lab=Path('F:/Projects/tyrax2-lab-20261001');out=lab/'wild-pool-lattice-physical-pair-root-review-v1';assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
fixture=lab/'wild-pool-lattice-physical-v7';pins={};runs=[]
def pin(p):pins[str(p)]=sha(p);return p
for order in (0,1):
 d=lab/f'night-ablation-ps2-pool-lattice-fog-order{order}-20261005-evidence'
 m=json.loads(pin(d/'machine-evidence.json').read_text())
 assert m['status']=='PASS_COMPLETED_NIGHT_SOURCE_NATIVE_CFG_ASSET_BOUND_RUNTIME' and m['kind']==8 and m['order']==order
 manifest=json.loads(pin(d/'target-source-manifest.json').read_text());assert manifest['frozen'] and len(manifest['files'])==501
 assert sha(d/'target-source-manifest.json')==m['sourceManifestSha256']==sha(fixture/'target-source-manifest.json')
 for rel,h in manifest['files'].items():assert sha(fixture/rel)==h,rel
 assets=json.loads(pin(d/'runtime-assets-manifest.json').read_text())['files'];assert len(assets)==298
 for rel,h in assets.items():assert sha(fixture/'game/bin'/rel)==h,rel
 assert sha(pin(d/'vehicle-playground.elf'))==m['selectedElfSha256']==sha(fixture/'game/bin/vehicle-playground.elf')
 assert (d/'ps2client-raw.log').read_bytes().decode('latin1').encode('utf8')==(d/'stdout.log').read_bytes()
 for name in ('ps2client-raw.log','stdout.log','night-ablation.log','normalization-proof.json','owned-launch.json','owned-stop.json','client-process.json','host-arrivals.jsonl'):
  pin(d/name)
 assert (d/'night-ablation.cfg').read_text().split()==['8',str(order),'0','0']
 report=out/f'order-{order}-reparse.json'
 command=[sys.executable,str(pin(d/'analyze-night-cli.py')),'--stdout',str(d/'stdout.log'),'--artifact',str(d/'night-ablation.log'),
  '--environment','ps2','--kind','8','--order',str(order),'--joint','0','--restored','0','--report',str(report)]
 r=subprocess.run(command,capture_output=True,text=True);assert r.returncode==0,(r.stdout,r.stderr)
 actual=json.loads(report.read_text());original=json.loads(pin(d/'strict-analysis.json').read_text())
 assert {k:v for k,v in actual.items() if k!='inputPins'}=={k:v for k,v in original.items() if k!='inputPins'}
 assert actual['engine_loops']==5400 and actual['sample_rows']==384 and actual['chunk_rows']==15
 assert actual['poolTableFlags']==[1,1,1] and actual['poolLatticeFlags']==([0,1,0] if order==0 else [1,0,1])
 assert actual['poolTableCountersUntimedFrames']==[750,1155] and len(actual['poolTableWindows'])==6 and not actual['fullTimedWindowTableActivationObserved']
 for counter in actual['poolTableWindows'].values():
  assert counter['applied']>0 and counter['coldMismatches']==0
 phases=[actual['by_phase'][str(i)] for i in range(3)]
 means=[]
 for phase in phases:
  assert phase['tax_loop_count']==320 and phase['rendered_completions']==320 and phase['synthetic_completions']==0
  assert phase['elapsed_ticks_u64']-phase['pacing_ticks_u64']==phase['non_pacing_ticks_u64']
  value=phase['non_pacing_ticks_u64']/320/294912
  assert abs(value-phase['mean_non_pacing_ms'])<1e-12;means.append(value)
 delta=[means[1]-means[0],means[1]-means[2]] if order==0 else [means[0]-means[1],means[2]-means[1]]
 runs.append(dict(order=order,flags=actual['poolLatticeFlags'],machineEvidenceSha256=sha(d/'machine-evidence.json'),
  phaseMeansInclusiveElapsedMinusExistingPacingMs=means,candidateMinusOwnBaselineMs=delta,
  outerArmSpreadMs=abs(means[2]-means[0]),presentPeriodMeanMs=[x['sample_stats']['presentPeriod']['mean_ms']for x in phases],
  counts=dict(loops=5400,rawSamples=384,chunks=15,sparseColdFrames=6),reparseSha256=sha(report)))
pin(Path(__file__))
result=dict(status='ROOT_REPARSED_COMPLETED_PHYSICAL_LATTICE_PAIR_RAW_IDENTITIES_AND_OWN_ARITHMETIC',kind=8,
 sourceManifestSha256=sha(fixture/'target-source-manifest.json'),elfSha256=sha(fixture/'game/bin/vehicle-playground.elf'),
 sourceFiles=501,runtimeAssets=298,sameElfBothOrders=True,loops=10800,rawSamples=768,chunks=30,
 losslessLatin1ProtocolAndStrictReparseBound=True,runs=runs,inputPins=pins,
 verdict='Raw same-ELF contrasts recorded; both orders must be interpreted below. No 60FPS or promotion established.',
 nativeSixtyFPSAccepted=False,productionPromotionAccepted=False,operatorAppearanceConfirmed=False,
 limits=['Inclusive elapsed-minus-existing-pacing is not pure EE, VU or GS cost.',
 'Sparse source expansion witnesses do not count complete timed activation or prove warm replay or packed GS output.',
 'New common layout, branch and apparatus cost remains unpriced. Do not subtract results from older ELF/source.',
 'Physical appearance confirmation is qualitative, not pixel equality. Fixed-case actual output probes remain a separate qualification.'])
(out/'proof.json').write_bytes((json.dumps(result,indent=2)+'\n').encode());print(result['status'],sha(out/'proof.json'))
print(json.dumps(runs,indent=2))
