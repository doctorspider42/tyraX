import subprocess
from pathlib import Path
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');h=b/'light-pick-far-root-helpers-v1';s='night-ablation-emulator-light-far-order0-20261005'
subprocess.run(['python3',str(h/'launch-emulator.py'),'--fixture',str(b/'light-pick-far-physical-v1'),'--stem',s,'--kind','12','--order','0'],check=True)
subprocess.run(['python3',str(h/'qualify-emulator.py'),'--stem',s],check=True)
