"""Decode exact A44 source captures and compare semantic inputs, not stamps."""
import argparse
import hashlib
import json
import re
import struct
from collections import Counter
from pathlib import Path

FIELDS={1:'material',2:'viewProjection',3:'model',4:'positions',5:'colours',
        6:'singleColour',7:'ST',8:'normals',9:'lightMatrix',10:'lightColours',
        11:'lightDirections',12:'envBasis',13:'textureName',14:'wrap',15:'fog',16:'stamps'}
LIMIT=2*1024*1024

def decode(raw,stage):
    assert len(raw)>=16
    magic,actual,count,size=struct.unpack_from('<4I',raw)
    assert magic==0x53343441 and actual==stage and 0<count<=4096
    assert 0<size<=LIMIT and len(raw)==16+size
    at=16;records=[];occurrences=Counter()
    while at<len(raw):
        length,=struct.unpack_from('<I',raw,at);at+=4
        end=at+length;assert length>0 and end<=len(raw)
        fields={}
        while at<end:
            assert at+8<=end
            kind,n=struct.unpack_from('<2I',raw,at);at+=8
            assert kind in FIELDS and kind not in fields and at+n<=end
            fields[kind]=raw[at:at+n];at+=n
        assert at==end and {1,2,3,4,15,16}<=fields.keys()
        assert len(fields[1])==27*4
        meta=struct.unpack('<27I',fields[1]);owner,n=meta[:2]
        assert 0<n<=LIMIT//16 and meta[22]==0, 'Unsupported billboard source'
        assert len(fields[2])==len(fields[3])==64 and len(fields[4])==n*16
        assert 5 in fields or 6 in fields
        for kind in (5,7,8):
            if kind in fields:assert len(fields[kind])==n*16
        for kind,size in ((6,16),(9,64),(10,64),(11,48),(12,32),(14,24),(15,12),(16,16)):
            if kind in fields:assert len(fields[kind])==size
        if meta[23]:assert {8,9,10,11}<=fields.keys()
        owner=owner if owner<2**31 else owner-2**32
        key=(owner,occurrences[owner]);occurrences[owner]+=1
        records.append((key,fields))
    assert len(records)==count
    return records

def negatives(raw,stage):
    cases={}
    for name,offset,value in (('magic',0,0),('stage',4,stage+1),('count',8,0),
                              ('size',12,LIMIT+1),('recordLength',16,LIMIT+1)):
        changed=bytearray(raw);struct.pack_into('<I',changed,offset,value);cases[name]=changed
    cases['truncated']=raw[:-1]
    changed=bytearray(raw);struct.pack_into('<I',changed,20,999);cases['fieldKind']=changed
    for name,value in cases.items():
        try:decode(value,stage)
        except (AssertionError,struct.error):continue
        raise AssertionError('Malformed source accepted: '+name)
    return list(cases)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('fixture',type=Path);p.add_argument('baseline',type=Path)
    a=p.parse_args();root=a.fixture.resolve();base=a.baseline.resolve()
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    bindings=[json.loads((r/'native-binding.json').read_text()) for r in (root,base)]
    for r,b in zip((root,base),bindings):
        for rel,digest in b['files'].items():assert sha(r/rel)==digest,rel
        assert sha(r/'game/bin/vehicle-playground.elf')==b['elfSha256']
    manifest=json.loads((root/'world-source-manifest.json').read_text())
    assert manifest['baseElfSha256']==bindings[1]['elfSha256']
    assert sha(root/'game/inc/world_source_capture.hpp')==manifest['headerSha256']
    assert json.loads((root/'emulator/launch.json').read_text())['elfSha256']==bindings[0]['elfSha256']
    logpath=root/'emulator/emulator.log';log=logpath.read_text(errors='replace')
    assert 'DRIVEDONE valid=1' in log
    rows=re.findall(r'A44SOURCE stage=(\d+) valid=(\d+) records=(\d+) bytes=(\d+)',log)
    assert len(rows)==9 and [int(r[0]) for r in rows]==list(range(9))
    captures=[];pins={};controls=[]
    for stage,valid,count,size in rows:
        stage=int(stage);assert int(valid)==1
        path=root/'game/bin'/f'source-{stage}.bin';raw=path.read_bytes()
        records=decode(raw,stage)
        assert len(records)==int(count) and len(raw)==16+int(size)
        assert any(k[0]>=0 for k,_ in records) and any(k[0]<=-10000 for k,_ in records)
        captures.append(dict(records));pins[path.relative_to(root).as_posix()]=sha(path)
        controls=negatives(raw,stage)
    images=0;imageMismatches=[];phaseImages=[0]*3
    for stage in range(36):
        for target in ('main','env','display'):
            rel=f'game/bin/cell-{stage}-{target}.bin'
            raw=(root/rel).read_bytes();original=(base/rel).read_bytes()
            if raw!=original:
                imageMismatches.append({'stage':stage,'target':target,
                    'differentBytes':sum(x!=y for x,y in zip(raw,original)),
                    'actualSha256':sha(root/rel),'baselineSha256':sha(base/rel)})
            else:images+=1;phaseImages[stage//12]+=1
            pins[rel]=sha(root/rel)
    comparisons=[]
    for phase in range(3):
        for left,right in ((0,1),(0,2),(1,2)):
            before,after=captures[phase*3+left],captures[phase*3+right]
            changed=[];stamps=[]
            for key in sorted(before.keys()|after.keys()):
                if key not in before or key not in after:
                    changed.append({'key':key,'fields':['submissionPresence']});continue
                x,y=before[key],after[key]
                fields=[FIELDS[k] for k in sorted(x.keys()|y.keys())
                        if k!=16 and x.get(k)!=y.get(k)]
                if fields:changed.append({'key':key,'fields':fields})
                if x[16]!=y[16]:stamps.append(key)
            comparisons.append({'phase':phase,'arms':[left,right],
                                'semanticChanges':changed,'stampChanges':stamps})
    report={'valid':images==108,'experiment':'A44-ordinary-source','elfSha256':bindings[0]['elfSha256'],
            'baselineElfSha256':bindings[1]['elfSha256'],'logSha256':sha(logpath),
            'captureRecords':[len(x) for x in captures],'exactBaselineImages':images,
            'exactImagesByPhase':phaseImages,'imageMismatches':imageMismatches,
            'comparisons':comparisons,'negativeControls':controls,'files':pins,
            'promotionAllowed':False,'physicalPrice':False,
            'limits':manifest['limits']}
    (root/'world-source-emulator.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
    assert report['valid'], 'Source observer changes captured raster'
