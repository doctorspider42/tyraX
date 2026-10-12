"""Snapshot and test A41 dual-edge window predicate; not physical qualification."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('--ee-cxx', type=Path, required=True)
a = p.parse_args()
out = a.out.resolve()
assert not out.exists()
out.mkdir(parents=True)
repo = Path(__file__).resolve().parent
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
names = ('tyrax2-hybrid-copy-window.hpp', 'verify-hybrid-copy-window.cpp',
         'verify-hybrid-copy-window.py')
for name in names:
    shutil.copy2(repo / name, out / name)
pins = {name: sha(out / name) for name in names}
commands = []

def run(label, args):
    commands.append(args)
    r = subprocess.run(args, text=True, capture_output=True)
    (out / (label + '.stdout.log')).write_text(r.stdout)
    (out / (label + '.stderr.log')).write_text(r.stderr)
    assert r.returncode == 0, (label, r.returncode, r.stderr)
    return r.stdout

run('host-version', ['g++', '--version'])
run('ee-version', [str(a.ee_cxx), '--version'])
common = ['-std=c++17', '-Wall', '-Wextra', '-Werror']
results = []
for mode, flags in [('optimized', ['-O2']), ('sanitized', ['-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
    run(mode + '-compile', ['g++', *common, *flags,
        str(out / names[1]), '-o', str(out / mode)])
    results.append(run(mode + '-run', [str(out / mode)]))
assert results[0] == results[1] == (
    'HYBRIDWINDOW cases=280800 accepted=20000 guards=137 physical=UNQUALIFIED\n')
run('ee-compile', [str(a.ee_cxx), *common, '-O2', '-c',
    str(out / names[1]), '-o', str(out / 'window-ee.o')])
assert all(sha(repo / name) == pins[name] == sha(out / name) for name in names)
(out / 'report.json').write_text(json.dumps(dict(valid=True, sources=pins,
    commands=commands, result=results[0], physicalQualified=False,
    limits=['No MMIO sampling/configuration or timer reservation.',
            'Bounds and epoch are synthetic; physical copy budget is unbound.',
            'No actual Core/queue/ISR integration or FPS claim.']), indent=2) + '\n')
(out / 'sha256.json').write_text(json.dumps({f.name: sha(f) for f in out.iterdir()
    if f.is_file()}, indent=2) + '\n')
print(results[0], end='')
