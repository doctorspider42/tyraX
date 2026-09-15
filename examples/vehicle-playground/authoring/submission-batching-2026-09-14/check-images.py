from PIL import Image
import numpy as np,json
from pathlib import Path
r=Path(__file__).parent;out={}
for a in ['candidate','textured','large','repeat','final','production','gated']:
 if not (r/f'{a}-pose3.png').exists():continue
 out[a]=[]
 for i in range(4):
  x=np.array(Image.open(r/f'{a}-pose{i}.png')).astype(int);y=np.array(Image.open(r/f'control-pose{i}.png')).astype(int);d=np.abs(x-y);m=np.any(d,axis=2);ys,xs=np.where(m)
  out[a].append({'changed_pixels':int(m.sum()),'max_channel_delta':int(d.max()),'bbox':[int(xs.min()),int(ys.min()),int(xs.max()+1),int(ys.max()+1)] if len(xs) else None})
(r/'image-check.json').write_text(json.dumps(out,indent=2))
print(out)
