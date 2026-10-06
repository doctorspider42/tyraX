from pathlib import Path
import hashlib,json
lab=Path('F:/Projects/tyrax2-lab-20261001')
stem='night-ablation-emulator-main11-batch-v2-order0-20261006'
proof=lab/(stem+'-evidence')/'strict-analysis.json'
r=json.loads(proof.read_text());cold=r['mainBatchColdControls']
assert cold['candidateActivatedInCold'] is False and cold['candidatePricingActivationQualified'] is False and cold['fallbackOnlyCompletedCapture'] is True
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=lab/'night-main11-batch-v2-root-emulator-summary.json';assert not out.exists()
record={'status':'COMPLETED_PRIVATE_EMULATOR_FALLBACK_ONLY_NO_BATCHING_PRICE','engineLoops':r['engine_loops'],'strictAnalysis':str(proof),'strictAnalysisSha256':sha(proof),'activation':cold,'visualReview':{'reviewer':'root','method':'view_image of each owned phase raster','observed':'Car/map/lamps/shadows/HUD normal; no obvious stretched triangles in stationary phase0/1/2. Qualitative emulator review only.','images':{str(p):sha(p) for p in sorted((lab/(stem+'-launch')).glob('phase-*.png'))}},'hardwarePerformanceAccepted':False,'productionPromoted':False,'preflightOnlyObservation':'Native audit was attempted before running provenance process had completed; refused before output creation. Rerun after completed provenance passed. No device launched before completed native release.'}
out.write_bytes((json.dumps(record,indent=2)+'\n').encode())
print(record['status'])
