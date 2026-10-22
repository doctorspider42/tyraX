"""Factor actual SDK-generated final VIF geometry; never encode packet headers on host."""
import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path


def parse(data):
    spec=importlib.util.spec_from_file_location('terrain_factor',Path(__file__).with_name('tyrax2-offline-terrain-compile.py'))
    factor=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(factor)
    r=factor.Reader(data)
    if (r.word(),r.word())!=(0x31464956,1):
        raise ValueError('Bad VIF source version')
    nx,nz=r.word(),r.word()
    if not 0<nx<=64 or not 0<nz<=64:
        raise ValueError('Bad VIF dimensions')
    records,bindings,total={},{},0
    while True:
        tag=r.word()
        if tag==0x454e4431:
            if r.word()!=len(records) or r.word()!=total or r.at!=len(data):
                raise ValueError('Bad VIF footer')
            break
        if tag!=0x52454331:
            raise ValueError('Bad VIF record')
        key=tuple(r.word() for _ in range(7))
        cx,cz,lod,*edges=key
        if key in records or cx>=nx or cz>=nz or lod not in (1,2,4) or any(e not in (1,2,4) or e<lod for e in edges):
            raise ValueError('Invalid VIF key')
        passes=r.word()
        if passes not in (1,2):
            raise ValueError('Unsupported VIF material passes')
        metadata=[r.word() for _ in range(passes*5)]
        signature=[passes]+metadata+[0]*((2-passes)*5)
        if key[:3] in bindings and bindings[key[:3]]!=signature:
            raise ValueError('Edge changes VIF structural binding')
        bindings[key[:3]]=signature
        streams=[]
        for p in range(passes):
            address,capacity,count,run,textured=metadata[p*5:p*5+5]
            if address>2047 or not 3<=capacity<=72 or count<3 or textured not in (0,1) or run%3:
                raise ValueError('Invalid native VIF binding')
            size=r.word()
            if not size or size%16 or size>1024*1024:
                raise ValueError('Bad VIF extent')
            streams.append(r.take(size));total+=size
        records[key]=(bytes(32),streams+[b'']*(6-passes))
    factor.parse=lambda unused:(0,nx,nz,records)
    compiled,report=factor.compile_data(data)
    report['limits']=['Native SDK final geometry bytes, factored and verified for every effective edge variant.',
                      'Camera and material/VRAM state are late bound; VU layout/program destinations are validated at admission.',
                      'Target bootstrap experiment, not production editor export.']
    metadata={'entries':[bindings[cx,cz,lod] for cz in range(nz) for cx in range(nx) for lod in (1,2,4)]}
    return compiled,report,metadata


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('out',type=Path)
    p.add_argument('--oracle-platform',choices=('emulator','ps2'),required=True)
    a=p.parse_args();data=a.source.read_bytes()
    compiled,report,metadata=parse(data)
    controls={}
    for name,bad in [('truncated',data[:-4]),('trailing',data+b'x'),('magic',b'BAD!'+data[4:]),('version',data[:4]+struct.pack('<I',99)+data[8:])]:
        try:parse(bad)
        except (ValueError,struct.error):controls[name]=True
        else:raise AssertionError('Accepted corrupt VIF source: '+name)
    metadata_bytes=(json.dumps(metadata,indent=2)+'\n').encode('utf-8')
    report.update(valid=True,oraclePlatform=a.oracle_platform,negativeControls=controls,
                  outputSha256=hashlib.sha256(compiled).hexdigest(),metadataSha256=hashlib.sha256(metadata_bytes).hexdigest())
    a.out.mkdir(parents=True,exist_ok=False)
    (a.out/'offline-terrain.bin').write_bytes(compiled)
    (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (a.out/'metadata.json').write_bytes(metadata_bytes)
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
