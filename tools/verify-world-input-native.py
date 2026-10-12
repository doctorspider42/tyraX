"""Bind actual VIF input memory/state component; no rendering or FPS claim."""
import argparse
import hashlib
import json
import re
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture', type=Path)
p.add_argument('--platform', choices=('emulator', 'ps2'), required=True)
p.add_argument('--attempt', type=Path)
a = p.parse_args()
root = a.fixture.resolve()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
b = json.loads((root / 'native-binding.json').read_text())
for rel, digest in b['files'].items():
    assert sha(root / rel) == digest, rel
assert sha(root / 'game/bin/vehicle-playground.elf') == b['elfSha256']
assert sha(root / 'tyra/engine/bin/libtyra.a') == b['engineArchiveSha256']
m = json.loads((root / 'world-input-native-manifest.json').read_text())
assert m['componentOnly']
if a.platform == 'emulator':
    assert not a.attempt
    assert json.loads((root / 'emulator/launch.json').read_text())['elfSha256'] == b['elfSha256']
    logpath = root / 'emulator/emulator.log'
    binpath = root / 'game/bin'
    output = root / 'world-input-native-emulator.json'
else:
    assert a.attempt
    attempt = a.attempt.resolve()
    deploy = json.loads((attempt / 'editor-deploy.json').read_text())
    assert deploy['elfSha256'] == b['elfSha256']
    assert deploy['runner'] == 'src/runner.cpp::resetPs2Link/deployToPs2'
    binpath = attempt / 'bin'
    for rel, digest in json.loads((attempt / 'launch-inputs.json').read_text()).items():
        assert sha(binpath / rel) == digest, rel
    logpath = attempt / 'console.log'
    output = attempt / 'world-input-native-ps2.json'
log = logpath.read_text(errors='replace')
quietpath = binpath / 'log.txt'
# ps2link disables file logging; assertions explicitly flush console output.
assert a.platform == 'ps2' or quietpath.exists()
quiet = quietpath.read_text(errors='replace') if quietpath.exists() else ''
assert 'Assertion failed!' not in log + quiet
assert sha(binpath / 'world-input-sdk.bin') == m['sdkDumpSha256']
marker = r'VIFINPUTDONE comparisons=540 compacted=150 fallback=1230 vuWords=4096 states=2 restored=1 shaderExecuted=0 componentOnly=1'
assert len(re.findall(marker, log)) == 1
r = dict(valid=True, platform=a.platform, elfSha256=b['elfSha256'],
         engineArchiveSha256=b['engineArchiveSha256'], sourceFiles=len(b['files']),
         sdkDumpSha256=m['sdkDumpSha256'], logSha256=sha(logpath),
         quietLogSha256=sha(quietpath) if quietpath.exists() else None, comparisons=540, vuWords=4096,
         incomingStates=2, compactedStreams=150, fallbackStreams=1230,
         restored=True, compactVifExecuted=True, shaderExecuted=False,
         physicalExecution=a.platform == 'ps2', componentOnly=True,
         limits=['No moving rendering/images/leases, double-buffer toggles or FPS qualification.',
                 'No shader microprogram is executed; model gate is separately qualified.',
                 'Run before world init, explicit queue drain and state/data restoration.'])
output.write_text(json.dumps(r, indent=2) + '\n')
print(json.dumps(dict(valid=True, platform=a.platform, comparisons=540,
                     compactVifExecuted=True, physicalExecution=r['physicalExecution'], shaderExecuted=False)))
