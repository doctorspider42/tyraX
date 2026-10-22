"""Validate A19 qualification only; deliberately report no timing or speedup."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture', type=Path)
p.add_argument('--archive', action='store_true')
p.add_argument('--output', type=Path)
a = p.parse_args()
root = a.fixture
repo = Path(__file__).resolve().parent
output = a.output or root/'list-join-emulator.json'
if a.archive:
    assert a.output and not output.resolve().is_relative_to(root.resolve())
base_output = output.with_name(output.stem+'-base.json')
cmd = [sys.executable, str(repo/'tyrax2-command-price-analyze.py'), str(root),
       '--output', str(base_output)]
if a.archive:
    cmd.append('--archive')
subprocess.run(cmd, check=True)
base = json.loads(base_output.read_text())
log = root/'emulator/emulator.log'
rows = [tuple(map(int, r)) for r in re.findall(
    r'LISTJOIN stage=(\d+) active=(\d+) calls=(\d+) accepted=(\d+) bytes=(\d+)',
    log.read_text(errors='replace'))]
assert len(rows) == 10 and [r[0] for r in rows] == list(range(10))
for stage, active, calls, accepted, size in rows:
    assert active == int(stage in (2, 8))
    assert 0 <= accepted <= calls and bool(size) == bool(accepted)
    if not active:
        assert (calls, accepted, size) == (0, 0, 0)
assert rows[2][1:] == rows[8][1:]
source = (root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp').read_text()
assert source.count('ListJoin::checkAndApply(*this, pkgs,') == 2
assert source.count('memcpy(vertices + offset, pkg->vertices, bytes);') == 2
manifest = json.loads((root/'list-join-manifest.json').read_text())
assert manifest['qualificationOnly'] and manifest['originalGatherRetained']
result = dict(valid=True, qualificationOnly=True, rasterEqual=base['rasterEqual'],
              elfSha256=base['elfSha256'], logSha256=hashlib.sha256(log.read_bytes()).hexdigest(),
              phase=base['phase'], offset=base['offset'], pose=base['pose'],
              perFrame=dict(calls=rows[2][2], accepted=rows[2][3], bytes=rows[2][4]),
              positiveActivation=rows[2][3]>0, stages=rows,
              limitation='Original gather/oracle retained; no speedup, PS2 or general lifetime qualification')
output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps(result, indent=2))
