"""Full-asset gate: hash every PNG/TMDL/MTL under a deployment bin and compare
with the archived reference manifest."""
import hashlib
import json
import sys
from pathlib import Path

bin_dir = Path(sys.argv[1])
ref = json.load(open(sys.argv[2]))
got = {}
for p in bin_dir.rglob('*'):
    if p.suffix.lower() in ('.png', '.tmdl', '.mtl'):
        got[str(p.relative_to(bin_dir)).replace('\\', '/')] = hashlib.sha256(
            p.read_bytes()).hexdigest()
ref = {k.replace('\\', '/'): v for k, v in ref.items()}
missing = sorted(set(ref) - set(got))
extra = sorted(set(got) - set(ref))
differ = sorted(k for k in set(ref) & set(got) if ref[k] != got[k])
print('reference', len(ref), 'deployed', len(got))
print('missing', len(missing), missing[:10])
print('extra  ', len(extra), extra[:10])
print('differ ', len(differ), differ[:10])
sys.exit(0 if not (missing or differ) else 1)
