"""Check newly authored bodies after the real importer, and render comparisons.

python verify-remodeled-vehicles.py BASELINE CANDIDATE OUTPUT
Unlike verify-efficient-vehicles.py, this does NOT require identical bodies.
"""
from pathlib import Path
import argparse,json,runpy
import numpy as np
from PIL import Image,ImageDraw,ImageFont
H=runpy.run_path(str(Path(__file__).with_name('verify-efficient-vehicles.py')))

def main(baseline,candidate,output):
    output.mkdir(parents=True,exist_ok=True)
    bm=json.loads(next(baseline.glob('*.tyra')).read_text())
    cm=json.loads(next(candidate.glob('*.tyra')).read_text())
    report={'scope':'Baked resource inventory, not runtime FPS. Body plus four wheels; excludes reflection and shadow replays.', 'vehicles':{}}
    canvas=Image.new('RGB',(1400,1120),(23,29,36));draw=ImageDraw.Draw(canvas)
    font=ImageFont.load_default(size=19)
    row=0
    for v in bm['vehicles']:
        cv=next(x for x in cm['vehicles'] if x['id']==v['id'])
        assert cv['drive']==v['drive'],'Driving anchors changed'
        stem='vehicles/veh-'+v['id'];arms=[];entry={}
        for name,root in [('before',baseline),('after',candidate)]:
            root=root/'.res-baked'
            bb,b=H['read_tmdl'](root/(stem+'-body.tmdl'))
            wb,w=H['read_tmdl'](root/(stem+'-wheel.tmdl'))
            ib,iw=H['inventory'](b),H['inventory'](w)
            entry[name]=dict(body=ib,wheel=iw,near_packages=ib['packages']+4*iw['packages'])
            arms.append((root,bb,b,wb,w))
        if v['name']=='Rally 04':
            for suffix in ('body.tmdl','wheel.tmdl'):
                assert (baseline/'.res-baked'/(stem+'-'+suffix)).read_bytes()==(candidate/'.res-baked'/(stem+'-'+suffix)).read_bytes()
            report['vehicles'][v['name']]=entry;continue
        a,b=arms
        assert entry['after']['body']['triangles']<=600
        assert entry['after']['body']['parts']<=2
        assert entry['after']['body']['packages']<entry['before']['body']['packages']
        assert cv['bodyTris']>=entry['after']['body']['triangles'],'Importer must not decimate the authored body'
        assert np.allclose(a[3],b[3],atol=1e-5)
        # This redesign intentionally changes body shape, but must stay inside
        # a conservative envelope of the original vehicle (including mirrors).
        assert np.all(b[1][0]>=a[1][0]-.15) and np.all(b[1][1]<=a[1][1]+.15)
        lamps=next(p for p in b[2] if p['name']=='lamps')
        assert len(lamps['verts'])>=24
        for p in b[2]+b[4]:
            vs=p['verts'];assert np.isfinite(vs).all()
            assert np.allclose(np.linalg.norm(vs[:,3:6],axis=1),1,atol=1e-4)
            assert vs[:,6:8].min()>=0 and vs[:,6:8].max()<=1
            tri=vs.reshape(-1,3,8)
            n=np.cross(tri[:,1,:3]-tri[:,0,:3],tri[:,2,:3]-tri[:,0,:3])
            assert np.all(np.linalg.norm(n,axis=1)>1e-10),'Degenerate triangle'
        for side,sign in enumerate((1,-1)):
            eye=np.array([.65,.4,sign*.7]);eye/=np.linalg.norm(eye)
            right=np.cross([0,1,0],eye);right/=np.linalg.norm(right);up=np.cross(eye,right)
            basis=np.array([right,up,eye])
            models=[H['combined'](arm[2],arm[4],v['drive']) for arm in arms]
            points=np.concatenate([p['verts'][:,:3] for model in models for p in model])@basis.T
            lo,hi=points[:,:2].min(0),points[:,:2].max(0)
            # Identical orthographic scale in each A/B, with square pixels.
            mid=(lo+hi)/2;span=max((hi-lo)[0]/630,(hi-lo)[1]/220)
            bounds=(mid-np.array([630,220])*span*.53,mid+np.array([630,220])*span*.53)
            for ai,arm in enumerate(arms):
                im,_=H['raster'](models[ai],arm[0],basis,(630,220),bounds,shade=True)
                x,y=side*700+25,(row*2+ai)*280
                canvas.paste(im,(x,y+38));draw.text((x,y+9),v['name']+' / '+('before' if ai==0 else 'remodeled'),font=font,fill='white')
        report['vehicles'][v['name']]=entry;row+=1
    canvas.save(output/'remodeled-vehicles.png')
    (output/'remodeled-vehicles.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','candidate','output'):p.add_argument(name,type=Path)
    a=p.parse_args();main(a.baseline,a.candidate,a.output)
