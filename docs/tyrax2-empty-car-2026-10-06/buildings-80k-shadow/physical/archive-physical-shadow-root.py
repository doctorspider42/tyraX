from pathlib import Path
import hashlib,json,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');out=repo/'docs/tyrax2-empty-car-2026-10-06';run=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-shadow-v2';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
items=json.loads((out/'inventory.json').read_text(encoding='utf8'));assert len(items)==677
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
d=json.loads((run/'root-cadence-review.json').read_text(encoding='utf8'));assert d['protocolPassed'] and d['hardwareTimingQualified'] and d['loops']==5400
copies=[(run/name,'buildings-80k-shadow/physical/'+name)for name in ('launch-inputs.json','owned-launch.json','completed-stdout.raw','stdout.utf8.log','physical-review.json','protocol-analysis.json','root-cadence-review.json','bin/closeout-samples.txt')]
copies +=[(lab/name,'buildings-80k-shadow/physical/'+name)for name in ('finish-shadow-physical.py','qualify-shadow-physical-root.py','archive-physical-shadow-root.py')]
for src,rel in copies:assert src.is_file() and not(out/rel).exists(),rel
for src,rel in copies:
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);items.append(dict(path=rel,sha256=h(dst)))
(out/'inventory.json').write_bytes((json.dumps(items,indent=2)+'\n').encode())
means=[d['byPhase'][str(i)]['chunkMeanNonPacingMs']for i in range(3)]
note=f'''\n### Clean physical blob result\n\nFollowing another user-confirmed physical restart, the corrected asset closure with unchanged ELF completes 5400 PS2 loops without resource errors. Parked / short drive / post-drive non-pacing work is {means[0]:.4f} / {means[1]:.4f} / {means[2]:.4f} ms, retaining approximately 59.94 Hz in all observed windows. Each window has 320 rendered and zero synthetic completions; none of 384 raw entry periods exceeds 25 ms. Six cold observations accept one 54-vertex blob carrier, with the full 80,133-input parent partition retained: 80,187 total input vertices / 37 carriers.\n\nThis supersedes the emulator-only pending checkpoint above. It does not qualify the original missing-resource attempts or supply a causal shadow-only delta against a different ELF. The normal shadow preparation and ground queries are included in the workload. Physical TV confirmation remains pending; three clean emulator images show a soft shadow under Ravager. Every-frame raster/scanout and common observer footprint remain separate.\n'''
p=out/'README.md';p.write_bytes((p.read_text(encoding='utf8')+note).encode())
p=repo/'docs/tyrax2-closeout.md';s=p.read_text(encoding='utf8');s=s.replace('The requested blob continuation currently has native and clean emulator qualification only. Its first physical run is rejected for a missing default glow resource; do not use its timing to price the shadow. The unchanged ELF has a corrected, pinned runtime closure and awaits a fresh physical start.',f'The corrected blob continuation completes 5400 clean PS2 loops at {means[0]:.4f}/{means[1]:.4f}/{means[2]:.4f} ms non-pacing work, approximately 59.94 Hz in all observed windows, zero synthetic measured completions and zero raw entry periods above 25 ms. All six cold observations retain 80,133 parent inputs plus one accepted 54-vertex normal blob. The original missing-resource runs remain rejected. The separate-ELF comparison is not a causal shadow-only price; physical TV confirmation remains pending.')
s += '\n\nThe [clean physical blob result](tyrax2-empty-car-2026-10-06/README.md#clean-physical-blob-result) supersedes the earlier pending hardware checkpoint, preserving all rejected resource-failure evidence.\n';p.write_bytes(s.encode())
for name in ('docs/README.md','docs/backlog.md'):
 p=repo/name;s=p.read_text(encoding='utf8')
 s += f'\nClean blob hardware repetition completes 5400 PS2 loops with the corrected unchanged ELF: {means[0]:.4f}/{means[1]:.4f}/{means[2]:.4f} ms non-pacing work, approximately 59.94 Hz, one separate accepted 54-vertex blob and 80,187 total inputs. Initial missing-resource runs remain rejected and TV confirmation remains pending. The requested same-ELF stationary batching contrast is being prepared.\n';p.write_bytes(s.encode())
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
print('PASS physical shadow archive',len(items),'pins',means)
