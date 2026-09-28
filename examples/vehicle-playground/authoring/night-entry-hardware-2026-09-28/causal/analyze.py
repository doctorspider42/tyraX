from pathlib import Path
import csv,statistics,json,sys
folder=Path(sys.argv[1])
def read(name):
 return [{k:float(v) for k,v in r.items()} for r in csv.DictReader(open(folder/name))]
c=read('frame-cost.csv');a=read('frame-attrib.csv');c=[r for r in c if 240<=r['frame']<480];a=[r for r in a if 240<=r['frame']<480]
assert len(c)==len(a)==240
assert all(r['driver']==0 and r['speed']==0 and abs(r['z']+74.303497)<1e-5 for r in c)
for r in c:r['work']=r['update_ms']+r['submit_ms']+r['finish_ms']
d={k:statistics.median(r[k] for r in c) for k in c[0]}
w=sorted(r['work'] for r in c);d.update(p95work=w[int(.95*(len(w)-1))],maxwork=max(w),over20=sum(v>20 for v in w),uploads=sum(r['uploads'] for r in c),reuploads=sum(r['reuploads'] for r in c))
d['attribution']={k:statistics.median(r[k] for r in a) for k in a[0]}
print(folder.name,'cost',{k:round(d[k],4) for k in ['work','p95work','maxwork','over20','fps','update_ms','submit_ms','finish_ms','vif_wait_included_ms','bounds_included_ms','prepare_included_ms','dispatch_included_ms','triangles','uploads','reuploads']})
print('deep attribution',sorted([(k,round(v,4)) for k,v in d['attribution'].items() if k.endswith('_ms')],key=lambda x:-x[1])[:35])
objects=read('object-cost.csv');names=json.loads((Path(__file__).parent/'object-map.json').read_text(encoding='utf-8'))
for r in objects:r['name']=names[str(int(r['object']))]['name']
d['objects']=sorted(objects,key=lambda r:-r['elapsed_ms'])
print('named objects', [{k:r[k] for k in ['object','name','elapsed_ms','bounds_ms','prepare_ms','dispatch_ms','vif_wait_ms','pick_light_ms','texture_ms','bbox_cache_ms','bbox_recalc_ms','calls','triangles']} for r in d['objects'][:10]])
(folder/'summary.json').write_text(json.dumps(d,indent=2)+'\n')
