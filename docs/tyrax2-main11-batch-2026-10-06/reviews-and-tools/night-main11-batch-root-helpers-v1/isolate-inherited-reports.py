from pathlib import Path
import json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'night-main11-batch-controls-v1';o=c/'inherited-unused-unqualified';o.mkdir(exist_ok=False)
allowed={'analyze-loop.py','analyze-night.py','analyze-night-cli.py','corona_controls.py'}
for p in list(c.iterdir()):
 if p.is_file()and p.name not in allowed:p.rename(o/p.name)
(o/'README-UNQUALIFIED.md').write_bytes(b'# Unused inherited copies\n\nThese copied old seed files are not kind29 evidence or current source authority. The scaffold uses only the four analyzers and source-controls headers; these files are not read by its runtime guards or parser. No prior runtime qualification transfers to kind29.\n')
(o/'pins.json').write_bytes((json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in o.iterdir()if p.is_file()},indent=2)+'\n').encode());print('ISOLATED_UNUSED_SEED_COPIES_NOT_KIND29_EVIDENCE')
