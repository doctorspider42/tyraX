"""Standalone OBJ budget/topology/material validation; Python 3, no packages."""
import json
import math
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b): return sum(x*y for x,y in zip(a,b))

def check(path):
    vertices, normals, faces, materials, used = [], [], [], set(), set()
    components = defaultdict(list)
    current = ''
    for line in path.read_text().splitlines():
        p = line.split()
        if not p: continue
        if p[0] in ('v','vn'):
            vector = tuple(map(float,p[1:]))
            assert len(vector)==3 and all(map(math.isfinite,vector)), (path,line)
            (vertices if p[0]=='v' else normals).append(vector)
        elif p[0]=='mtllib':
            mtl = path.parent/p[1]
            assert mtl.is_file(), mtl
            for row in mtl.read_text().splitlines():
                q = row.split()
                if q and q[0]=='newmtl': materials.add(q[1])
                if q and q[0]=='map_Kd': assert (mtl.parent/q[1]).is_file()
                if q and q[0]=='d': assert float(q[1])==1
        elif p[0]=='usemtl': used.add(p[1])
        elif p[0]=='o': current=p[1]
        elif p[0]=='f':
            assert len(p)==4, (path,'not triangulated')
            ids = [int(t.split('/')[0])-1 for t in p[1:]]
            assert all(0<=i<len(vertices) for i in ids)
            a,b,c = [vertices[i] for i in ids]
            normal = cross(sub(b,a),sub(c,a))
            assert dot(normal,normal)>1e-16, (path,'degenerate')
            for t in p[1:]:
                ni = int(t.split('/')[2])-1
                assert 0<=ni<len(normals)
                assert dot(normal,normals[ni])>0, (path,'normal/winding mismatch')
            faces.append(ids)
            components[current].append(ids)
    assert used and used<=materials
    # Each authored piece is closed, consistently wound, with positive volume.
    for name, triangles in components.items():
        edges=defaultdict(list)
        volume=0
        for ids in triangles:
            a,b,c=[vertices[i] for i in ids]
            volume+=dot(a,cross(b,c))/6
            for i,j in zip(ids,ids[1:]+ids[:1]):
                edges[tuple(sorted((i,j)))].append((i,j))
        assert all(len(e)==2 and e[0]==e[1][::-1] for e in edges.values()), (path,name,'open/inconsistent')
        assert volume>1e-8, (path,name,'inward')
    lo=[min(p[i] for p in vertices) for i in range(3)]
    hi=[max(p[i] for p in vertices) for i in range(3)]
    budget=12 if '-lod' in path.stem else 64
    assert len(faces)<=budget
    assert abs(lo[1])<1e-6 and 2.6<=hi[1]<=3.4
    assert hi[0]-lo[0]<=1.21 and hi[2]-lo[2]<=1.21
    return dict(triangles=len(faces),minimum=lo,maximum=hi,materials=sorted(used),
                closed_components=len(components))

report={}
for letter in 'abc':
    for suffix in ('','-lod'):
        name=f'spruce-{letter}{suffix}.obj'
        report[name]=check(ROOT/'res/models'/name)
    near,far=[report[f'spruce-{letter}{suffix}.obj'] for suffix in ('','-lod')]
    assert near['minimum']==far['minimum'] and near['maximum']==far['maximum']
print(json.dumps(report,indent=2))
(ROOT/'authoring/spruces-validation.json').write_text(json.dumps(report,indent=2)+'\n')
