from pathlib import Path
import hashlib,json,argparse,datetime
p=argparse.ArgumentParser();p.add_argument('--family',choices=('cycle-reuse','object-route','spot-result-v2'),required=True);a=p.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001');stem=f'night-ablation-emulator-{a.family}-order0-20261006';e=lab/(stem+'-evidence');launch=lab/(stem+'-launch')
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
proof=json.loads((e/'raster-capture-proof.json').read_text())
images={}
for i in range(3):
 f=e/f'phase-{i}.png';assert sha(f)==proof['images'][str(i)]['sha256'];assert sha(f)==sha(launch/f.name)
 images[str(i)]={'file':f.name,'sha256':sha(f),'review':'Full night car, map, lamps, shadows and HUD present; no visible stretched triangles or new gross raster corruption in this stationary capture.'}
record={'status':'ROOT_QUALITATIVE_REVIEW_OF_THREE_OWNED_WARM_EMULATOR_CAPTURES','reviewedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'captureProofSha256':sha(e/'raster-capture-proof.json'),'images':images,'physicalVisualFeedbackObtained':False,'hardwarePerformanceAccepted':False,'limitations':'Stationary emulator screenshots, no motion, target visual feedback, pixel identity or all-scene qualification.'}
out=e/'root-visual-review.json';assert not out.exists();out.write_bytes((json.dumps(record,indent=2)+'\n').encode());print(out)
