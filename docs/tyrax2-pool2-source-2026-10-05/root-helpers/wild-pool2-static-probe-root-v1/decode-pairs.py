from pathlib import Path
import subprocess
lab=Path(__file__).parents[1]
for count,label in [(96,'A96'),(192,'B192')]:
 for repeat in (2,3):
  stem=f'pool2-static-{label.lower()}'
  out=lab/f'{stem}-repeat{repeat}-pair.json'
  subprocess.run(['python3',str(lab/'wild-pool2-static-probe-design-v2/decode-vu1-output.py'),
   '--savestates','--baseline',str(lab/f'{stem}-arm0-repeat{repeat}-20261005-launch/state.p2s'),
   '--table',str(lab/f'{stem}-arm1-repeat{repeat}-20261005-launch/state.p2s'),
   '--case',label,'--out',str(out)],check=True)
  print('Paired fixed case',label,repeat,flush=True)
