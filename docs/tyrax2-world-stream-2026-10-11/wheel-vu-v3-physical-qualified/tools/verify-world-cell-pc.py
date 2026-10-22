"""Qualify calibrated raw-PC exports after the current full world-cell image gate."""
import argparse
import copy
import csv
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true')
p.add_argument('--sdk',type=Path,default=Path('/mnt/c/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2sdk'))
a=p.parse_args();root=a.fixture;sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
platform='physical' if a.physical else 'emulator'
location=root/'physical' if a.physical else root
command=[sys.executable,str(Path(__file__).with_name('verify-world-cell-price.py')),str(root),
         '--platform','ps2' if a.physical else 'emulator']
if a.physical:command+=['--attempt',str(location)]
subprocess.run(command,check=True)
base=json.loads((location/f'world-cell-price-{"ps2" if a.physical else "emulator"}.json').read_text())
assert base['valid'] and base['variant']=='pc-refresh'
b=json.loads((root/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(root/rel)==digest,rel
assert base['elfSha256']==b['elfSha256']
manifest=json.loads((root/'pc-moving-manifest.json').read_text())
assert manifest['periods']==[0,997,0,1733,0,1733]
assert manifest['heldPeriods']==[997,1733,997,1733,997,1733]
calroot=root/'calibration-evidence'
if not calroot.exists():calroot=Path(__file__).resolve().parents[1]/'docs/tyrax2-pc-sampling-2026-10-11/calibration-v1'
cal=json.loads((calroot/'pc-calibration-physical.json').read_text())
assert cal['valid'] and cal['elfSha256']==manifest['calibrationElf']
assert json.loads((calroot/'pc-calibration-manifest.json').read_text())['sdkPins']==manifest['sdkPins']
for rel,digest in manifest['sdkPins'].items():assert sha(a.sdk/rel)==digest,rel
adapter=Path(__file__).with_name('tyrax2-pc-moving.hpp')
store=Path(__file__).with_name('tyrax2-pc-sample-store.hpp')
assert (root/'tyra/engine/inc/debug/pc_moving.hpp').read_bytes()==adapter.read_bytes()
assert (root/'tyra/engine/inc/debug/pc_sample_store.hpp').read_bytes()==store.read_bytes()
assert not re.search(r'^.* [TW] _+mcount$',(root/'symbols.txt').read_text(),re.M)
logpath=root/('physical/console.log' if a.physical else 'emulator/emulator.log')
assert sha(logpath)==base['logSha256']
log=logpath.read_text(errors='replace')
folder=root/('physical/bin' if a.physical else 'game/bin')
rows=list(csv.DictReader((folder/'drive-probe.csv').open()))
lastOffsets={phase:max(int(r['offset']) for r in rows if int(r['phase'])==phase) for phase in range(6)}

def recordGate(text):
    records=[tuple(map(int,r)) for r in re.findall(r'PCMOVING phase=(\d+) kind=(\d+) periodUs=(\d+) rows=(\d+) dropped=(\d+) afterStop=(\d+)',text)]
    assert [(r[0],r[1]) for r in records]==[(p,k) for p in range(6) for k in range(2)]
    for phase,kind,us,n,dropped,late in records:
        assert us==(manifest['heldPeriods'][phase] if kind else manifest['periods'][phase])
        assert dropped==late==0 and (n>=40 if us else n==0)
    return records

def sampleGate(raw,phase,kind,n):
    assert len(raw)==n
    assert all(r['phase']==phase and r['actual']>=r['scheduled'] for r in raw)
    assert all(t['actual']>s['actual'] for s,t in zip(raw,raw[1:]))
    assert all((r['region']&15)==8 and (r['region']>>4)<32 if kind else
               (r['region']&15) in (0,1,2) and 1320<=(r['region']>>4)<=lastOffsets[phase]+1 for r in raw)
    if n:assert len({r['pc'] for r in raw})>=5

records=recordGate(log);pins={};samples={}
for phase,kind,us,n,dropped,late in records:
    path=folder/f'pc-{"held" if kind else "drive"}-{phase}.csv'
    raw=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(path.open())]
    sampleGate(raw,phase,kind,n);pins[path.relative_to(root).as_posix()]=sha(path)
    samples[phase,kind]=raw
negatives=[]
for name,text in [('missing-record',log.replace('PCMOVING phase=0 kind=0','MISSING phase=0 kind=0')),
                  ('overflow',log.replace('dropped=0','dropped=1')),
                  ('late-callback',log.replace('afterStop=0','afterStop=1')),
                  ('wrong-period',log.replace('periodUs=997','periodUs=998'))]:
    try:recordGate(text)
    except AssertionError:negatives.append(name)
    else:raise AssertionError(name)
reference=samples[1,0]
for name,mutate in [('missing-row',lambda r:r.pop()),('wrong-phase',lambda r:r[0].update(phase=0)),
                    ('backwards-time',lambda r:r[1].update(actual=r[0]['actual'])),
                    ('IO-in-drive',lambda r:r[0].update(region=8)),
                    ('warmup-in-drive',lambda r:r[0].update(region=1200<<4))]:
    bad=copy.deepcopy(reference);mutate(bad)
    try:sampleGate(bad,1,0,len(reference))
    except AssertionError:negatives.append(name)
    else:raise AssertionError(name)
report=dict(valid=True,platform=platform,elfSha256=b['elfSha256'],sourceFiles=len(b['files']),
    samplePins=pins,sampleRecords=records,negativeControls=negatives,
    phases=[dict(phase=p,periodUs=manifest['periods'][p]) for p in range(6)],
    logSha256=sha(logpath),currentWorkloadQualificationSha256=sha(location/f'world-cell-price-{"ps2" if a.physical else "emulator"}.json'),
    scope='Qualified current direct+paint raw PC residency; IRQ mask/preemption bias, not exclusive cycles or a60fps ceiling')
(root/f'pc-moving-{platform}.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(valid=True,platform=platform,samples=sum(r[3] for r in records),controls=len(negatives))))
