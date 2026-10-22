"""Root-only remaining warm cases, serial ownership of a single emulator slot."""
from pathlib import Path
import json, subprocess
root=Path(__file__).parent;lab=root.parent;slot=164;results=[]
for repeats in (2,3):
 for vertices in (96,192):
  for arm in (0,1):
   args=['python3',str(root/'run-case-v2.py'),'--arm',str(arm),'--vertices',str(vertices),'--repeats',str(repeats),'--slot',str(slot)]
   subprocess.run(args,check=True)
   results.append(dict(arm=arm,vertices=vertices,repeats=repeats,slot=slot));slot+=1
out=lab/'pool2-static-warm-matrix-execution.json';assert not out.exists()
out.write_text(json.dumps(dict(status='COMPLETED_EIGHT_ADDITIONAL_FIXED_CASE_CAPTURES_PAIR_CLOSURE_PENDING',cases=results),indent=2)+'\n')
print('Eight warm cases completed',flush=True)
