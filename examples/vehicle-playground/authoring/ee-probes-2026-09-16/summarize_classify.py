"""Per-pose means of the classification split from a TYRA_STAPIP_ATTRIB arm.

`dsClassify_ms` is `StaPipBagPackager::checkFrustum` measured EXCLUSIVELY -
the merged min/max read, the AABB test and the eight-plane clip mask, summed
over every package and subpackage of the frame. It is therefore the HARD
UPPER BOUND on what any change to how packages are classified can save, S3
included: you cannot save more than the whole bracket costs.

`dsCreate_ms` is inclusive of it (the pooled descriptor build plus the
classification), so `dsCreate - dsClassify` is the descriptor half.

Usage: python summarize_classify.py <resultsDir>
"""
import csv
import statistics
import sys
from pathlib import Path

POSES = ['garage-day', 'garage-night', 'outer-day', 'outer-night']
SHOW = ['spRender_ms', 'dsRetain_ms', 'dsDirect_ms', 'dsCreate_ms',
        'dsClassify_ms', 'dsRender_ms', 'dsFlush_ms',
        'dsDirectBags', 'dsPartialBags', 'dsPackages', 'dsMergeParts',
        'dsMaskCalls']

rows = list(csv.DictReader((Path(sys.argv[1]) / 'frame-attrib.csv').open()))
print(f'{"metric":18}' + ''.join(f'{p:>15}' for p in POSES))
per = []
for phase in range(4):
    rr = [r for r in rows if int(r['phase']) == phase]
    if len(rr) != 240:
        print(f'WARN pose {phase}: {len(rr)} rows', file=sys.stderr)
    per.append({k: statistics.mean(float(r[k]) for r in rr)
                for k in rr[0] if k not in ('frame', 'phase')})
for c in SHOW:
    if c not in per[0]:
        continue
    print(f'{c:18}' + ''.join(f'{per[i][c]:>15.3f}' for i in range(4)))
print(f'{"create-classify":18}' +
      ''.join(f'{per[i]["dsCreate_ms"] - per[i]["dsClassify_ms"]:>15.3f}'
              for i in range(4)))
