from pathlib import Path
import json,hashlib,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');archive=repo/'docs/tyrax2-empty-car-2026-10-06';stage=lab/'batch-archive-stage-v1'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inventory=json.loads((archive/'inventory.json').read_text(encoding='utf8'));assert len(inventory)==688
for x in inventory:assert h(archive/x['path'])==x['sha256'],x['path']
incoming=[]
plan=json.loads((stage/'staging-plan.json').read_text())
for x in plan['records']:
 p=stage/x['path'];assert h(p)==x['sha256'];incoming.append((p,x['path']))
for suffix,order in [('v5',0),('order1-v1',1)]:
 r=lab/f'tyrax2-closeout-emulator-two-cars-light-buildings-80k-shadow-batch-{suffix}'
 d=json.loads((r/'protocol-analysis.json').read_text());assert d['order']==order and d['status']=='PASS_COMPLETED_PRIVATE_STATIONARY_STATIC_BATCH_CONTRAST_PROTOCOL'
 v=json.loads((r/'root-raster-review.json').read_text());assert v['terminalAbsenceConfirmed'] and v['onAndOffRepresentationsInspected']
 for n in ['launch-attempt.json','owned-launch.json','owned-stop.json','completed-emulator.log','bin-closeout-samples.txt','protocol-analysis.json','root-runtime-review.json','root-raster-review.json','runtime/profile/PCSX2/inis/PCSX2.ini']:
  incoming.append((r/n,f'batch/emulator-order{order}/{n}'))
 for p in sorted(r.glob('capture-*/*')):
  if p.is_file():incoming.append((p,f'batch/emulator-order{order}/'+p.relative_to(r).as_posix()))
for n in ['review-batch-root.py','qualify-batch-emulator-root.py','archive-batch-root.py']:
 incoming.append((lab/n,'batch/preparation/'+n))
reverse=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-order1-v3'
incoming.append((reverse/'root-emulator-review.json','batch/preparation/root-order1-emulator-review.json'))
assert len({n for p,n in incoming})==len(incoming)
for p,n in incoming:
 q=archive/n;assert p.is_file() and not q.exists(),n
for p,n in incoming:
 q=archive/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q);inventory.append(dict(path=n,sha256=h(q)))
readme='''# Stationary batching contrast

Native binding and both owned software-emulator orders pass: ON/OFF/ON and OFF/ON/OFF,5400 loops each. All six sparse cold observations per run retain80187 accepted input vertices and all50 building owners; ON accepts37 carriers,OFF313. The normal54-vertex Ravager blob,other car,road,pine and point-light consumption remain present. Parked camera/car and phase-local point-light words match exactly at corresponding cold offsets. Root inspected full-scene ON/OFF rasters in order0 and OFF rasters in reverse order; reverse ON has shared-ELF order0 image authority and its own accepted-geometry protocol. The reverse capture-on filename was late and actually records phase2 OFF; this is a spot check,not a pixel-equivalence oracle or per-owner raster proof.

The normal static builder runs once. Its authored membership uses-2 for multi-batch objects; snapshot occurs after boot. Solo material geometry is prepared once through the normal world-space rebuild after light setup,300 parts/70800 building input vertices. Combined and solo buffers stay resident in every phase; switches only alter membership and the draw route. No added clocks,waits or phase-switch rebuilds. Startup work,extra retained memory/cache footprint and the common observer are unpriced. The contrast measures net submission representation cost,including transforms,state,culling and packets; it does not isolate a universal per-packet price or preserve the historical fixture's smaller memory footprint.

Four earlier private fixture attempts were rejected: preboot snapshot,incorrect nonnegative-only membership observer,missing solo geometry in OFF,and authored-map versus reserved runtime-pool equality guard. Their logs and inputs are retained as rejected evidence and never used for prices. The final native-v6 proof binds470 inputs,the actual build log,compiled mirrors/objects and ELF;40 parser controls and actual-header sentinel controls pass.

Physical ON/OFF comparisons remain pending a reset-ready console. Emulator timing and HUD FPS are not PS2 measurements. The clean preceding blob-only hardware result is approximately59.94 Hz with15.4753/15.4872/15.4859 ms non-pacing work; it is a separate ELF and does not establish a causal shadow price. No public renderer implementation changed in this experiment.
'''
q=archive/'batch/README.md';q.write_bytes(readme.encode());inventory.append(dict(path='batch/README.md',sha256=h(q)))
(archive/'inventory.json').write_bytes((json.dumps(inventory,indent=2)+'\n').encode())
updates={
 'docs/tyrax2-empty-car-2026-10-06/README.md':'\n### Stationary batching contrast\n\nThe [same-ELF batching fixture](batch/README.md) passes both5400-loop emulator orders with identical80187 accepted inputs and37 ON versus313 OFF carriers. Both representations are prepared once and retained; extra memory and common observer cost remain unpriced. Physical timing awaits reset-ready PS2. Four rejected private preparations are retained without performance claims.\n',
 'docs/tyrax2-closeout.md':'\nThe [stationary batching contrast](tyrax2-empty-car-2026-10-06/batch/README.md) now passes native binding and both5400-loop software-emulator orders. Same sparse geometry/car/camera/light witnesses qualify37 ON versus313 OFF carriers at80187 input vertices. Normal solo and combined buffers remain resident in both arms; startup/extra memory/common observer costs are unpriced. Physical ON/OFF pricing remains pending a reset-ready console; no batching performance conclusion or public optimization is accepted yet.\n',
 'docs/README.md':'\nThe [stationary batching contrast](tyrax2-empty-car-2026-10-06/batch/README.md) qualifies both emulator orders with80187 accepted inputs,37 ON/313 OFF carriers and retained normal geometry. Physical timing is pending; emulator FPS is not hardware pricing.\n',
 'docs/backlog.md':'\nTyraX2 isolation follow-up: [stationary batching ON/OFF](tyrax2-empty-car-2026-10-06/batch/README.md) passes native and both emulator orders with matched sparse contexts and80187 accepted input vertices. Physical comparisons await reset-ready PS2; no production optimization or per-packet price accepted.\n'}
for n,s in updates.items():
 p=repo/n;p.write_bytes(p.read_text(encoding='utf8').replace('\r\n','\n').encode()+s.encode())
print('Archived batching prep/emulator evidence; pins',len(inventory))
