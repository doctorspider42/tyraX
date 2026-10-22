import subprocess
from pathlib import Path
b=Path('/mnt/f/Projects/tyrax2-lab-20261001')
h=b/'corona24-pricing-root-helpers-v1'
s='night-ablation-emulator-corona24-order1-20261005'
subprocess.run(['python3',str(h/'launch-emulator.py'),'--fixture',str(b/'corona24-pricing-physical-v1'),'--stem',s,'--kind','9','--order','1'],check=True)
subprocess.run(['python3',str(h/'qualify-emulator.py'),'--stem',s],check=True)
