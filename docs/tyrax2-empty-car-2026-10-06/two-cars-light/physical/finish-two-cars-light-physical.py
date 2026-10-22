from pathlib import Path
import argparse,json,hashlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--route',choices=['vehicle','gate']);a=p.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001');run=a.run
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
raw=(run/'ps2client.stdout').read_bytes()
assert b'LOG: CLOSEOUTDONE 'in raw,'not completed'
for line in raw.splitlines():
 if b'LOG: CLOSEOUT'in line:line.decode('ascii')
normalized=run/'stdout.utf8.log';normalized.write_bytes(raw.decode('latin1').encode('utf8'))
(run/'completed-stdout.raw').write_bytes(raw)
artifact=run/'bin/closeout-samples.txt';assert artifact.is_file()
assert not any(x in raw for x in (b'Texture missing:',b'TmdlLoader: cannot read',b'Failed to load texture',b'freepad: DMA Busy')),'invalid run'
pins=json.loads((run/'launch-inputs.json').read_text(encoding='utf-8-sig'))
for f in pins['files']:assert h(run/'bin'/f['name'])==f['sha256'],f['name']
script=lab/'tyrax2-closeout-road-two-cars-light-v1/analyze-light.py'
args=[sys.executable,str(script),str(normalized),'--artifact',str(artifact),'--out',str(run/'protocol-analysis.json')]
if a.route:args+=['--route',a.route]
subprocess.run(args,check=True)
data=json.loads((run/'protocol-analysis.json').read_text())
report=dict(status='PASS_PHYSICAL_COMPLETE_PINNED_PROTOCOL_NOT_TV_CERTIFICATION',rawSha256=hashlib.sha256(raw).hexdigest(),rawSnapshot='completed-stdout.raw',normalizedSha256=h(normalized),sampleSha256=h(artifact),launchInputsSha256=h(run/'launch-inputs.json'),rawEncoding='original completion snapshot bytes retained; bijective latin1 to UTF8; protocol rows ASCII; live client may append after snapshot',protocol=data,sourceNativeProvenance='road-two-cars-light-v1 native proof and root source review; all runtime launch input hashes verified',publicCommonCodeCostUnpriced=True,observerCommonFootprintUnpriced=True,nativeSixtyFpsAccepted=False)
(run/'physical-review.json').write_text(json.dumps(report,indent=2)+'\n')
if 'middleMinusOuterMeanNonPacingMs'in data:print('delta_ms',data['middleMinusOuterMeanNonPacingMs'],'outer_spread_ms',data['outerControlSpreadMs'])
for phase,r in data['byPhase'].items():print(phase,'work_ms',round(r['chunkMeanNonPacingMs'],4),'period_ms',round(r['chunkMeanEntryMs'],4))
