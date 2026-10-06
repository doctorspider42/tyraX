from pathlib import Path
import json,hashlib,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');archive=repo/'docs/tyrax2-empty-car-2026-10-06';run=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-shadow-batch-order0-v1'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();inventory=json.loads((archive/'inventory.json').read_text(encoding='utf8'));assert len(inventory)==819
for q in inventory:assert h(archive/q['path'])==q['sha256']
d=json.loads((run/'root-cadence-review.json').read_text());assert d['order']==0 and d['protocolPassed'] and d['hardwareTimingProtocolValid']
assert d['allSixColdTotalInputVertices']==80187 and d['measuredSyntheticCompletions']==0 and d['physicalTVConfirmationPending']
for n in ['owned-launch.json','launch-inputs.json','completed-stdout.raw','stdout.utf8.log','physical-review.json','protocol-analysis.json','root-cadence-review.json','bin/closeout-samples.txt']:
 p=run/n;q=archive/'batch/physical-order0'/n;assert not q.exists();q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q);inventory.append(dict(path=q.relative_to(archive).as_posix(),sha256=h(q)))
p=lab/'archive-batch-physical-order0-root.py';q=archive/'batch/physical-order0'/p.name;shutil.copyfile(p,q);inventory.append(dict(path=q.relative_to(archive).as_posix(),sha256=h(q)))
readme='''# First physical stationary batching contrast

Fresh loader start and pinned native-v6 ELF complete5400 loops on PS2,ON/OFF/ON. Non-pacing inclusive game-loop work is15.4751/20.0728/15.4748 ms; observed entry periods16.6834/33.3668/16.6833 ms,approximately59.94/29.97/59.94 Hz. Each320-loop measured window has320 rendered completions and no synthetic flips. All128 raw entry periods over25 ms occur in OFF; these384 raw loops and960 window loops do not prove every-frame scanout.

OFF minus the two ON control mean is4.5978 ms; outer ON spread0.000348 ms. All six sparse cold observations retain80187 accepted input vertices,70800 building inputs and all50 owner references,with37 ON versus313 OFF accepted carriers. Parked car/camera and phase-local point-light words match. Normal54-vertex blob,road,pine and both car/light-consumption partitions remain present. Normal solo and batch arrays are prepared once and retained in all arms; no waits or clocks were added.

This supports a substantial net submission-representation cost on this workload. It does not isolate pure EE time,a universal fixed bag price,VU/GS cost,or the full production night's cause. Extra retained heap/cache footprint and common observer remain unpriced. Historical full-night counts involve different geometry,materials and effects,so they cannot be linearly priced from this result.

Reverse OFF/ON/OFF hardware confirmation remains pending a physical restart. Root-owned same-ELF emulator ON/OFF image spot checks are recorded; the human explicitly cannot currently inspect TV,so physical visual confirmation remains pending. No public renderer implementation changed. This physical checkpoint supersedes the preparation checkpoint's pending-first-hardware status.
'''
q=archive/'batch/physical-order0/README.md';q.write_bytes(readme.encode());inventory.append(dict(path=q.relative_to(archive).as_posix(),sha256=h(q)))
(archive/'inventory.json').write_bytes((json.dumps(inventory,indent=2)+'\n').encode())
updates={
 'docs/tyrax2-empty-car-2026-10-06/README.md':'\n### First physical batching order\n\nThe [first physical ON/OFF/ON run](batch/physical-order0/README.md) completes5400 loops with15.4751/20.0728/15.4748 ms work and approximately59.94/29.97/59.94 Hz. Same80187 accepted inputs and37/313 carriers give4.5978 ms net representation difference. Reverse-order hardware and TV checks remain pending; this supersedes the preparation checkpoint\'s pending-first-physical status.\n',
 'docs/tyrax2-closeout.md':'\nThe [first physical batching contrast](tyrax2-empty-car-2026-10-06/batch/physical-order0/README.md) completes5400 loops: ON/OFF/ON15.4751/20.0728/15.4748 ms,approximately59.94/29.97/59.94 Hz. Identical80187 accepted inputs and37/313 carriers yield4.5978 ms OFF-minus-ON with0.000348 ms outer spread. This prices net representation on this fixture,not pure EE or fixed bag cost; reverse hardware and physical TV checks remain pending. It supports fragmentation as a practical optimization lead without diagnosing the whole production night.\n',
 'docs/README.md':'\nThe [first physical batching order](tyrax2-empty-car-2026-10-06/batch/physical-order0/README.md) measures4.5978 ms OFF-minus-ON with identical80187 inputs: ON around59.94 Hz,OFF29.97 Hz. Reverse-order hardware and TV confirmation remain pending.\n',
 'docs/backlog.md':'\nTyraX2 batching isolation: [first physical ON/OFF/ON](tyrax2-empty-car-2026-10-06/batch/physical-order0/README.md) measures15.4751/20.0728/15.4748 ms with matching80187 inputs,37/313 carriers and zero synthetic completions. Net representation difference4.5978 ms; reverse-order hardware and TV remain pending. No production optimization or fixed packet-price extrapolation accepted.\n'}
for n,s in updates.items():
 p=repo/n;p.write_bytes(p.read_text(encoding='utf8').replace('\r\n','\n').encode()+s.encode())
print('PASS archived first physical batching order; pins',len(inventory))
