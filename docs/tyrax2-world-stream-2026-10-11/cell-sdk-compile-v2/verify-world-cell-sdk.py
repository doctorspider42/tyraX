"""Compile A44 actual SDK cold compiler for EE; not execution or FPS proof."""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('--engine', type=Path, required=True)
p.add_argument('--toolchain', type=Path, required=True)
a = p.parse_args()
out = a.out.resolve()
assert not out.exists()
out.mkdir(parents=True)
repo = Path(__file__).resolve().parents[1]
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
names = ('tyrax2-world-cell-sdk.hpp', 'tyrax2-world-cell-plan.hpp',
         'tyrax2-world-strip-plan.hpp', 'tyrax2-world-body.hpp',
         'verify-world-cell-sdk.cpp', 'verify-world-cell-sdk.py')
for name in names:
    shutil.copy2(repo/'tools'/name, out/name)
compiler = a.toolchain/'ee/bin/mips64r5900el-ps2-elf-g++'
obj = out/'cell-sdk-ee.o'
command = [str(compiler), '-D_EE', '-std=c++17', '-O2', '-Wall', '-Wextra',
           '-Werror', '-isystem', str(a.engine/'inc'),
           '-isystem', str(a.toolchain/'ps2sdk/ee/include'),
           '-isystem', str(a.toolchain/'ps2sdk/common/include'),
           '-MD', '-MF', str(out/'dependencies.d'), '-c',
           str(out/'verify-world-cell-sdk.cpp'), '-o', str(obj)]
r = subprocess.run(command, text=True, capture_output=True)
(out/'compile.stdout.log').write_text(r.stdout)
(out/'compile.stderr.log').write_text(r.stderr)
assert r.returncode == 0, r.stderr
raw = obj.read_bytes()
assert raw[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<HH',raw,16) == (1,8)
deps = (out/'dependencies.d').read_text().replace('\\\n',' ').split(':',1)[1].split()
pins = {}
for index, name in enumerate(sorted(set(deps))):
    f = Path(name).resolve()
    dest = out/'headers'/f'{index:04d}-{f.name}'
    dest.parent.mkdir(exist_ok=True)
    shutil.copy2(f,dest)
    pins[str(f)] = sha(dest)
    assert sha(f) == pins[str(f)]
assert all(sha(out/name) == sha(repo/'tools'/name) for name in names)
report = {'valid': True, 'experiment': 'A44-cell-sdk-compile',
          'command': command, 'compilerSha256': sha(compiler),
          'headerPins': pins, 'eeObjectSha256': sha(obj),
          'promotionAllowed': False,
          'limits': ['Actual target API compilation only; no linked SDK execution.',
                     'No runtime caller, decoded payload/raster or physical A/B yet.',
                     'Bodies are per-pass geometry only, not complete CALL/RET cells.',
                     'View/material heads, VRAM reservations and DMA/GS leases remain unbound.',
                     'nativeStripImage is a caller requirement, not shader-image qualification.']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
inventory = {f.relative_to(out).as_posix(): sha(f)
             for f in out.rglob('*') if f.is_file()}
(out/'sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print(json.dumps({'valid': True, 'headers': len(pins),
                  'eeObjectSha256': sha(obj), 'promotionAllowed': False}))
