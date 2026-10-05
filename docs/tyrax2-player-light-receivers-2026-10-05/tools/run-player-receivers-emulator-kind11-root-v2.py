import subprocess
from pathlib import Path
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');h=b/'player-light-receivers-root-helpers-v2';s='night-ablation-emulator-player-receivers-kind11-order0-20261005'
subprocess.run(['python3',str(h/'launch-emulator.py'),'--fixture',str(b/'player-light-receivers-physical-v2'),'--stem',s,'--kind','11','--order','0'],check=True)
subprocess.run(['python3',str(h/'qualify-emulator.py'),'--stem',s],check=True)
