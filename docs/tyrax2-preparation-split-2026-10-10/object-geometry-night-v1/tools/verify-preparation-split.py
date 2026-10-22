"""Run the actual split observer with deterministic clocks under host sanitizers."""
import subprocess,tempfile
from pathlib import Path
with tempfile.TemporaryDirectory(prefix='preparation-split-') as tmp:
    exe=Path(tmp)/'test'
    for geometry in (0,1):
        subprocess.run(['g++','-std=c++17','-O2','-g','-fsanitize=address,undefined',
            '-DPREPARATION_SPLIT_GEOMETRY='+str(geometry),
            str(Path(__file__).with_suffix('.cpp')),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
