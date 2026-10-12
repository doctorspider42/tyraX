"""Build isolated A41 timer preflight. No copying, scene pricing or deployment."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('--toolchain', type=Path, required=True)
a = p.parse_args()
out = a.out.resolve()
assert not out.exists()
out.mkdir(parents=True)
bin = out / 'game/bin'
bin.mkdir(parents=True)
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
repo = Path(__file__).resolve().parent
for name in ('tyrax2-window-preflight.cpp', 'tyrax2-window-preflight.py'):
    shutil.copy2(repo / name, out / name)
tc = a.toolchain.resolve()
sdk = tc / 'ps2sdk'
cxx = tc / 'ee/bin/mips64r5900el-ps2-elf-g++'
args = [str(cxx), '-std=c++17', '-D_EE', '-G0', '-O2', '-Wall', '-Wextra',
        '-Werror', '-I' + str(sdk / 'ee/include'), '-I' + str(sdk / 'common/include'),
        '-T' + str(sdk / 'ee/startup/linkfile'), '-L' + str(sdk / 'ee/lib'),
        '-Wl,-zmax-page-size=128', str(out / 'tyrax2-window-preflight.cpp'),
        '-lgraph', '-o', str(bin / 'vehicle-playground.elf')]
env = os.environ.copy()
env.update(PS2DEV=str(tc), PS2SDK=str(sdk))
r = subprocess.run(args, env=env, text=True, capture_output=True)
(out / 'build.stdout.log').write_text(r.stdout)
(out / 'build.stderr.log').write_text(r.stderr)
assert r.returncode == 0, r.stderr
pins = {str(f.relative_to(out)): sha(f) for f in out.rglob('*') if f.is_file()}
(out / 'build.json').write_text(json.dumps(dict(command=args, files=pins,
    signal='480p noninterlaced FRAME, same A40 graph_set_mode', copyEnabled=False,
    scope='Isolated timer ownership/polarity/interval preflight, not full game or copy budget'),
    indent=2) + '\n')
print(json.dumps(dict(elfSha256=sha(bin / 'vehicle-playground.elf'), out=str(out))))
