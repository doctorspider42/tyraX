"""Inspect the additional GLB and price its body with the existing stripifier.

python verify-cc96-strip-study.py [ASSET_DIRECTORY]
Requires NumPy, Pillow and g++ on PATH. Compiles only the unchanged host
meshstrip.cpp into a static temporary verifier so the result does not depend
on a particular MinGW runtime being present on PATH. Does not import/bake the
game asset.
"""
from pathlib import Path
import argparse
from collections import Counter
import hashlib
import json
import runpy
import subprocess
import tempfile
import numpy as np

HERE=Path(__file__).resolve().parent
H=runpy.run_path(str(HERE/'build-cc96-strip-study.py'))
CPP=r'''
#include "meshstrip.hpp"
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc,char** argv) {
  if(argc!=3) return 2;
  std::ifstream f(argv[1],std::ios::binary|std::ios::ate);
  if(!f) return 3;
  auto bytes=f.tellg(); f.seekg(0);
  std::vector<float> in(static_cast<size_t>(bytes)/sizeof(float)),out;
  f.read(reinterpret_cast<char*>(in.data()),bytes);
  std::vector<unsigned char> ao;
  bool ok=meshstrip::build(in,{},meshstrip::kRun,out,ao,meshstrip::Weld::kFull);
  if(!ok) out=in;
  std::ofstream g(argv[2],std::ios::binary);
  g.write(reinterpret_cast<const char*>(out.data()),out.size()*sizeof(float));
  std::cout << (ok ? "strip" : "list") << " " << meshstrip::kRun;
}
'''


def triangles(v,strip=False,run=75):
    result=Counter()
    ranges=(range(i,min(i+run,len(v))-2) for i in range(0,len(v),run)) if strip else [range(0,len(v),3)]
    for rr in ranges:
        for i in rr:
            tri=v[i:i+3];p=tri[:,:3]
            if np.linalg.norm(np.cross(p[1]-p[0],p[2]-p[0]))<1e-10:continue
            # Compare complete attributes, including normals and UVs. Winding
            # is ignored by the existing stripifier/renderer contract.
            result[tuple(sorted(tuple(float(x) for x in t) for t in tri))]+=1
    return result


def main(asset):
    glb=asset/'cc96-strip-study.glb';doc,acc=H['read_glb'](glb)
    source,srcacc=H['read_glb'](HERE.parent/'res/models/cc96-efficient.glb')
    original={n['name']:n for n in source['nodes'] if n['name'].startswith('Cylinder')}
    actual={n['name']:n for n in doc['nodes'] if n['name'].startswith('Cylinder')}
    assert original.keys()==actual.keys()
    for name,node in actual.items():
        assert node['translation']==original[name]['translation']
        def points(d,a,n):return a(d['meshes'][n['mesh']]['primitives'][0]['attributes']['POSITION'])
        p,q=points(doc,acc,node),points(source,srcacc,original[name])
        assert np.allclose([p.min(0),p.max(0)],[q.min(0),q.max(0)],atol=1e-6)
    entries=[]
    with tempfile.TemporaryDirectory(prefix='cc96-strip-') as td:
        td=Path(td);cpp=td/'check.cpp';cpp.write_text(CPP);exe=td/'check.exe'
        repo=HERE.parents[2]
        subprocess.run(['g++','-std=c++20','-O2','-static','-I',str(repo/'src'),str(cpp),str(repo/'src/meshstrip.cpp'),'-o',str(exe)],check=True)
        body=next(n for n in doc['nodes'] if n['name']=='Body')
        for prim in doc['meshes'][body['mesh']]['primitives']:
            attrs=prim['attributes'];v=np.concatenate([acc(attrs[k]) for k in ['POSITION','NORMAL','TEXCOORD_0']],axis=1)
            ix=acc(prim['indices']).ravel();assert int(ix.max())<len(v)
            assert np.isfinite(v).all() and np.allclose(np.linalg.norm(v[:,3:6],axis=1),1,atol=1e-5)
            assert (v[:,6:]>=0).all() and (v[:,6:]<=1).all()
            corners=v[ix].copy();corners[corners==0]=0
            assert sum(triangles(corners).values())==len(corners)//3
            corners.tofile(td/'in.f32')
            mode,run=subprocess.check_output([str(exe),str(td/'in.f32'),str(td/'out.f32')],text=True).split();run=int(run)
            strip=np.fromfile(td/'out.f32',dtype='<f4').reshape(-1,8)
            assert triangles(corners)==triangles(strip,mode=='strip',run),'Changed surface/normal/UV'
            name=doc['materials'][prim['material']]['name']
            entries.append(dict(material=name,triangles=len(corners)//3,indexed_vertices=len(v),
                list_vertices=len(corners),strip_vertices=len(strip),run=run,accepted=mode=='strip',
                list_packages=(len(corners)+run-1)//run,strip_packages=(len(strip)+run-1)//run,
                surface_and_attributes_equal=True))
    main_body=next(e for e in entries if e['material']=='body')
    assert main_body['accepted'] and main_body['strip_vertices']<main_body['list_vertices']*.65
    report=dict(glb_sha256=hashlib.sha256(glb.read_bytes()).hexdigest(),
        stripifier_sha256=hashlib.sha256((repo/'src/meshstrip.cpp').read_bytes()).hexdigest(),
        weld='kFull: exact position + normal + UV',parts=entries,
        wheel_anchors_and_bounds_unchanged=True,
        note='Host source-geometry experiment; not importer output, runtime package inventory or an FPS result. Lamps should initially remain lists to preserve rear/front ranges.')
    (asset/'strip-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('asset',nargs='?',type=Path,default=HERE.parent/'res/models/cc96-strip-study')
    main(p.parse_args().asset)
