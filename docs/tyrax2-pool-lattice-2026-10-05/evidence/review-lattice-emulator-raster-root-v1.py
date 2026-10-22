from pathlib import Path
import json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');out=lab/'wild-pool-lattice-emulator-raster-root-review-v1';assert not out.exists();out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();runs=[]
for order in (0,1):
 stem=f'night-ablation-emulator-pool-lattice-fog-order{order}-20261005';launch=lab/(stem+'-launch');evidence=lab/(stem+'-evidence');m=json.loads((evidence/'machine-evidence.json').read_text());c=json.loads((launch/'raster-capture-proof.json').read_text());assert m['status']=='PASS_COMPLETED_NIGHT_SOURCE_NATIVE_CFG_ASSET_BOUND_RUNTIME' and m['kind']==8 and m['order']==order;assert set(c['images'])=={'0','1','2'}
 for phase,row in c['images'].items():assert row['sha256']==sha(launch/row['filename']) and row['latticeEnabled']==int((int(phase)==1)!=(order==1))
 runs.append({'order':order,'machineProofSha256':sha(evidence/'machine-evidence.json'),'captureProofSha256':sha(launch/'raster-capture-proof.json'),'images':c['images'],'rootVisualReview':'All three actual owned captures inspected: car, roads, light pools, beams, shadows and HUD present; no stretched triangles or visible missing effects.'})
r={'status':'ROOT_REVIEWED_SIX_OWNED_FULL_NIGHT_LATTICE_PHASE_IMAGES','runs':runs,'pixelEqualityClaimed':False,'physicalVisualOrPerformanceAccepted':False};(out/'proof.json').write_bytes((json.dumps(r,indent=2)+'\n').encode());print(r['status'],sha(out/'proof.json'))
