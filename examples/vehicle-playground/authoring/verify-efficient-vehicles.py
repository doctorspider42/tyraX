"""Compare independently baked baseline/candidate vehicle assets.

python verify-efficient-vehicles.py BASELINE CANDIDATE OUTPUT_DIRECTORY
Checks near-body geometry, normals, bounds, lamp ranges, material pass ownership,
wheel bounds/winding, driving settings, and unchanged Rally artifacts. Writes
a JSON resource inventory and paired host renders (not game screenshots).
"""
from pathlib import Path
import argparse
import json
import math
import runpy
import numpy as np
from PIL import Image, ImageDraw, ImageFont

helper=runpy.run_path(str(Path(__file__).with_name('prepare-efficient-vehicles.py')))
read_tmdl,raster=helper['read_tmdl'],helper['raster']


def inventory(parts):
    packages=sum(math.ceil(len(p['strips'][0])/p['run']) if len(p['strips'][0])
                 else math.ceil(len(p['verts'])/75) for p in parts)
    return dict(triangles=sum(len(p['verts'])//3 for p in parts),parts=len(parts),
                packages=packages,reflection_parts=sum(bool(p['reflection']) for p in parts),
                lod_triangles=[sum(len(p['lods'][i] if len(p['lods'])>i else p['verts'])//3
                                  for p in parts) for i in range(2)])


def canonical_geometry(parts):
    v=np.concatenate([p['verts'][:,:6] for p in parts])
    # Canonical export/import may round a normal's last bit. Sort on rounded
    # position/normal first, then compare the actual floats to a tight bound.
    keys=np.round(v,4)
    return v[np.lexsort(tuple(keys[:,i] for i in reversed(range(6))))]


def combined(body,wheel,drive):
    parts=list(body)
    for x,z in [(-1,-1),(-1,1),(1,-1),(1,1)]:
        for source in wheel:
            p=dict(source);p['verts']=source['verts'].copy()
            p['verts'][:,:3]+=[x*drive['track']/2,0,z*drive['wheelBase']/2]
            parts.append(p)
    return parts


def main(baseline,candidate,output):
    output.mkdir(parents=True,exist_ok=True)
    bm=json.loads(next(baseline.glob('*.tyra')).read_text())
    cm=json.loads(next(candidate.glob('*.tyra')).read_text())
    report={'scope':'Tier-zero resource inventory; 75-vertex list packages or baked strip runs. '
                    'No culling, wheel batching, reflection/shadow replays or hardware timing.',
            'vehicles':{}}
    canvas=Image.new('RGB',(1280,960),(23,29,36))
    draw=ImageDraw.Draw(canvas);font=ImageFont.load_default(size=20)
    row=0
    for v in bm['vehicles']:
        current=next(x for x in cm['vehicles'] if x['id']==v['id'])
        assert v['drive']==current['drive'], 'Driving settings changed'
        arms=[]
        entry={}
        for name,root in [('before',baseline),('after',candidate)]:
            baked=root/'.res-baked';stem='vehicles/veh-'+v['id']
            bb,body=read_tmdl(baked/(stem+'-body.tmdl'))
            wb,wheel=read_tmdl(baked/(stem+'-wheel.tmdl'))
            arms.append((baked,bb,body,wb,wheel))
            entry[name]={'body':inventory(body),'wheel':inventory(wheel)}
            entry[name]['near_triangles']=inventory(body)['triangles']+4*inventory(wheel)['triangles']
            entry[name]['near_packages']=inventory(body)['packages']+4*inventory(wheel)['packages']
        a,b=arms
        assert np.allclose(a[1],b[1],atol=1e-6,rtol=0), 'Body bounds changed'
        assert np.allclose(a[3],b[3],atol=1e-6,rtol=0), 'Wheel bounds changed'
        error=float(np.max(np.abs(canonical_geometry(a[2])-canonical_geometry(b[2]))))
        ga,gb=canonical_geometry(a[2]),canonical_geometry(b[2])
        assert np.allclose(ga[:,:3],gb[:,:3],atol=1e-6,rtol=0), 'Body position changed'
        assert error<1e-4, ('Body position/normal changed',error)
        entry['max_body_position_normal_error']=error
        for reflected in (False,True):
            pa=[p for p in a[2] if bool(p['reflection'])==reflected]
            pb=[p for p in b[2] if bool(p['reflection'])==reflected]
            if not pa and not pb:
                continue
            assert np.allclose(canonical_geometry(pa),canonical_geometry(pb),atol=1e-4,rtol=0)
        for p in a[2]:
            if p['name']=='lamps':
                q=next(p2 for p2 in b[2] if p2['name']=='lamps')
                assert np.allclose(p['verts'],q['verts'],atol=1e-6,rtol=0)
                assert np.allclose(p['kd'],q['kd'],atol=1e-6,rtol=0)
        report['vehicles'][v['name']]=entry
        if v['name']=='Rally 04':
            for suffix in ('body.tmdl','wheel.tmdl'):
                rel='vehicles/veh-'+v['id']+'-'+suffix
                assert (a[0]/rel).read_bytes()==(b[0]/rel).read_bytes(), 'Rally changed'
            continue
        wheel=b[4][0]['verts']
        for tri in wheel.reshape(-1,3,8):
            normal=np.cross(tri[1,:3]-tri[0,:3],tri[2,:3]-tri[0,:3])
            assert np.dot(normal,tri[:,3:6].mean(0))>0, 'Inside-out wheel triangle'
        masks=[]
        body_pixel_changes=[]
        for col,yaw in enumerate((.7,2.4)):
            s,c=math.sin(yaw),math.cos(yaw)
            basis=[[c,0,-s],[-.3*s,.953939,-.3*c],[.953939*s,.3,.953939*c]]
            points=np.concatenate([p['verts'][:,:3] for p in combined(a[2],a[4],v['drive'])])@np.array(basis).T
            lo,hi=points[:,:2].min(0),points[:,:2].max(0)
            extent=max((hi-lo)[0]/600,(hi-lo)[1]/180)*1.06
            center=(lo+hi)/2
            bounds=(center-np.array([300,90])*extent,center+np.array([300,90])*extent)
            body_images=[];arm_masks=[]
            for armidx,(baked,bb,body,wb,wheels) in enumerate(arms):
                im,mask=raster(combined(body,wheels,v['drive']),baked,basis,(600,180),bounds,True)
                body_im,_=raster(body,baked,basis,(600,180),bounds,True)
                body_images.append(np.asarray(body_im));arm_masks.append(mask)
                px,py=col*640+20,row*480+armidx*230+35
                canvas.paste(im,(px,py))
                draw.text((px,py-27),v['name']+' / '+['before','after'][armidx]+
                          ' / '+['front','rear'][col],font=font,fill='white')
            masks.append(int(np.count_nonzero(arm_masks[0]!=arm_masks[1])))
            diff=np.abs(body_images[0].astype(int)-body_images[1].astype(int))
            assert diff.max()<=2, 'Host body rendering changed beyond rounding tolerance'
            body_pixel_changes.append(dict(changed_pixels=int(np.count_nonzero(diff.max(-1)>2)),
                                           max_channel_difference=int(diff.max())))
        entry['host_body_render_difference']=body_pixel_changes
        entry['host_complete_silhouette_changed_pixels']=masks
        row+=1
        print(v['name'],json.dumps(entry))
    canvas.save(output/'efficient-vehicles.png')
    (output/'efficient-vehicles.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','candidate','output'):
        parser.add_argument(name,type=Path)
    args=parser.parse_args()
    main(args.baseline,args.candidate,args.output)
