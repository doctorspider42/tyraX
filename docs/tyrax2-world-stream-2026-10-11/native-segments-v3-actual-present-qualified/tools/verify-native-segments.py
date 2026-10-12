"""Freeze and test window partitioning against actual engine writer headers."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('--engine-inc', type=Path, required=True)
a = p.parse_args()
assert not a.out.exists()
a.out.mkdir(parents=True)
repo = Path(__file__).resolve().parent
for name in ('tyrax2-native-segments.hpp', 'verify-native-segments.cpp', 'verify-native-segments.py'):
    shutil.copy2(repo / name, a.out / name)
for rel in ('renderer/core/paths/path1/frame_vif_writer.hpp',
            'renderer/core/paths/path1/frame_chain_arena.hpp',
            'renderer/core/paths/path1/vif1_chain_check.hpp', 'debug/hardware_trace.hpp'):
    dest = a.out / 'inc' / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(a.engine_inc / rel, dest)
(a.out / 'inc/tamtypes.h').write_text('#pragma once\n#include <stdint.h>\ntypedef uint32_t u32;\n')
for mode, flags in (('optimized', ['-O2']),
                    ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])):
    exe = a.out / ('check-' + mode)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-DTYRA_HARDWARE_TRACE=0', *flags, '-I' + str(a.out / 'inc'),
                    str(a.out / 'verify-native-segments.cpp'), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    (a.out / (mode + '.log')).write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()
pins = {str(f.relative_to(a.out)): hashlib.sha256(f.read_bytes()).hexdigest()
        for f in a.out.rglob('*') if f.is_file()}
(a.out / 'sha256.json').write_text(json.dumps(pins, indent=2) + '\n')
print('PASS preserved writer/storage oracle; native queue and target qualification still pending')
