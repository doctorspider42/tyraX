from pathlib import Path
lab=Path('F:/Projects/tyrax2-lab-20261001')
p=lab/'freeze-night-main11-batch-root-v1.py'
assert not p.exists()
s=(lab/'freeze-claude-trial-root-v2.py').read_text().replace("choices=('assert-gate','companion-census','tex1-owned')","choices=('night-main11-batch',)")
p.write_bytes(s.encode())
