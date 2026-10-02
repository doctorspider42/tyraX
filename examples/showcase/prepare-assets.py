#!/usr/bin/env python3
"""Optional CC0 import step. Scene rebuilds use the checked-in results.

python prepare-assets.py C:/Assets
Copies only the named low-poly meshes; retains UVs and triangulation, moves
their origins to bottom-centre, and replaces absolute material references.
"""
import argparse
from pathlib import Path
from PIL import Image

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('assets',type=Path)
    args=ap.parse_args()
    root=args.assets/'Medieval Village MegaKit[Standard]'
    out=Path(__file__).resolve().parent/'res'/'aster'
    out.mkdir(parents=True,exist_ok=True)
    for source,target,mat in [('Prop_Vine1','ivy','ivy'),('Prop_Vine4','ivy-small','ivy'),
                              ('Prop_Crate','supply-crate','bark'),('Prop_Wagon','survey-cart','bark')]:
        lines=(root/'OBJ'/(source+'.obj')).read_text().splitlines()
        vertices=[[float(v) for v in line.split()[1:4]] for line in lines if line.startswith('v ')]
        lo=[min(v[i] for v in vertices) for i in range(3)]
        hi=[max(v[i] for v in vertices) for i in range(3)]
        center=[(lo[0]+hi[0])/2,lo[1],(lo[2]+hi[2])/2]
        result=['# Quaternius / Medieval Village MegaKit / CC0','mtllib '+('ivy.mtl' if mat=='ivy' else 'aster.mtl'),'usemtl '+mat]
        for line in lines:
            if line.startswith('v '):
                v=[float(x) for x in line.split()[1:4]]
                result.append('v '+' '.join(f'{v[i]-center[i]:.6f}' for i in range(3)))
            elif line.startswith(('vt ','vn ','f ')):result.append(line)
        (out/(target+'.obj')).write_text('\n'.join(result)+'\n',encoding='utf-8')
    Image.open(root/'Textures'/'T_VineLeaf.png').resize((128,128),Image.Resampling.LANCZOS).save(out/'ivy.png')
    (out/'ivy.mtl').write_text('newmtl ivy\nKd 0.32 0.62 0.24\nmap_Kd ivy.png\n',encoding='utf-8')
    license_text=(root/'License_Standard.txt').read_text()
    (out/'CC0-Quaternius.txt').write_text('\n'.join(line.rstrip() for line in license_text.splitlines())+'\n',encoding='utf-8')
    print('Prepared four CC0 prop meshes and one 128px cutout texture.')

if __name__=='__main__':main()
