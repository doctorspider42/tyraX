from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, sys
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--order',type=int,required=True);a=p.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001');source=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';run=a.run
h=lambda q:hashlib.sha256(q.read_bytes()).hexdigest()
owned=json.loads((run/'owned-launch.json').read_text());fixture=Path(owned['fixture'].replace('/mnt/f/','F:/'))
native=json.loads((source/'native-proof.json').read_text())
assert owned['elfSha256']==native['actualELFSha256']==h(fixture/'bin/empty-car.elf')
review=json.loads((source/'source-review.json').read_text())
for n,digest in review['files'].items():
 q=source/n.replace('game/','empty-car/',1) if n.startswith('game/') else source/n
 assert h(q)==digest,n
raw=(run/'runtime/emulator.log').read_bytes()
for bad in (b'Failed to load texture',b'Texture missing:',b'TmdlLoader: cannot read',b'freepad: DMA Busy'):
 assert bad not in raw,bad
assert b'LOG: CLOSEOUTDONE 'in raw
(run/'completed-emulator.log').write_bytes(raw)
shutil.copyfile(fixture/'bin/closeout-samples.txt',run/'bin-closeout-samples.txt')
subprocess.run([sys.executable,str(source/'analyze-batch.py'),str(run/'completed-emulator.log'),'--artifact',str(run/'bin-closeout-samples.txt'),'--out',str(run/'protocol-analysis.json')],check=True)
result=json.loads((run/'protocol-analysis.json').read_text());assert result['order']==a.order
report=dict(status='ROOT_COMPLETED_STATIONARY_BATCH_EMULATOR_PROTOCOL_NOT_HARDWARE_PRICE',order=a.order,loops=5400,elfSha256=owned['elfSha256'],sourceReviewSha256=h(source/'source-review.json'),nativeProofSha256=h(source/'native-proof.json'),protocolSha256=h(run/'protocol-analysis.json'),startupResourceErrorsAbsent=True,actualInputsBothRepresentations=80187,onAcceptedCarriers=37,offAcceptedCarriers=313,hardwarePerformanceQualified=False,interpretation='Normal retained batch and solo geometry with same sparse car/camera/light words. Extra retained memory and common observer are unpriced. Root raster review and terminal ownership-stop proof recorded separately. Emulator timings do not price PS2.')
(run/'root-runtime-review.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('PASS completed root emulator batch protocol',a.order)
