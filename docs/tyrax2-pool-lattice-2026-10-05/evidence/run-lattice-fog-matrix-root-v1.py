from pathlib import Path
import subprocess
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001')
jobs=[(v,r,a) for v in (96,192) for r in (1,2,3) for a in (0,1)]
for i,(v,r,a) in enumerate(jobs):
    if i==0: continue
    subprocess.run(['python3',str(lab/'wild-pool-lattice-fog-probe-runtime-v1/run-case.py'),'--arm',str(a),'--vertices',str(v),'--repeats',str(r),'--slot',str(196+i),'--native-proof',str(lab/'wild-pool-lattice-fog-probe-native-root-review-v1/proof.json')],check=True)
    print('MATRIX_PROGRESS',i+1,len(jobs),flush=True)
