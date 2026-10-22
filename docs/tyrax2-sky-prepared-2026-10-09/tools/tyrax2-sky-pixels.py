"""Stationary A/B and A/A ROI differences, not a moving/depth raster oracle."""
import hashlib
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image
root=Path(sys.argv[1])/'emulator'
rois=dict(worldNoFps=[1,65,640,505],skyAndBuildings=[1,65,640,185],paint=[215,265,420,355])
paths=[root/f'park-phase-{p}.png' for p in range(6)]
images=[np.asarray(Image.open(p).convert('RGB'),dtype=np.int16) for p in paths]
assert all(im.shape==images[0].shape for im in images)
out=dict(scope='Stationary smoke only; A/A is temporal-noise control, not an equivalence threshold',
         hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},rois=rois,comparisons=[])
for a,b in [(0,1),(0,2),(3,4),(3,5)]:
    entry=dict(phases=[a,b],regions={})
    for name,(x0,y0,x1,y1) in rois.items():
        d=np.abs(images[a][y0:y1,x0:x1]-images[b][y0:y1,x0:x1])
        entry['regions'][name]=dict(changedPixels=int(np.any(d,axis=2).sum()),meanAbsRGB=float(d.mean()),maxDelta=int(d.max()))
    out['comparisons'].append(entry)
(root/'pixel-comparison.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps(out,indent=2))
