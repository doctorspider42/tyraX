"""Offline pixel comparison of root-owned screenshots; no device interaction."""
from pathlib import Path
import argparse,hashlib,json
from PIL import Image,ImageChops
p=argparse.ArgumentParser();p.add_argument('--off',type=Path,required=True);p.add_argument('--on',type=Path,required=True);p.add_argument('--crop',type=int,nargs=4,required=True,metavar=('LEFT','TOP','RIGHT','BOTTOM'));p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists()
x=Image.open(a.off).convert('RGB');y=Image.open(a.on).convert('RGB');assert x.size==y.size
l,t,r,b=a.crop;assert 0<=l<r<=x.width and 0<=t<b<=x.height
x=x.crop((l,t,r,b));y=y.crop((l,t,r,b));pairs=list(zip(x.getdata(),y.getdata()));different=sum(u!=v for u,v in pairs);nonblack=sum(any(c for c in u)for u,v in pairs)
report=dict(status='OBSERVED_OFF_CLOCK_RASTER_PIXEL_COMPARISON',imageSize=list(x.size),rootObservedViewportCrop=a.crop,pixels=len(pairs),differentPixels=different,nonblackBaselinePixels=nonblack,exactPixelsEqual=different==0,maxChannelDifference=max(abs(c-d)for u,v in pairs for c,d in zip(u,v)),inputFiles={str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in(a.off,a.on,Path(__file__))},limits=['Root must bind images to exact owned completed arm/source/native captures and observed viewport coordinates.','Nonblack count alone does not prove the authored texture rendered; inspect both actual screenshots.','This records differences without relabeling unequal pixels as equivalent or proving hardware raster behavior.'])
a.out.write_bytes((json.dumps(report,indent=2)+'\n').encode());print(report['status'],different)
