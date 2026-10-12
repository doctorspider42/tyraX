"""Check exact capture allocation against the actual FrameChainArena under sanitizers."""
import subprocess
import tempfile
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='a18-size-') as tmp:
    tmp=Path(tmp)
    (tmp/'tamtypes.h').write_text('#pragma once\n#include <stdint.h>\ntypedef uint32_t u32;\n')
    exe=tmp/'check'
    subprocess.run(['g++','-std=c++17','-O2','-g','-fsanitize=address,undefined',
        '-DTYRA_HARDWARE_TRACE=0','-I'+str(tmp),'-I'+str(repo/'vendor/tyra/engine/inc'),
        str(repo/'tools/verify-command-size.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
