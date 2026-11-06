"""Convert physical-console FOREST_RESULT lines into CSV and compact ranges."""
import csv
import re
import sys
from pathlib import Path

path=Path(sys.argv[1])
rows=[]
for line in path.read_text(errors='replace').splitlines():
    if 'FOREST_RESULT ' not in line: continue
    row=dict(re.findall(r'(\w+)=([\d.]+)',line.split('FOREST_RESULT ',1)[1]))
    if row: rows.append(row)
if not rows: raise SystemExit('No completed samples')
with path.with_suffix('.csv').open('w',newline='') as out:
    writer=csv.DictWriter(out,fieldnames=list(rows[0]))
    writer.writeheader();writer.writerows(rows)
for copies,lod in dict.fromkeys((r['copies'],r.get('lod','authored')) for r in rows):
    group=[r for r in rows if r['copies']==copies and r.get('lod','authored')==lod]
    fps=[float(r['fps']) for r in group]
    print(f"copies={copies:>5} trees={group[0]['trees']:>5} lod={lod} poses={len(group)} FPS={min(fps):.2f}..{max(fps):.2f} max_p95={max(float(r['p95']) for r in group):.2f}ms")
